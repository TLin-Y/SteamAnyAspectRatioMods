import os
import re
import shutil
import tempfile
import threading
from contextlib import contextmanager
from functools import wraps
from pathlib import Path
from zipfile import BadZipFile, ZipFile, ZipInfo, is_zipfile

from scanner.mod_catalog import ModDefinition, load_mod_catalog
from scanner.mod_installer import (
    MAX_MOD_FILE_SIZE_BYTES,
    OfflineModeError,
    copy_limited,
    mod_readme_path,
    open_url,
)
from scanner.plugin_updater import latest_release
from store.dependencies import (
    MAX_CATALOG_SIZE_BYTES,
    repository_for,
    repository_overrides,
    save_catalog,
)
from store.plugin_store import DEFAULT_CATALOG_PATH, PluginStore, Settings

MAX_PACK_TOTAL_BYTES = 2 * 1024 * 1024 * 1024
MAX_PACK_DOWNLOAD_BYTES = MAX_PACK_TOTAL_BYTES + 64 * 1024 * 1024
OFFLINE_PACK_PART_PATTERN = re.compile(
    r"AnyAspectRatio-Offline-Mods-(?P<version>.+)-part(?P<index>\d+)-of-(?P<count>\d+)\.zip"
)
_pack_lock = threading.RLock()


@contextmanager
def pack_operation():
    if not _pack_lock.acquire(blocking=False):
        raise ValueError("Another offline pack operation is in progress. Try again when it finishes.")
    try:
        yield
    finally:
        _pack_lock.release()


def _exclusive_pack_operation(function):
    @wraps(function)
    def wrapped(*args, **kwargs):
        with pack_operation():
            return function(*args, **kwargs)
    return wrapped


def _wanted_paths(mods: tuple[ModDefinition, ...]) -> tuple[set[str], set[str]]:
    """Return (required, optional) repository paths: mod sources and READMEs."""
    required: set[str] = set()
    optional: set[str] = set()
    for mod in mods:
        source = mod.source
        if source.kind == "files":
            required.update(item.path for item in source.files)
        elif source.kind == "directory":
            required.update(f"{source.path}/{path}" for path in mod.files)
        elif source.path is not None:
            required.add(source.path)
        optional.add(mod_readme_path(mod))
    return required, optional


def _release_pack_assets(
    repository: str, release: dict[str, object], tag: str
) -> list[tuple[str, str]]:
    version = tag.removeprefix("v")
    base_name = f"AnyAspectRatio-Offline-Mods-{version}"
    expected_url = f"https://github.com/{repository}/releases/download/{tag}"
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise ValueError(f"Latest release {tag} has no offline pack assets.")

    asset_urls = {
        asset["name"]: asset["browser_download_url"]
        for asset in assets
        if isinstance(asset, dict)
        and isinstance(asset.get("name"), str)
        and isinstance(asset.get("browser_download_url"), str)
    }
    single_name = f"{base_name}.zip"
    if asset_urls.get(single_name) == f"{expected_url}/{single_name}":
        return [(single_name, asset_urls[single_name])]

    parts: dict[int, tuple[int, str, str]] = {}
    for name, url in asset_urls.items():
        match = OFFLINE_PACK_PART_PATTERN.fullmatch(name)
        if match is None or match["version"] != version:
            continue
        index, count = int(match["index"]), int(match["count"])
        if (
            count < 2
            or index < 1
            or index > count
            or url != f"{expected_url}/{name}"
            or index in parts
        ):
            raise ValueError(f"Latest release {tag} has invalid offline pack parts.")
        parts[index] = (count, name, url)

    if not parts:
        raise ValueError(
            f"Latest release {tag} does not contain {single_name} or a complete set of parts."
        )
    count = next(iter(parts.values()))[0]
    if len(parts) != count or any(
        index not in parts or parts[index][0] != count
        for index in range(1, count + 1)
    ):
        raise ValueError(f"Latest release {tag} has an incomplete offline pack part set.")
    return [(parts[index][1], parts[index][2]) for index in range(1, count + 1)]


def _resolve_pack_paths(archive_path: Path | tuple[Path, ...]) -> tuple[Path, ...]:
    if isinstance(archive_path, tuple):
        paths = archive_path
    else:
        match = OFFLINE_PACK_PART_PATTERN.fullmatch(archive_path.name)
        if match is None:
            paths = (archive_path,)
        else:
            version, count = match["version"], int(match["count"])
            if count < 2 or int(match["index"]) > count:
                raise ValueError("The selected offline pack part has an invalid filename.")
            prefix = f"AnyAspectRatio-Offline-Mods-{version}-part"
            candidates = [
                candidate
                for candidate in archive_path.parent.iterdir()
                if candidate.name.startswith(prefix)
            ]
            expected_names = {
                f"{prefix}{index}-of-{count}.zip" for index in range(1, count + 1)
            }
            if {candidate.name for candidate in candidates} != expected_names:
                raise ValueError(
                    f"Keep all {count} offline pack parts in the same folder and select one part."
                )
            paths = tuple(
                archive_path.parent / name
                for name in sorted(
                    expected_names,
                    key=lambda name: int(
                        OFFLINE_PACK_PART_PATTERN.fullmatch(name)["index"]
                    ),
                )
            )
    if not paths or any(not path.is_file() for path in paths):
        raise ValueError("An offline pack file is missing.")
    return paths


@_exclusive_pack_operation
def download_release_pack(store: PluginStore, settings: Settings) -> dict[str, object]:
    """Download and import the latest release's single-file or multipart offline pack."""
    if settings.offline_mode:
        raise OfflineModeError("Turn off offline mode before downloading the offline pack.")
    repository_url, _ = repository_for(settings)
    repository, release, tag = latest_release(repository_url)
    version = tag.removeprefix("v")
    assets = _release_pack_assets(repository, release, tag)
    store.ensure_directories()
    with tempfile.TemporaryDirectory(prefix="offline-download-", dir=store.data_dir) as scratch:
        archives = []
        total_downloaded = 0
        for name, url in assets:
            archive = Path(scratch) / name
            with open_url(url) as response, archive.open("wb") as target:
                total_downloaded += copy_limited(
                    response,
                    target,
                    MAX_PACK_DOWNLOAD_BYTES - total_downloaded,
                    "Offline pack download",
                )
            archives.append(archive)
        current_settings = store.load_settings()
        if current_settings.offline_mode or current_settings.source_key() != settings.source_key():
            raise ValueError("Offline mode or catalog source changed during the download; try again.")
        result = install_pack(store, settings, tuple(archives))
    return {**result, "version": version}


def _pack_root(names: set[str], catalog_path: str) -> str:
    """The repository folder in a release zip: the one prefix that holds the catalog."""
    roots = {
        name[: -len(catalog_path)]
        for name in names
        if name.endswith("/" + catalog_path) and name.count("/") == catalog_path.count("/") + 1
    }
    if len(roots) != 1:
        raise ValueError(f"The archive must contain exactly one {catalog_path}")
    return roots.pop()


def _extract_member(archive: ZipFile, info: ZipInfo, staging: Path, path: str) -> None:
    destination = staging / path
    if not destination.resolve().is_relative_to(staging.resolve()):
        raise ValueError(f"Unsafe path in catalog: {path}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with archive.open(info) as source, destination.open("wb") as target:
        shutil.copyfileobj(source, target)


@_exclusive_pack_operation
def install_pack(
    store: PluginStore, settings: Settings, archive_path: Path | tuple[Path, ...]
) -> dict[str, object]:
    """Extract needed files from one repository-layout zip or a complete part set."""
    archive_paths = _resolve_pack_paths(archive_path)
    if any(not is_zipfile(path) for path in archive_paths):
        raise ValueError("The selected file is not a zip archive")
    catalog_path = settings.catalog_path or DEFAULT_CATALOG_PATH
    staging = store.data_dir / "pack.tmp"
    shutil.rmtree(staging, ignore_errors=True)
    catalog_bytes: bytes | None = None
    try:
        required: set[str] = set()
        optional: set[str] = set()
        found: set[str] = set()
        total = 0
        for archive_path in archive_paths:
            with ZipFile(archive_path) as archive:
                names = set(archive.namelist())
                root = _pack_root(names, catalog_path)
                catalog_info = archive.getinfo(root + catalog_path)
                if catalog_info.file_size > MAX_CATALOG_SIZE_BYTES:
                    raise ValueError(
                        f"Catalog exceeds the {MAX_CATALOG_SIZE_BYTES // 1024 // 1024} MiB limit"
                    )
                part_catalog = archive.read(catalog_info)
                if catalog_bytes is None:
                    catalog_bytes = part_catalog
                    with tempfile.TemporaryDirectory(dir=store.data_dir) as probe:
                        probe_path = Path(probe) / "catalog.json"
                        probe_path.write_bytes(catalog_bytes)
                        mods = load_mod_catalog(
                            probe_path, **repository_overrides(settings)
                        )
                    required, optional = _wanted_paths(mods)
                    catalog_destination = staging / catalog_path
                    if not catalog_destination.resolve().is_relative_to(staging.resolve()):
                        raise ValueError(f"Unsafe catalog path: {catalog_path}")
                    catalog_destination.parent.mkdir(parents=True, exist_ok=True)
                    catalog_destination.write_bytes(catalog_bytes)
                    total += len(catalog_bytes)
                elif part_catalog != catalog_bytes:
                    raise ValueError("Offline pack parts contain different catalogs.")

                for path in sorted(required | optional):
                    member = root + path
                    if member not in names:
                        continue
                    if path in found:
                        raise ValueError(f"Offline pack parts contain duplicate file {path}.")
                    info = archive.getinfo(member)
                    if info.is_dir() or info.file_size > MAX_MOD_FILE_SIZE_BYTES:
                        raise ValueError(f"Unusable archive entry: {path}")
                    total += info.file_size
                    if total > MAX_PACK_TOTAL_BYTES:
                        raise ValueError(f"The pack exceeds {MAX_PACK_TOTAL_BYTES // 1024**3} GiB")
                    _extract_member(archive, info, staging, path)
                    found.add(path)

        missing = required - found
        if missing:
            raise ValueError(f"The archive set is missing {sorted(missing)[0]}")
        if catalog_bytes is None:
            raise ValueError(f"The archive does not contain {catalog_path}")
        store.clear_pack()
        os.replace(staging, store.pack_dir)
    except BadZipFile as error:
        raise ValueError("The selected file is not a valid zip archive") from error
    finally:
        shutil.rmtree(staging, ignore_errors=True)

    meta = store.catalog_meta()
    if (
        meta is None
        or meta.get("source") != list(settings.source_key())
        or not store.catalog_path.is_file()
        or meta.get("source_url") == "offline pack"
    ):
        save_catalog(store, settings, catalog_bytes, "offline pack")
    return {"mod_count": len(mods), **store.pack_stats()}
