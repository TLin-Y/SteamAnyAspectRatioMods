import hashlib
import os
import tempfile
from dataclasses import dataclass, replace
from pathlib import Path

from scanner.mod_catalog import (
    ModDefinition,
    bundled_catalog_path,
    load_mod_catalog,
    read_catalog_repository,
)
from scanner.mod_installer import read_url_limited, repository_file_url
from store.plugin_store import (
    DEFAULT_CATALOG_PATH,
    PluginStore,
    Settings,
    utc_now,
    write_json_atomic,
)

MAX_CATALOG_SIZE_BYTES = 2 * 1024 * 1024


@dataclass(frozen=True)
class CatalogSource:
    origin: str
    mods: tuple[ModDefinition, ...]
    warning: str | None = None


def _with_bundled_resolution_metadata(
    mods: tuple[ModDefinition, ...], settings: Settings
) -> tuple[ModDefinition, ...]:
    bundled_url, bundled_ref = read_catalog_repository()
    if (
        any(mod.ini_resolution for mod in mods)
        or (settings.catalog_repository_url and settings.catalog_repository_url.rstrip("/") != bundled_url.rstrip("/"))
        or (settings.catalog_ref and settings.catalog_ref != bundled_ref)
        or (settings.catalog_path and settings.catalog_path != DEFAULT_CATALOG_PATH)
    ):
        return mods
    bundled = {mod.mod_id: mod for mod in load_mod_catalog(bundled_catalog_path())}
    result = []
    for mod in mods:
        original = bundled.get(mod.mod_id)
        if (
            original is not None
            and mod.source.kind == "archive"
            and mod.source.sha256
            and mod.source.sha256 == original.source.sha256
            and mod.source.path == original.source.path
            and mod.source.destination == original.source.destination
            and mod.steam_app_id == original.steam_app_id
            and mod.files == original.files
            and mod.ini_file == original.ini_file
        ):
            mod = replace(mod, ini_resolution=original.ini_resolution)
        result.append(mod)
    return tuple(result)


def repository_overrides(settings: Settings) -> dict[str, str | None]:
    return {
        "repository_url": settings.catalog_repository_url or None,
        "repository_ref": settings.catalog_ref or None,
    }


def load_active_catalog(store: PluginStore, settings: Settings) -> CatalogSource:
    """Prefer the cached catalog fetched for the current settings; fall back to the bundled one."""
    overrides = repository_overrides(settings)
    pack_catalog = store.pack_dir / (settings.catalog_path or DEFAULT_CATALOG_PATH)
    if settings.offline_mode and pack_catalog.is_file():
        try:
            mods = load_mod_catalog(pack_catalog, **overrides)
            return CatalogSource("cache", _with_bundled_resolution_metadata(mods, settings))
        except (OSError, ValueError) as error:
            raise ValueError(f"Offline pack catalog is unusable: {error}") from error
    meta = store.catalog_meta()
    warning = None
    if (
        meta is not None
        and store.catalog_path.is_file()
        and meta.get("source") == list(settings.source_key())
    ):
        try:
            mods = load_mod_catalog(store.catalog_path, **overrides)
            return CatalogSource("cache", _with_bundled_resolution_metadata(mods, settings))
        except (OSError, ValueError) as error:
            warning = f"Cached catalog is unusable ({error}); using the bundled catalog."
    return CatalogSource(
        "bundled",
        load_mod_catalog(bundled_catalog_path(), **overrides),
        warning,
    )


def _download_catalog(url: str) -> bytes:
    return read_url_limited(url, MAX_CATALOG_SIZE_BYTES, "Catalog")


def save_catalog(
    store: PluginStore,
    settings: Settings,
    content: bytes,
    source_url: str,
) -> tuple[tuple[ModDefinition, ...], bool, str]:
    """Validate catalog bytes and atomically make them the cached catalog."""
    store.ensure_directories()
    descriptor, temporary_name = tempfile.mkstemp(dir=store.cache_dir, suffix=".json.tmp")
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    try:
        temporary_path.write_bytes(content)
        mods = load_mod_catalog(temporary_path, **repository_overrides(settings))
        digest = hashlib.sha256(content).hexdigest()
        previous = store.catalog_meta()
        changed = previous is None or previous.get("sha256") != digest
        os.replace(temporary_path, store.catalog_path)
    finally:
        temporary_path.unlink(missing_ok=True)

    fetched_at = utc_now()
    write_json_atomic(
        store.catalog_meta_path,
        {
            "fetched_at": fetched_at,
            "sha256": digest,
            "mod_count": len(mods),
            "source_url": source_url,
            "source": list(settings.source_key()),
        },
    )
    return mods, changed, fetched_at


def repository_for(settings: Settings) -> tuple[str, str]:
    bundled_url, bundled_ref = read_catalog_repository()
    return (
        settings.catalog_repository_url or bundled_url,
        settings.catalog_ref or bundled_ref,
    )


def catalog_url(settings: Settings) -> str:
    repository_url, repository_ref = repository_for(settings)
    return repository_file_url(
        repository_url,
        repository_ref,
        settings.catalog_path or DEFAULT_CATALOG_PATH,
    )


def refresh_catalog_on_start(store: PluginStore, settings: Settings) -> bool | None:
    """Best-effort catalog refresh at plugin load; returns whether it changed, None when skipped.

    An imported offline pack does not block the refresh: the newest online catalog wins, and pack
    files are then used only while their checksum still matches it. Cached mod archives are only
    dropped when the catalog actually changed.
    """
    if settings.offline_mode:
        return None
    url = catalog_url(settings)
    _, changed, _ = save_catalog(store, settings, _download_catalog(url), url)
    if changed:
        store.clear_archive_cache()
        store.local_index_path.unlink(missing_ok=True)
    return changed


def update_dependencies(store: PluginStore, settings: Settings) -> dict[str, object]:
    """Fetch the catalog from the configured repository and drop cached mod archives."""
    url = catalog_url(settings)
    mods, changed, fetched_at = save_catalog(store, settings, _download_catalog(url), url)
    store.clear_archive_cache()
    return {
        "catalog_changed": changed,
        "mod_count": len(mods),
        "fetched_at": fetched_at,
    }
