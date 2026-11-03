import sys
from dataclasses import asdict
from pathlib import Path
from urllib.error import URLError

import decky

plugin_dir = str(Path(__file__).resolve().parent)
if plugin_dir not in sys.path:
    sys.path.insert(0, plugin_dir)

from helper.constants import (
    BACKEND_MESSAGE_FIELD,
    BACKEND_READY_MESSAGE,
    BACKEND_STATUS_FIELD,
    BACKEND_STATUS_READY,
    LOG_EVENT_BACKEND_STATUS_REQUESTED,
    LOG_EVENT_CATALOG_FALLBACK,
    LOG_EVENT_DEPENDENCY_UPDATE_COMPLETED,
    LOG_EVENT_DEPENDENCY_UPDATE_FAILED,
    LOG_EVENT_DEPENDENCY_UPDATE_STARTED,
    LOG_EVENT_MOD_FILE_OPERATION_COMPLETED,
    LOG_EVENT_MOD_FILE_OPERATION_FAILED,
    LOG_EVENT_MOD_FILE_OPERATION_STARTED,
    LOG_EVENT_PLUGIN_STARTED,
    LOG_EVENT_PLUGIN_STOPPED,
    LOG_EVENT_SETTINGS_REJECTED,
    LOG_EVENT_SETTINGS_SAVED,
    LOG_EVENT_SUPPORTED_GAME_SCAN_COMPLETED,
    LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED,
    LOG_EVENT_SUPPORTED_GAME_LIBRARY_UNAVAILABLE,
    LOG_EVENT_SUPPORTED_GAME_SCAN_STARTED,
    LOG_SCAN_PHASE_FORMAT,
    STEAM_LIBRARY_UNAVAILABLE_WARNING,
)
from logger.plugin_logger import PluginLogger, attach_file_handler
from scanner import scan_supported_games as scan_supported_games_report
from scanner.local_archives import scan_local_archives
from scanner.mod_catalog import ModDefinition, read_catalog_repository, summarize_credits
from scanner.mod_installer import (
    apply_mod_files,
    fetch_mod_readme,
    read_mod_ini,
    remove_mod_files,
    set_archive_cache_dir,
    set_pack_dir,
    write_mod_ini,
)
from store import (
    DEFAULT_CATALOG_PATH,
    PluginStore,
    Settings,
    install_pack,
    load_active_catalog,
    update_dependencies,
    validate_settings,
)


log = PluginLogger(decky.logger)
_store: PluginStore | None = None


def get_store() -> PluginStore:
    """Open the persistent data directory once and wire the cache and file log to it."""
    global _store
    if _store is None:
        data_dir = (
            getattr(decky, "DECKY_PLUGIN_RUNTIME_DIR", None)
            or getattr(decky, "DECKY_PLUGIN_SETTINGS_DIR", None)
            or Path.home() / ".local" / "share" / "AnyAspectRatio"
        )
        store = PluginStore(Path(data_dir))
        store.ensure_directories()
        set_archive_cache_dir(store.archive_cache_dir)
        set_pack_dir(store.pack_dir)
        attach_file_handler(decky.logger, store.log_path)
        _store = store
    return _store


class Plugin:
    @staticmethod
    def _scan():
        store = get_store()
        settings = store.load_settings()
        source = load_active_catalog(store, settings)
        report = scan_supported_games_report(
            source.mods,
            local_archive_dir=settings.local_archive_dir or None,
            local_index_path=store.local_index_path,
        )
        if source.warning:
            log.warning(LOG_EVENT_CATALOG_FALLBACK)
            report.warnings.append(source.warning)
        return report

    @staticmethod
    def _resolve_mod_game(mod_id: str, game_path: str) -> tuple[ModDefinition, str]:
        report = Plugin._scan()
        mod = report.mods.get(mod_id)
        if mod is None:
            raise ValueError("Mod is not present in the catalog")
        game = next(
            (
                candidate
                for candidate in report.supported_games
                if candidate.mod_id == mod_id and candidate.game_path == game_path
            ),
            None,
        )
        if game is None or game.game_path is None:
            raise ValueError("Game installation is no longer available")
        return mod, game.game_path

    @staticmethod
    def _get_mod_definition(mod_id: str) -> ModDefinition:
        mod = Plugin._scan().mods.get(mod_id)
        if mod is None:
            raise ValueError("Mod is not present in the catalog")
        return mod

    @staticmethod
    def _scan_local_archives(store: PluginStore, settings: Settings):
        return scan_local_archives(
            settings.local_archive_dir or None,
            store.local_index_path,
        )

    async def get_plugin_status(self) -> dict[str, str]:
        log.info(LOG_EVENT_BACKEND_STATUS_REQUESTED)
        return {
            BACKEND_STATUS_FIELD: BACKEND_STATUS_READY,
            BACKEND_MESSAGE_FIELD: BACKEND_READY_MESSAGE,
        }

    async def get_cached_scan(self) -> dict[str, object] | None:
        return get_store().load_last_scan()

    async def get_credits(self) -> dict[str, object]:
        store = get_store()
        source = load_active_catalog(store, store.load_settings())
        return {
            "mod_count": len(source.mods),
            "authors": summarize_credits(source.mods),
            "pack": store.pack_stats(),
        }

    async def import_offline_pack(self, archive_path: str) -> dict[str, object]:
        store = get_store()
        try:
            return install_pack(store, store.load_settings(), Path(archive_path))
        except (OSError, ValueError) as error:
            log.exception(LOG_EVENT_DEPENDENCY_UPDATE_FAILED, phase="pack-import")
            return {"error": str(error) or type(error).__name__}

    async def clear_local_data(self) -> dict[str, object]:
        get_store().clear_local_data()
        return {}

    async def get_settings(self) -> dict[str, object]:
        store = get_store()
        settings = store.load_settings()
        source = load_active_catalog(store, settings)
        default_url, default_ref = read_catalog_repository()
        archives, warnings = self._scan_local_archives(store, settings)
        meta = store.catalog_meta() if source.origin == "cache" else None
        return {
            "settings": asdict(settings),
            "defaults": {
                "catalog_repository_url": default_url,
                "catalog_ref": default_ref,
                "catalog_path": DEFAULT_CATALOG_PATH,
            },
            "catalog": {
                "origin": source.origin,
                "mod_count": len(source.mods),
                "fetched_at": meta.get("fetched_at") if meta else None,
                "warning": source.warning,
            },
            "local_archives": {"count": len(archives), "warnings": warnings},
            "paths": {
                "data_dir": str(store.data_dir),
                "log_file": str(store.log_path),
            },
        }

    async def save_settings(
        self,
        catalog_repository_url: str,
        catalog_ref: str,
        catalog_path: str,
        local_archive_dir: str,
    ) -> dict[str, object]:
        store = get_store()
        try:
            requested = validate_settings(
                Settings(
                    catalog_repository_url,
                    catalog_ref,
                    catalog_path,
                    local_archive_dir,
                )
            )
        except ValueError:
            log.warning(LOG_EVENT_SETTINGS_REJECTED)
            raise

        # Values equal to the shipped defaults are stored as "use default".
        default_url, default_ref = read_catalog_repository()
        settings = Settings(
            "" if requested.catalog_repository_url == default_url else requested.catalog_repository_url,
            "" if requested.catalog_ref == default_ref else requested.catalog_ref,
            "" if requested.catalog_path == DEFAULT_CATALOG_PATH else requested.catalog_path,
            requested.local_archive_dir,
        )
        previous = store.load_settings()
        store.save_settings(settings)
        source_changed = previous.source_key() != settings.source_key()
        if source_changed:
            store.clear_catalog_cache()
            store.clear_archive_cache()
            store.clear_pack()
        if previous.local_archive_dir != settings.local_archive_dir:
            store.local_index_path.unlink(missing_ok=True)
        log.info(LOG_EVENT_SETTINGS_SAVED, phase=f"source-changed-{source_changed}")
        return {"saved": True, "source_changed": source_changed}

    async def update_dependencies(self) -> dict[str, object]:
        store = get_store()
        settings = store.load_settings()
        log.info(LOG_EVENT_DEPENDENCY_UPDATE_STARTED)
        try:
            result = update_dependencies(store, settings)
        except (OSError, URLError, ValueError) as error:
            log.exception(LOG_EVENT_DEPENDENCY_UPDATE_FAILED)
            return {"error": str(error) or type(error).__name__}
        store.local_index_path.unlink(missing_ok=True)
        archives, warnings = self._scan_local_archives(store, settings)
        log.info(
            LOG_EVENT_DEPENDENCY_UPDATE_COMPLETED,
            phase=f"mods-{result['mod_count']}-changed-{result['catalog_changed']}",
        )
        return {**result, "local_archive_count": len(archives), "warnings": warnings}

    async def scan_supported_games(self) -> dict[str, object]:
        log.info(LOG_EVENT_SUPPORTED_GAME_SCAN_STARTED)
        try:
            report = self._scan()
        except (OSError, ValueError):
            log.exception(LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED)
            raise
        unavailable_warning = STEAM_LIBRARY_UNAVAILABLE_WARNING.split(
            "{path}", maxsplit=1
        )[0]
        for warning in report.warnings:
            if warning.startswith(unavailable_warning):
                log.warning(LOG_EVENT_SUPPORTED_GAME_LIBRARY_UNAVAILABLE)
        log.info(
            LOG_EVENT_SUPPORTED_GAME_SCAN_COMPLETED,
            phase=LOG_SCAN_PHASE_FORMAT.format(
                libraries=report.steam_scan.libraries_scanned,
                apps=report.steam_scan.apps_scanned,
                supported=len(report.supported_games),
            ),
        )
        result = report.to_dict()
        try:
            get_store().save_last_scan(result)
        except OSError:
            log.warning(LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED, phase="cache-write")
        return result

    async def set_mod_applied(
        self,
        mod_id: str,
        game_path: str,
        applied: bool,
        replace_existing_files: bool | None = None,
        preserve_modified_ini: bool | None = None,
    ) -> dict[str, object]:
        phase = "apply" if applied else "remove"
        log.info(
            LOG_EVENT_MOD_FILE_OPERATION_STARTED,
            mod_id=mod_id,
            phase=phase,
        )
        try:
            mod, installed_game_path = self._resolve_mod_game(mod_id, game_path)
            if applied:
                conflicting_files = apply_mod_files(
                    mod,
                    installed_game_path,
                    replace_existing_files=replace_existing_files,
                )
                if conflicting_files and replace_existing_files is None:
                    copied_file_count = 0
                elif replace_existing_files is False:
                    copied_file_count = len(mod.files) - len(conflicting_files)
                else:
                    copied_file_count = len(mod.files)
            else:
                conflicting_files = remove_mod_files(
                    mod,
                    installed_game_path,
                    preserve_modified_ini=preserve_modified_ini,
                )
                copied_file_count = 0
        except (OSError, URLError, ValueError):
            log.exception(
                LOG_EVENT_MOD_FILE_OPERATION_FAILED,
                mod_id=mod_id,
                phase=phase,
            )
            raise

        log.info(
            LOG_EVENT_MOD_FILE_OPERATION_COMPLETED,
            mod_id=mod_id,
            phase=phase,
        )
        return {
            "applied": applied,
            "copied_file_count": copied_file_count,
            "conflicting_files": conflicting_files,
        }

    async def get_mod_ini(self, mod_id: str, game_path: str) -> dict[str, object]:
        try:
            mod, installed_game_path = self._resolve_mod_game(mod_id, game_path)
            content, exists = read_mod_ini(mod, installed_game_path)
        except (OSError, ValueError):
            log.exception(
                LOG_EVENT_MOD_FILE_OPERATION_FAILED,
                mod_id=mod_id,
                phase="read-ini",
            )
            raise
        return {
            "ini_file": mod.ini_file,
            "content": content,
            "exists": exists,
        }

    async def get_mod_readme(self, mod_id: str) -> dict[str, str]:
        mod = self._get_mod_definition(mod_id)
        try:
            content = fetch_mod_readme(mod)
        except (OSError, URLError, ValueError):
            log.exception(
                LOG_EVENT_MOD_FILE_OPERATION_FAILED,
                mod_id=mod_id,
                phase="read-readme",
            )
            raise
        return {"title": mod.name, "content": content}

    async def save_mod_ini(
        self,
        mod_id: str,
        game_path: str,
        content: str,
    ) -> dict[str, object]:
        try:
            mod, installed_game_path = self._resolve_mod_game(mod_id, game_path)
            write_mod_ini(mod, installed_game_path, content)
        except (OSError, ValueError):
            log.exception(
                LOG_EVENT_MOD_FILE_OPERATION_FAILED,
                mod_id=mod_id,
                phase="save-ini",
            )
            raise
        return {"saved": True, "ini_file": mod.ini_file}

    async def _main(self) -> None:
        get_store()
        log.info(LOG_EVENT_PLUGIN_STARTED)

    async def _unload(self) -> None:
        log.info(LOG_EVENT_PLUGIN_STOPPED)
