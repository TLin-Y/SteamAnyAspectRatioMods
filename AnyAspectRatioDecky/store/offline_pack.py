import threading
from contextlib import contextmanager
from dataclasses import dataclass, field
from functools import wraps
from pathlib import Path

from helper import PLUGIN_NAME
from helper.errors import PluginError
from installer import staging
from scanner.mod_catalog import ModDefinition, SourceKind, load_mod_catalog
from store.dependencies import (
    MAX_CATALOG_SIZE_BYTES,
    repository_for,
    repository_overrides,
    save_catalog,
)
from store.github_releases import latest_release
from store.mod_sources import MAX_MOD_FILE_SIZE_BYTES, mod_readme_path
from store.plugin_store import DEFAULT_CATALOG_PATH, PluginStore, Settings
from utils import files, folders, sizes, zips
from utils.network import OfflineModeError, download

MAX_PACK_TOTAL_BYTES = 2 * sizes.GIB
MAX_PACK_DOWNLOAD_BYTES = MAX_PACK_TOTAL_BYTES + 64 * sizes.MIB
OFFLINE_PACK_NAME = f"{PLUGIN_NAME}-Offline-Mods"
_pack_lock = threading.RLock()


@contextmanager
def pack_operation():
    if not _pack_lock.acquire(blocking=False):
        raise PluginError("Another offline pack operation is in progress. Try again when it finishes.")
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


# Offline pack naming, should keep same format for backward compatibility: <name>-<version>-part<i>-of-<n>.zip.
def archive_names(version: str, count: int) -> list[str]:
    if count == 1:
        return [f"{OFFLINE_PACK_NAME}-{version}.zip"]
    return [f"{OFFLINE_PACK_NAME}-{version}-part{index}-of-{count}.zip" for index in range(1, count + 1)]


def parse_part_name(name: str) -> tuple[str, int, int] | None:
    prefix, suffix = f"{OFFLINE_PACK_NAME}-", ".zip"
    if not (name.startswith(prefix) and name.endswith(suffix)):
        return None
    version, _, numbers = name[len(prefix):-len(suffix)].rpartition("-part")
    index, _, count = numbers.partition("-of-")
    if not version or not all(number.isascii() and number.isdigit() for number in (index, count)):
        return None
    return version, int(index), int(count)


def wanted_paths(mods: tuple[ModDefinition, ...]) -> tuple[set[str], set[str]]:
    required: set[str] = set()
    optional: set[str] = set()
    for mod in mods:
        match mod.source.kind:
            case SourceKind.FILES | SourceKind.DIRECTORY:
                required.update(staging.source_file_map(mod).values())
            case _ if mod.source.path is not None:
                required.add(mod.source.path)
        optional.add(mod_readme_path(mod))
    return required, optional


def _release_pack_assets(repository: str, release: dict[str, object], tag: str) -> list[tuple[str, str]]:
    version = tag.removeprefix("v")
    download_url = f"https://github.com/{repository}/releases/download/{tag}"
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise PluginError(f"Latest release {tag} has no offline pack assets.")

    asset_urls = {
        asset["name"]: asset["browser_download_url"]
        for asset in assets
        if isinstance(asset, dict)
        and isinstance(asset.get("name"), str)
        and isinstance(asset.get("browser_download_url"), str)
    }
    (single_name,) = archive_names(version, 1)
    if asset_urls.get(single_name) == f"{download_url}/{single_name}":
        return [(single_name, asset_urls[single_name])]

    parts: dict[int, tuple[int, str]] = {}
    for name, url in asset_urls.items():
        match parse_part_name(name):
            case (part_version, index, count) if part_version == version:
                if count < 2 or not 1 <= index <= count or url != f"{download_url}/{name}" or index in parts:
                    raise PluginError(f"Latest release {tag} has invalid offline pack parts.")
                parts[index] = (count, name)
    if not parts:
        raise PluginError(f"Latest release {tag} does not contain {single_name} or a complete set of parts.")

    count = next(iter(parts.values()))[0]
    if sorted(parts) != list(range(1, count + 1)) or any(part_count != count for part_count, _ in parts.values()):
        raise PluginError(f"Latest release {tag} has an incomplete offline pack part set.")
    return [(parts[index][1], asset_urls[parts[index][1]]) for index in range(1, count + 1)]


def _sibling_parts(selected: Path) -> tuple[Path, ...]:
    """Selecting one part of a split pack imports every part, which must sit in the same folder."""
    match parse_part_name(selected.name):
        case None:
            return (selected,)
        case (_, index, count) if count < 2 or index > count:
            raise PluginError("The selected offline pack part has an invalid filename.")
        case (version, _, count):
            names = archive_names(version, count)
            prefix = f"{OFFLINE_PACK_NAME}-{version}-part"
            present = {path.name for path in selected.parent.iterdir() if path.name.startswith(prefix)}
            if present != set(names):
                raise PluginError(f"Keep all {count} offline pack parts in the same folder and select one part.")
            return tuple(selected.parent / name for name in names)


def _resolve_pack_paths(selection: Path | tuple[Path, ...]) -> tuple[Path, ...]:
    paths = selection if isinstance(selection, tuple) else _sibling_parts(selection)
    if not paths or any(not path.is_file() for path in paths):
        raise PluginError("An offline pack file is missing.")
    return paths


# Asynchronous download of the latest release pack, and GUI end will prevent 2nd attempt.
@_exclusive_pack_operation
def download_release_pack(store: PluginStore, settings: Settings) -> dict[str, object]:
    if settings.offline_mode:
        raise OfflineModeError("Turn off offline mode before downloading the offline pack.")
    repository_url, _ = repository_for(settings)
    repository, release, tag = latest_release(repository_url)
    assets = _release_pack_assets(repository, release, tag)
    store.ensure_directories()

    with folders.temporary(store.data_dir) as scratch:
        archives = []
        total_downloaded = 0
        for name, url in assets:
            archive = scratch / name
            total_downloaded += download(url, archive, MAX_PACK_DOWNLOAD_BYTES - total_downloaded, "Offline pack download")
            archives.append(archive)
        current_settings = store.load_settings()
        if current_settings.offline_mode or current_settings.source_key() != settings.source_key():
            raise PluginError("Offline mode or catalog source changed during the download; try again.")
        result = install_pack(store, settings, tuple(archives))

    return {**result, "version": tag.removeprefix("v")}


def _pack_root(names: set[str], catalog_path: str) -> str:
    """The top folder of the zip; it must hold the catalog at its repository path."""
    roots = {
        name[: -len(catalog_path)]
        for name in names
        if name.endswith("/" + catalog_path) and name.count("/") == catalog_path.count("/") + 1
    }
    if len(roots) != 1:
        raise PluginError(f"The archive must contain exactly one {catalog_path}")
    return roots.pop()


@dataclass
class _PackExtraction:
    """Extracts the catalog and the files it needs from each pack part into staging."""

    staging: Path
    catalog_path: str
    settings: Settings
    catalog: bytes | None = None
    mods: tuple[ModDefinition, ...] = ()
    required: set[str] = field(default_factory=set)
    optional: set[str] = field(default_factory=set)
    found: set[str] = field(default_factory=set)
    total_bytes: int = 0

    def add_part(self, archive_path: Path) -> None:
        with zips.open_zip(archive_path, "The selected file is not a valid zip archive") as archive:
            names = set(archive.namelist())
            root = _pack_root(names, self.catalog_path)
            self._read_catalog(archive, root + self.catalog_path)
            for path in sorted(self.required | self.optional):
                if root + path in names:
                    self._extract(archive, root + path, path)

    def _read_catalog(self, archive: zips.ZipFile, member: str) -> None:
        catalog = zips.read_file(archive, archive.getinfo(member), MAX_CATALOG_SIZE_BYTES, "Catalog")
        if self.catalog is None:
            self.catalog = catalog
            self.mods = load_mod_catalog(content=catalog, **repository_overrides(self.settings))
            self.required, self.optional = wanted_paths(self.mods)
            files.write_bytes_atomic(folders.resolve_inside(self.staging, self.catalog_path), catalog)
            self.total_bytes += len(catalog)
        elif catalog != self.catalog:
            raise PluginError("Offline pack parts contain different catalogs.")

    def _extract(self, archive: zips.ZipFile, member: str, path: str) -> None:
        if path in self.found:
            raise PluginError(f"Offline pack parts contain duplicate file {path}.")
        info = archive.getinfo(member)
        if info.is_dir() or info.file_size > MAX_MOD_FILE_SIZE_BYTES:
            raise PluginError(f"Unusable archive entry: {path}")
        self.total_bytes += info.file_size
        if self.total_bytes > MAX_PACK_TOTAL_BYTES:
            raise PluginError(f"The pack exceeds {sizes.label(MAX_PACK_TOTAL_BYTES)}")
        target = folders.resolve_inside(self.staging, path)
        zips.extract_file(archive, info, target, MAX_MOD_FILE_SIZE_BYTES, "Offline pack file")
        self.found.add(path)

    def check_complete(self) -> None:
        if self.catalog is None:
            raise PluginError(f"The archive does not contain {self.catalog_path}")
        missing = self.required - self.found
        if missing:
            raise PluginError(f"The archive set is missing {sorted(missing)[0]}")


def _save_pack_catalog(store: PluginStore, settings: Settings, catalog: bytes) -> None:
    """Make the pack's catalog the active one unless a downloaded catalog for this source is newer."""
    meta = store.catalog_meta()
    if (
        meta is None
        or meta.get("source") != list(settings.source_key())
        or not store.catalog_path.is_file()
        or meta.get("source_url") == "offline pack"
    ):
        save_catalog(store, settings, catalog, "offline pack")


@_exclusive_pack_operation
def install_pack(store: PluginStore, settings: Settings, selection: Path | tuple[Path, ...]) -> dict[str, object]:
    archive_paths = _resolve_pack_paths(selection)
    if any(not zips.is_zip(path) for path in archive_paths):
        raise PluginError("The selected file is not a zip archive")

    with folders.scratch((store.data_dir / "pack.tmp").resolve()) as staging:
        extraction = _PackExtraction(staging, settings.catalog_path or DEFAULT_CATALOG_PATH, settings)
        for archive_path in archive_paths:
            extraction.add_part(archive_path)
        extraction.check_complete()
        store.clear_pack()
        files.move(staging, store.pack_dir)

    _save_pack_catalog(store, settings, extraction.catalog)
    return {"mod_count": len(extraction.mods), **store.pack_stats()}
