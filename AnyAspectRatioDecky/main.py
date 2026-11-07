import asyncio
import sys
from dataclasses import asdict, replace
from logging.handlers import RotatingFileHandler
from pathlib import Path

import decky

plugin_dir = str(Path(__file__).resolve().parent)
if plugin_dir not in sys.path:
    sys.path.insert(0, plugin_dir)

from helper.constants import (
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
    MOD_SOURCE_ARCHIVE,
    STEAM_LIBRARY_UNAVAILABLE_WARNING,
)
from logger.plugin_logger import PluginLogger, attach_file_handler, clear_file_handler_logs
from scanner import scan_supported_games as scan_supported_games_report
from scanner.local_archives import scan_local_archives
from scanner.plugin_updater import (
    PLUGIN_NAME,
    check_for_update,
    install_update,
    read_installed_version,
)
from scanner.mod_catalog import ModDefinition, read_catalog_repository, summarize_credits
from scanner.mod_installer import (
    NetworkError,
    apply_mod_files,
    existing_mod_logs,
    fetch_mod_readme,
    mod_log_names,
    read_mod_ini,
    read_mod_log,
    remove_mod_files,
    set_archive_cache_dir,
    set_offline_mode as block_network,
    set_pack_dir,
    write_mod_ini,
)
from store import (
    DEFAULT_CATALOG_PATH,
    PluginStore,
    Settings,
    download_release_pack,
    install_pack,
    load_active_catalog,
    pack_operation,
    refresh_catalog_on_start,
    update_dependencies,
    validate_settings,
)


log = PluginLogger(decky.logger)
_store: PluginStore | None = None
_file_handler: RotatingFileHandler | None = None


def get_store() -> PluginStore:
    """Open the persistent data directory once and wire the cache and file log to it."""
    global _store, _file_handler
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
        block_network(store.load_settings().offline_mode)
        _file_handler = attach_file_handler(decky.logger, store.log_path)
        _store = store
    return _store


def _update_available(
    game_path: str | None,
    files_present: list[str],
    mod: ModDefinition,
    hashes: dict[str, dict[str, str]],
) -> bool:
    applied_hash = hashes.get(game_path, {}).get(mod.mod_id) if game_path else None
    return bool(
        files_present
        and mod.source.kind == MOD_SOURCE_ARCHIVE
        and mod.source.sha256
        and applied_hash
        and applied_hash != mod.source.sha256
    )


def _selected_groups(
    candidates: list[tuple[object, object, object]],
    choices: dict[str, dict[str, str]],
) -> set[str]:
    return {
        group for group, mod_id, game_path in candidates
        if isinstance(group, str) and group
        and (choice := choices.get(group))
        and choice["mod_id"] == mod_id and choice["game_path"] == game_path
    }


def _matches_selection(
    group: object, mod_id: object, game_path: object,
    groups: set[str], choices: dict[str, dict[str, str]],
) -> bool:
    if not isinstance(group, str) or group not in groups:
        return True
    choice = choices[group]
    return choice["mod_id"] == mod_id and choice["game_path"] == game_path


def _find_mod_logs(
    files: tuple[str, ...] | list[str], name: str, game_path: str | None, warnings: list[str],
) -> list[str]:
    if not game_path or not mod_log_names(files):
        return []
    try:
        return existing_mod_logs(files, game_path)
    except (OSError, ValueError) as error:
        log.warning(LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED, phase="mod-logs")
        warnings.append(f"Could not check logs for {name}: {error}")
        return []


class Plugin:
    @staticmethod
    def _scan(*, include_all_matches: bool = False):
        store = get_store()
        settings = store.load_settings()
        source = load_active_catalog(store, settings)
        report = scan_supported_games_report(
            source.mods,
            local_archive_dir=settings.local_archive_dir or None,
            local_index_path=store.local_index_path,
        )
        try:
            hashes = store.load_applied_hashes()
        except ValueError as error:
            log.warning(LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED, phase="applied-hashes")
            report.warnings.append(str(error))
            hashes = {}
        report.supported_games = [
            replace(
                game,
                update_available=_update_available(
                    game.game_path, game.files_present, report.mods[game.mod_id], hashes
                ),
                log_files=_find_mod_logs(
                    report.mods[game.mod_id].files, game.mod_name, game.game_path, report.warnings,
                ),
            )
            for game in report.supported_games
        ]
        if not include_all_matches:
            choices = store.load_matched_games()
            groups = _selected_groups(
                [(game.ambiguity_group, game.mod_id, game.game_path) for game in report.supported_games],
                choices,
            )
            report.supported_games = [
                replace(game, match_locked=True)
                if game.ambiguity_group in groups else game
                for game in report.supported_games
                if _matches_selection(game.ambiguity_group, game.mod_id, game.game_path, groups, choices)
            ]
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

    async def get_cached_scan(self) -> dict[str, object] | None:
        store = get_store()
        cached = store.load_last_scan()
        if cached is None:
            return None
        report = cached["report"]
        games = report.get("supported_games") if isinstance(report, dict) else None
        if not isinstance(games, list):
            return None
        choices = store.load_matched_games()
        selected_groups = _selected_groups(
            [(game.get("ambiguity_group"), game.get("mod_id"), game.get("game_path"))
             for game in games if isinstance(game, dict)],
            choices,
        )
        report["supported_games"] = games = [
            game for game in games
            if not isinstance(game, dict)
            or _matches_selection(
                game.get("ambiguity_group"), game.get("mod_id"),
                game.get("game_path"), selected_groups, choices,
            )
        ]
        for game in games:
            if isinstance(game, dict) and isinstance(game.get("ambiguity_group"), str) and game["ambiguity_group"] in selected_groups:
                game["match_locked"] = True
        source = load_active_catalog(store, store.load_settings())
        mods = {mod.mod_id: mod for mod in source.mods}
        hashes = None
        try:
            hashes = store.load_applied_hashes()
        except ValueError as error:
            log.warning(LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED, phase="applied-hashes")
            warnings = report.get("warnings")
            if isinstance(warnings, list):
                warnings.append(str(error))

        for game in games:
            if not isinstance(game, dict):
                continue
            mod_id, game_path, files_present = (
                game.get("mod_id"), game.get("game_path"), game.get("files_present")
            )
            if (
                isinstance(mod_id, str)
                and mod_id in mods
                and (game_path is None or isinstance(game_path, str))
                and isinstance(files_present, list)
            ):
                if hashes is not None:
                    game["update_available"] = _update_available(
                        game_path, files_present, mods[mod_id], hashes
                    )
            warnings = report.get("warnings")
            mod_files = list(mods[mod_id].files) if isinstance(mod_id, str) and mod_id in mods else game.get("mod_files")
            mod_name = game.get("mod_name")
            if (
                isinstance(warnings, list)
                and isinstance(mod_name, str)
                and (game_path is None or isinstance(game_path, str))
                and isinstance(mod_files, list)
                and all(isinstance(path, str) for path in mod_files)
            ):
                game["log_files"] = _find_mod_logs(mod_files, mod_name, game_path, warnings)
        return cached

    async def select_matched_game(self, group: str, mod_id: str, game_path: str) -> dict[str, object]:
        report = self._scan(include_all_matches=True)
        if not any(
            game.ambiguity_group == group and game.mod_id == mod_id and game.game_path == game_path
            for game in report.supported_games
        ):
            raise ValueError("Selected game is no longer available. Scan again and retry.")
        store = get_store()
        store.set_matched_game(group, mod_id, game_path)
        store.last_scan_path.unlink(missing_ok=True)
        return {"selected": True}

    async def clear_matched_game(self, group: str, mod_id: str, game_path: str) -> dict[str, object]:
        try:
            report = self._scan()
            game = next(
                (game for game in report.supported_games
                 if game.ambiguity_group == group and game.mod_id == mod_id and game.game_path == game_path),
                None,
            )
            if game is None or not game.match_locked:
                raise ValueError("Saved game selection has changed. Scan again and retry.")
            if not game.manual and game.files_present:
                raise ValueError("Remove the mod files before changing the game selection.")
            store = get_store()
            store.last_scan_path.unlink(missing_ok=True)
            store.clear_matched_game(group, mod_id, game_path)
        except (OSError, ValueError):
            log.exception(LOG_EVENT_MOD_FILE_OPERATION_FAILED, mod_id=mod_id, phase="clear-match")
            raise
        return {"cleared": True}

    async def get_credits(self) -> dict[str, object]:
        store = get_store()
        source = load_active_catalog(store, store.load_settings())
        return {
            "mod_count": len(source.mods),
            "offline_mode": store.load_settings().offline_mode,
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

    async def download_offline_pack(self) -> dict[str, object]:
        store = get_store()
        try:
            return await asyncio.to_thread(download_release_pack, store, store.load_settings())
        except (OSError, ValueError) as error:
            log.exception(LOG_EVENT_DEPENDENCY_UPDATE_FAILED, phase="pack-download")
            return {"error": str(error) or type(error).__name__}

    async def clear_local_data(self) -> dict[str, object]:
        store = get_store()
        try:
            with pack_operation():
                store.clear_local_data()
                if _file_handler is None:
                    raise ValueError("Plugin log handler is unavailable.")
                clear_file_handler_logs(_file_handler)
        except (OSError, ValueError) as error:
            log.exception(LOG_EVENT_DEPENDENCY_UPDATE_FAILED, phase="clear-local-data")
            return {"error": str(error) or type(error).__name__}
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
        with pack_operation():
            previous = store.load_settings()
            settings = Settings(
                "" if requested.catalog_repository_url == default_url else requested.catalog_repository_url,
                "" if requested.catalog_ref == default_ref else requested.catalog_ref,
                "" if requested.catalog_path == DEFAULT_CATALOG_PATH else requested.catalog_path,
                requested.local_archive_dir,
                previous.offline_mode,
            )
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

    async def get_offline_mode(self) -> bool:
        return get_store().load_settings().offline_mode

    async def get_log(self, max_lines: int = 300) -> dict[str, object]:
        path = get_store().log_path
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except FileNotFoundError:
            return {"content": "", "path": str(path)}
        except OSError as error:
            return {"error": str(error)}
        return {"content": "\n".join(lines[-max(1, min(max_lines, 2000)):]), "path": str(path)}

    async def set_offline_mode(self, enabled: bool) -> dict[str, object]:
        store = get_store()
        store.save_settings(replace(store.load_settings(), offline_mode=bool(enabled)))
        block_network(bool(enabled))
        log.info(LOG_EVENT_SETTINGS_SAVED, phase=f"offline-mode-{bool(enabled)}")
        return {"offline_mode": bool(enabled)}

    async def update_dependencies(self) -> dict[str, object]:
        store = get_store()
        settings = store.load_settings()
        log.info(LOG_EVENT_DEPENDENCY_UPDATE_STARTED)
        try:
            with pack_operation():
                result = update_dependencies(store, settings)
        except NetworkError as error:
            log.warning(LOG_EVENT_DEPENDENCY_UPDATE_FAILED, phase="network")
            return {"error": str(error)}
        except (OSError, ValueError) as error:
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
            if mod.source.kind == MOD_SOURCE_ARCHIVE:
                complete_apply = applied and not conflicting_files
                complete_remove = not applied and not conflicting_files
                if complete_apply or complete_remove:
                    try:
                        get_store().set_applied_hash(
                            installed_game_path, mod.mod_id,
                            mod.source.sha256 if complete_apply and mod.source.sha256 else None,
                        )
                    except (OSError, ValueError) as error:
                        raise ValueError(
                            f"Mod files were {'applied' if applied else 'removed'}, "
                            f"but the applied version could not be saved: {error}"
                        ) from error
            awaiting_confirmation = bool(conflicting_files) and (
                replace_existing_files is None if applied else preserve_modified_ini is None
            )
            if not awaiting_confirmation:
                try:
                    get_store().last_scan_path.unlink(missing_ok=True)
                except OSError as error:
                    raise ValueError(
                        f"Mod files were {'applied' if applied else 'removed'}, "
                        f"but the cached game status could not be cleared: {error}"
                    ) from error
            if not applied and not conflicting_files:
                try:
                    get_store().remove_matched_game(mod.mod_id, installed_game_path)
                except (OSError, ValueError) as error:
                    raise ValueError(
                        f"Mod files were removed, but the saved game match could not be cleared: {error}"
                    ) from error
        except NetworkError as error:
            log.warning(
                LOG_EVENT_MOD_FILE_OPERATION_FAILED,
                mod_id=mod_id,
                phase=f"{phase}-network",
            )
            return {"error": str(error)}
        except (OSError, ValueError):
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

    async def get_mod_log(self, mod_id: str, game_path: str, name: str) -> dict[str, object]:
        try:
            mod, installed_game_path = self._resolve_mod_game(mod_id, game_path)
            content, truncated = read_mod_log(mod, installed_game_path, name)
        except (OSError, ValueError):
            log.exception(LOG_EVENT_MOD_FILE_OPERATION_FAILED, mod_id=mod_id, phase="read-mod-log")
            raise
        return {"name": name, "content": content, "truncated": truncated}

    async def get_mod_readme(self, mod_id: str) -> dict[str, str]:
        mod = self._get_mod_definition(mod_id)
        try:
            content = fetch_mod_readme(mod)
        except NetworkError as error:
            log.warning(
                LOG_EVENT_MOD_FILE_OPERATION_FAILED,
                mod_id=mod_id,
                phase="read-readme-network",
            )
            return {"title": mod.name, "content": f"**Could not load the ReadMe.**\n\n{error}"}
        except (OSError, ValueError):
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

    @staticmethod
    def _update_repository() -> str:
        configured = get_store().load_settings().catalog_repository_url
        return configured or read_catalog_repository()[0]

    async def check_plugin_update(self) -> dict[str, object]:
        installed = read_installed_version(Path(plugin_dir))
        try:
            return await asyncio.to_thread(
                check_for_update, self._update_repository(), installed
            )
        except (OSError, ValueError) as error:
            log.warning(LOG_EVENT_DEPENDENCY_UPDATE_FAILED, phase="plugin-check")
            return {"error": str(error) or type(error).__name__}

    async def install_plugin_update(self) -> dict[str, object]:
        log.info(LOG_EVENT_DEPENDENCY_UPDATE_STARTED, phase="plugin-install")
        repository = self._update_repository()
        try:
            info = await asyncio.to_thread(
                check_for_update, repository, read_installed_version(Path(plugin_dir))
            )
            if not info["update_available"]:
                return {"error": "The plugin is already up to date."}
            if not info["download_url"]:
                raise ValueError(
                    f"Release {info['latest_version']} has no {PLUGIN_NAME}-<version>.zip file attached"
                )
            result = await asyncio.to_thread(
                install_update,
                repository,
                str(info["download_url"]),
                Path(plugin_dir),
                Path.home() / "Downloads",
            )
        except (OSError, ValueError) as error:
            log.warning(LOG_EVENT_DEPENDENCY_UPDATE_FAILED, phase="plugin-install")
            return {"error": str(error) or type(error).__name__}
        log.info(LOG_EVENT_DEPENDENCY_UPDATE_COMPLETED, phase="plugin-install")
        return result

    async def _main(self) -> None:
        store = get_store()
        log.info(LOG_EVENT_PLUGIN_STARTED)
        self._startup_refresh = asyncio.create_task(self._refresh_catalog(store))

    @staticmethod
    async def _refresh_catalog(store: PluginStore) -> None:
        """Fetch the newest catalog in the background; the bundled or cached one stays in use on failure."""
        log.info(LOG_EVENT_DEPENDENCY_UPDATE_STARTED, phase="startup")

        def refresh():
            with pack_operation():
                return refresh_catalog_on_start(store, store.load_settings())

        try:
            changed = await asyncio.to_thread(refresh)
        except Exception:
            log.warning(LOG_EVENT_DEPENDENCY_UPDATE_FAILED, phase="startup")
            return
        log.info(LOG_EVENT_DEPENDENCY_UPDATE_COMPLETED, phase=f"startup-changed-{changed}")

    async def _unload(self) -> None:
        log.info(LOG_EVENT_PLUGIN_STOPPED)
