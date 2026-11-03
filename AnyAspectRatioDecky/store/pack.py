import os
import shutil
import tempfile
from pathlib import Path
from zipfile import BadZipFile, ZipFile, is_zipfile

from scanner.mod_catalog import ModDefinition, load_mod_catalog
from scanner.mod_installer import MAX_MOD_FILE_SIZE_BYTES, mod_readme_path
from helper.constants import MOD_SOURCE_DIRECTORY, MOD_SOURCE_FILES
from store.dependencies import (
    MAX_CATALOG_SIZE_BYTES,
    repository_overrides,
    save_catalog,
)
from store.plugin_store import DEFAULT_CATALOG_PATH, PluginStore, Settings

MAX_PACK_TOTAL_BYTES = 2 * 1024 * 1024 * 1024


def _wanted_paths(mods: tuple[ModDefinition, ...]) -> tuple[set[str], set[str]]:
    """Return (required, optional) repository paths: mod sources and READMEs."""
    required: set[str] = set()
    optional: set[str] = set()
    for mod in mods:
        source = mod.source
        if source.kind == MOD_SOURCE_FILES:
            required.update(item.path for item in source.files)
        elif source.kind == MOD_SOURCE_DIRECTORY:
            required.update(f"{source.path}/{path}" for path in mod.files)
        elif source.path is not None:
            required.add(source.path)
        optional.add(mod_readme_path(mod))
    return required, optional


def install_pack(store: PluginStore, settings: Settings, archive_path: Path) -> dict[str, object]:
    """Extract only the catalog, mod files, and READMEs from a repository-layout zip."""
    if not archive_path.is_file() or not is_zipfile(archive_path):
        raise ValueError("The selected file is not a zip archive")
    catalog_path = settings.catalog_path or DEFAULT_CATALOG_PATH
    staging = store.data_dir / "pack.tmp"
    shutil.rmtree(staging, ignore_errors=True)
    try:
        with ZipFile(archive_path) as archive:
            names = set(archive.namelist())
            roots = [
                name[: -len(catalog_path)]
                for name in names
                if name.endswith("/" + catalog_path) and name.count("/") == catalog_path.count("/") + 1
            ]
            if not roots:
                raise ValueError(f"The archive does not contain {catalog_path}")
            root = roots[0]

            catalog_info = archive.getinfo(root + catalog_path)
            if catalog_info.file_size > MAX_CATALOG_SIZE_BYTES:
                raise ValueError(f"Catalog exceeds the {MAX_CATALOG_SIZE_BYTES // 1024 // 1024} MiB limit")
            catalog_bytes = archive.read(catalog_info)
            with tempfile.TemporaryDirectory(dir=store.data_dir) as probe:
                probe_path = Path(probe) / "catalog.json"
                probe_path.write_bytes(catalog_bytes)
                mods = load_mod_catalog(probe_path, **repository_overrides(settings))

            required, optional = _wanted_paths(mods)
            total = 0
            for path in sorted(required | optional | {catalog_path}):
                member = root + path
                if member not in names:
                    if path in required:
                        raise ValueError(f"The archive is missing {path}")
                    continue
                info = archive.getinfo(member)
                if info.is_dir() or info.file_size > MAX_MOD_FILE_SIZE_BYTES:
                    raise ValueError(f"Unusable archive entry: {path}")
                total += info.file_size
                if total > MAX_PACK_TOTAL_BYTES:
                    raise ValueError(f"The pack exceeds the {MAX_PACK_TOTAL_BYTES // 1024**3} GiB limit")
                destination = staging / path
                if not destination.resolve().is_relative_to(staging.resolve()):
                    raise ValueError(f"Unsafe path in catalog: {path}")
                destination.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(info) as source, destination.open("wb") as target:
                    shutil.copyfileobj(source, target)
        store.clear_pack()
        os.replace(staging, store.pack_dir)
    except BadZipFile as error:
        raise ValueError("The selected file is not a valid zip archive") from error
    finally:
        shutil.rmtree(staging, ignore_errors=True)

    save_catalog(store, settings, catalog_bytes, "offline pack")
    store.clear_archive_cache()
    return {"mod_count": len(mods), **store.pack_stats()}
