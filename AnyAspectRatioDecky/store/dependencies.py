import hashlib
import os
import tempfile
from dataclasses import dataclass
from pathlib import Path
from urllib.error import HTTPError, URLError

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


def repository_overrides(settings: Settings) -> dict[str, str | None]:
    return {
        "repository_url": settings.catalog_repository_url or None,
        "repository_ref": settings.catalog_ref or None,
    }


def load_active_catalog(store: PluginStore, settings: Settings) -> CatalogSource:
    """Prefer the cached catalog fetched for the current settings; fall back to the bundled one."""
    overrides = repository_overrides(settings)
    meta = store.catalog_meta()
    warning = None
    if (
        meta is not None
        and store.catalog_path.is_file()
        and meta.get("source") == list(settings.source_key())
    ):
        try:
            return CatalogSource("cache", load_mod_catalog(store.catalog_path, **overrides))
        except (OSError, ValueError) as error:
            warning = f"Cached catalog is unusable ({error}); using the bundled catalog."
    return CatalogSource(
        "bundled",
        load_mod_catalog(bundled_catalog_path(), **overrides),
        warning,
    )


def _download_catalog(url: str) -> bytes:
    try:
        return read_url_limited(url, MAX_CATALOG_SIZE_BYTES, "Catalog")
    except HTTPError as error:
        raise ValueError(
            f"Catalog download failed: HTTP {error.code} for {url}. "
            "Check the repository, ref, and catalog path, and that the file is pushed."
        ) from error
    except URLError as error:
        raise ValueError(f"Catalog download failed: {error.reason}") from error


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
