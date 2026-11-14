import asyncio
import sys
from collections.abc import Iterator, Sequence
from contextlib import contextmanager
from dataclasses import asdict, replace
from logging.handlers import RotatingFileHandler
from pathlib import Path

import decky

plugin_dir = str(Path(__file__).resolve().parent)
# Store builds keep the Python packages in py_modules/; search it and the plugin root first.
for import_root in (plugin_dir, str(Path(plugin_dir) / "py_modules")):
    if import_root in sys.path:
        sys.path.remove(import_root)
    sys.path.insert(0, import_root)

from helper.build_flavor import SELF_UPDATE_ENABLED
from logger.plugin_logger import attach_file_handler, clear_file_handler_logs
from scanner.local_archives import scan_local_archives
from scanner.mod_catalog import (
    ModDefinition,
    read_catalog_repository,
    summarize_credits,
)
from scanner.mod_installer import (
    MissingModBackupError,
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
    update_existing_ini_resolution,
    write_mod_ini,
)
from scanner.plugin_updater import (
    PLUGIN_NAME,
    check_for_update,
    install_update,
    read_installed_version,
)
from scanner.supported_game_scanner import (
    SupportedGame,
    scan_supported_games as scan_supported_games_report,
)
from store.dependencies import (
    load_active_catalog,
    refresh_catalog_on_start,
    update_dependencies,
)
from store.pack import download_release_pack, install_pack, pack_operation
from store.plugin_store import (
    DEFAULT_CATALOG_PATH,
    PluginStore,
    Settings,
    validate_settings,
)

log = decky.logger
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


def _update_available(game: SupportedGame, mod: ModDefinition, hashes: dict[str, dict[str, str]]) -> bool:
    applied_hash = hashes.get(game.game_path, {}).get(mod.mod_id) if game.game_path else None
    return bool(
        game.files_present
        and mod.source.kind == "archive"
        and mod.source.sha256
        and applied_hash
        and applied_hash != mod.source.sha256
    )


def _include_tracked_backup_targets(
    game: SupportedGame,
    mod: ModDefinition,
    hashes: dict[str, dict[str, str]],
) -> SupportedGame:
    if (
        game.game_path is None
        or mod.source.sha256 is None
        or hashes.get(game.game_path, {}).get(mod.mod_id) != mod.source.sha256
    ):
        return game

    missing_backups = [
        item
        for item in mod.file_backups
        if (Path(game.game_path) / item.target).is_file()
        and not (Path(game.game_path) / item.backup).is_file()
    ]
    if not missing_backups:
        return game

    targets = {item.target for item in missing_backups}
    present_files = list(dict.fromkeys([*game.files_present, *targets]))
    missing_files = [path for path in game.files_missing if path not in targets]
    file_status = (
        "complete" if not missing_files
        else "partial" if present_files
        else "not_detected"
    )
    return replace(
        game,
        file_status=file_status,
        files_present=present_files,
        files_missing=missing_files,
        mod_files=list(dict.fromkeys([*game.mod_files, *targets])),
    )


@contextmanager
def _logged_failure(message: str, *args: object) -> Iterator[None]:
    """Log file-system and validation errors with a traceback, then let them reach the UI."""
    try:
        yield
    except (OSError, ValueError):
        log.exception(message, *args)
        raise


def _error_result(error: Exception) -> dict[str, object]:
    return _error_result(error)


def _record_outcome(
    store: PluginStore, mod: ModDefinition, game_path: str, applied: bool, conflicts: list[str], awaiting: bool,
) -> None:
    """Persist what an apply/remove changed: applied version, cached scan and saved game match."""
    action = "applied" if applied else "removed"
    complete = not conflicts
    try:
        if mod.source.kind == "archive" and complete:
            step = "the applied version could not be saved"
            store.set_applied_hash(game_path, mod.mod_id, (mod.source.sha256 or None) if applied else None)
        if not awaiting:
            step = "the cached game status could not be cleared"
            store.last_scan_path.unlink(missing_ok=True)
        if not applied and complete:
            step = "the saved game match could not be cleared"
            store.remove_matched_game(mod.mod_id, game_path)
    except (OSError, ValueError) as error:
        raise ValueError(f"Mod files were {action}, but {step}: {error}") from error


def _is_chosen(game: SupportedGame, choices: dict[str, dict[str, str]]) -> bool:
    choice = choices.get(game.ambiguity_group) if game.ambiguity_group else None
    return bool(choice) and choice["mod_id"] == game.mod_id and choice["game_path"] == game.game_path


def _find_mod_logs(files: Sequence[str], name: str, game_path: str | None, warnings: list[str]) -> list[str]:
    if not game_path or not mod_log_names(files):
        return []
    try:
        return existing_mod_logs(files, game_path)
    except (OSError, ValueError) as error:
        log.warning("Could not check mod logs for %s", name)
        warnings.append(f"Could not check logs for {name}: {error}")
        return []


def _load_applied_hashes(store: PluginStore, warnings: list[str]) -> dict[str, dict[str, str]] | None:
    try:
        return store.load_applied_hashes()
    except ValueError as error:
        log.warning("Could not read applied mod versions")
        warnings.append(str(error))
        return None


def _refresh_games(
    games: list[SupportedGame],
    mods: dict[str, ModDefinition],
    hashes: dict[str, dict[str, str]] | None,
    choices: dict[str, dict[str, str]],
    warnings: list[str],
) -> list[SupportedGame]:
    """Recompute what changes without a rescan: update flags, mod logs and saved game choices.

    In an ambiguity group with a saved choice, only the chosen game is kept and it is locked.
    Unknown mods (catalog changed) and unreadable hashes leave the cached update flag alone.
    """
    locked_groups = {game.ambiguity_group for game in games if _is_chosen(game, choices)}
    refreshed = []
    for game in games:
        locked = game.ambiguity_group in locked_groups
        if locked and not _is_chosen(game, choices):
            continue
        mod = mods.get(game.mod_id)
        changes: dict[str, object] = {"match_locked": True} if locked else {}
        if mod is not None and hashes is not None:
            changes["update_available"] = _update_available(game, mod, hashes)
        files = mod.files if mod is not None else game.mod_files
        changes["log_files"] = _find_mod_logs(files, game.mod_name, game.game_path, warnings)
        refreshed.append(replace(game, **changes))
    return refreshed


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
        hashes = _load_applied_hashes(store, report.warnings) or {}
        games = [
            _include_tracked_backup_targets(game, report.mods[game.mod_id], hashes)
            for game in report.supported_games
        ]
        choices = {} if include_all_matches else store.load_matched_games()
        report.supported_games = _refresh_games(games, report.mods, hashes, choices, report.warnings)
        if source.warning:
            log.warning("Cached catalog is unusable; using the bundled catalog")
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
        try:
            games = [SupportedGame.from_dict(game) for game in report["supported_games"]]
            warnings = list(report["warnings"])
        except (KeyError, TypeError, ValueError):
            return None  # Written by another plugin version: treat as no cached scan.
        mods = {mod.mod_id: mod for mod in load_active_catalog(store, store.load_settings()).mods}
        hashes = _load_applied_hashes(store, warnings)
        games = _refresh_games(games, mods, hashes, store.load_matched_games(), warnings)
        report["supported_games"] = [game.to_dict() for game in games]
        report["warnings"] = warnings
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
        with _logged_failure("Clearing the game selection failed: %s", mod_id):
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
            result = await asyncio.to_thread(install_pack, store, store.load_settings(), Path(archive_path))
        except (OSError, ValueError) as error:
            log.exception("Offline pack import failed")
            return _error_result(error)
        warning = await self._refresh_online_catalog(store)
        return {**result, "warning": warning}

    async def download_offline_pack(self) -> dict[str, object]:
        store = get_store()
        try:
            result = await asyncio.to_thread(download_release_pack, store, store.load_settings())
        except (OSError, ValueError) as error:
            log.exception("Offline pack download failed")
            return _error_result(error)
        warning = await self._refresh_online_catalog(store)
        return {**result, "warning": warning}

    @staticmethod
    async def _refresh_online_catalog(store: PluginStore) -> str | None:
        def refresh():
            with pack_operation():
                return refresh_catalog_on_start(store, store.load_settings())

        try:
            await asyncio.to_thread(refresh)
        except (OSError, ValueError) as error:
            log.warning("Catalog refresh failed")
            return f"Could not refresh the online catalog: {error}"
        return None

    async def clear_local_data(self) -> dict[str, object]:
        store = get_store()
        try:
            with pack_operation():
                store.clear_local_data()
                if _file_handler is None:
                    raise ValueError("Plugin log handler is unavailable.")
                clear_file_handler_logs(_file_handler)
        except (OSError, ValueError) as error:
            log.exception("Clearing local data failed")
            return _error_result(error)
        return {}

    async def get_settings(self) -> dict[str, object]:
        store = get_store()
        settings = store.load_settings()
        defaults = Settings()
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
                "ini_resolution_width": defaults.ini_resolution_width,
                "ini_resolution_height": defaults.ini_resolution_height,
            },
            "catalog": {
                "origin": source.origin,
                "mod_count": len(source.mods),
                "fetched_at": meta.get("fetched_at") if meta else None,
                "warning": source.warning,
            },
            "local_archives": {"count": len(archives), "warnings": warnings},
            "self_update_enabled": SELF_UPDATE_ENABLED,
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
        ini_resolution_width: str,
        ini_resolution_height: str,
    ) -> dict[str, object]:
        store = get_store()
        try:
            if not all(
                isinstance(value, str) and value.isascii() and value.isdecimal()
                for value in (ini_resolution_width, ini_resolution_height)
            ):
                raise ValueError("INI resolution dimensions must be whole numbers")
            requested = validate_settings(
                Settings(
                    catalog_repository_url,
                    catalog_ref,
                    catalog_path,
                    local_archive_dir,
                    ini_resolution_width=int(ini_resolution_width),
                    ini_resolution_height=int(ini_resolution_height),
                )
            )
        except ValueError:
            log.warning("Settings rejected")
            raise

        default_url, default_ref = read_catalog_repository()

        def unless_default(value: str, default: str) -> str:
            return "" if value == default else value  # "" means "use the shipped default"

        with pack_operation():
            previous = store.load_settings()
            settings = replace(
                requested,
                catalog_repository_url=unless_default(requested.catalog_repository_url, default_url),
                catalog_ref=unless_default(requested.catalog_ref, default_ref),
                catalog_path=unless_default(requested.catalog_path, DEFAULT_CATALOG_PATH),
                offline_mode=previous.offline_mode,
                ini_resolution_update_pending=(
                    previous.ini_resolution_update_pending
                    or (requested.ini_resolution_width, requested.ini_resolution_height)
                    != (previous.ini_resolution_width, previous.ini_resolution_height)
                ),
            )
            store.save_settings(settings)
            source_changed = previous.source_key() != settings.source_key()
            if source_changed:
                store.clear_catalog_cache()
                store.clear_archive_cache()
                store.clear_pack()
            if previous.local_archive_dir != settings.local_archive_dir:
                store.local_index_path.unlink(missing_ok=True)
        log.info("Settings saved (mod source changed: %s)", source_changed)
        return {"saved": True, "source_changed": source_changed}

    async def get_offline_mode(self) -> bool:
        return get_store().load_settings().offline_mode

    async def get_disk_usage(self) -> int:
        return await asyncio.to_thread(get_store().disk_usage)

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
        log.info("Offline mode %s", "on" if enabled else "off")
        warning = None if enabled else await self._refresh_online_catalog(store)
        return {"offline_mode": bool(enabled), "warning": warning}

    async def update_dependencies(self) -> dict[str, object]:
        store = get_store()
        settings = store.load_settings()
        log.info("Catalog update started")
        try:
            with pack_operation():
                result = update_dependencies(store, settings)
        except NetworkError as error:
            log.warning("Catalog update failed: network error")
            return {"error": str(error)}
        except (OSError, ValueError) as error:
            log.exception("Catalog update failed")
            return _error_result(error)
        store.local_index_path.unlink(missing_ok=True)
        archives, warnings = self._scan_local_archives(store, settings)
        log.info(
            "Catalog update completed: %s mods, changed: %s",
            result["mod_count"], result["catalog_changed"],
        )
        return {**result, "local_archive_count": len(archives), "warnings": warnings}

    async def scan_supported_games(self) -> dict[str, object]:
        log.info("Scan started")
        with _logged_failure("Scan failed"):
            report = self._scan()
        for warning in report.warnings:
            if warning.startswith("Steam library is unavailable:"):
                log.warning(warning)
        log.info(
            "Scan completed: %s libraries, %s apps, %s supported",
            report.steam_scan.libraries_scanned,
            report.steam_scan.apps_scanned,
            len(report.supported_games),
        )
        result = report.to_dict()
        try:
            get_store().save_last_scan(result)
        except OSError:
            log.warning("Could not save the scan result")
        return result

    async def update_current_resolution(self) -> dict[str, object]:
        log.info("Resolution update started")
        with _logged_failure("Resolution update failed"):
            report = self._scan()
            settings = get_store().load_settings()

        candidates: dict[tuple[str, str], list[SupportedGame]] = {}
        skipped: list[str] = []
        for game in report.supported_games:
            mod = report.mods[game.mod_id]
            if not game.game_path or game.manual:
                continue
            if not mod.ini_resolution:
                if game.ini_path and game.file_status == "complete":
                    skipped.append(f"{game.app_name}: no catalogued resolution fields.")
                continue
            if game.ambiguity_group and not game.match_locked:
                skipped.append(f"{game.app_name}: select the correct mod first.")
                continue
            if not game.ini_path:
                continue
            if game.file_status != "complete":
                skipped.append(f"{game.app_name}: mod files are incomplete.")
                continue
            key = (str(Path(game.game_path).resolve()), mod.ini_file or "")
            candidates.setdefault(key, []).append(game)

        updated: list[str] = []
        unchanged: list[str] = []
        failures: list[str] = []
        warnings = list(report.warnings)
        for games in candidates.values():
            if len(games) != 1:
                skipped.extend(f"{game.app_name}: INI is shared by multiple matches." for game in games)
                continue
            game = games[0]
            mod = report.mods[game.mod_id]
            if game.game_path is None:
                continue
            try:
                changed, permissions_preserved = update_existing_ini_resolution(
                    mod, game.game_path, settings.ini_resolution_width, settings.ini_resolution_height
                )
            except (OSError, ValueError) as error:
                log.exception("Resolution update failed: %s", mod.mod_id)
                failures.append(f"{game.app_name}: {error}")
                continue
            if changed:
                updated.append(game.app_name)
                if not permissions_preserved:
                    log.warning("Could not keep INI file permissions: %s", mod.mod_id)
            else:
                unchanged.append(game.app_name)
        with pack_operation():
            store = get_store()
            latest = store.load_settings()
            if (
                not failures
                and latest.ini_resolution_update_pending
                and (latest.ini_resolution_width, latest.ini_resolution_height)
                == (settings.ini_resolution_width, settings.ini_resolution_height)
            ):
                latest = replace(latest, ini_resolution_update_pending=False)
                store.save_settings(latest)
        log.info("Resolution update completed")
        return {
            "updated": updated, "unchanged": unchanged, "skipped": skipped,
            "failures": failures, "warnings": warnings,
            "resolution_update_pending": latest.ini_resolution_update_pending,
        }

    async def set_mod_applied(
        self,
        mod_id: str,
        game_path: str,
        applied: bool,
        replace_existing_files: bool | None = None,
        preserve_modified_ini: bool | None = None,
    ) -> dict[str, object]:
        """Apply or remove a mod's files.

        Conflicting files are returned for confirmation first; the UI calls again with
        replace_existing_files (apply) or preserve_modified_ini (remove) set.
        """
        phase = "apply" if applied else "remove"
        log.info("Mod %s started: %s", phase, mod_id)
        ini_warnings: list[str] = []
        try:
            mod, path = self._resolve_mod_game(mod_id, game_path)
            store = get_store()
            settings = store.load_settings()
            resolution = (settings.ini_resolution_width, settings.ini_resolution_height)
            if applied:
                conflicts = apply_mod_files(
                    mod, path,
                    replace_existing_files=replace_existing_files,
                    ini_resolution=resolution,
                    ini_adjustment_warnings=ini_warnings,
                )
                awaiting = bool(conflicts) and replace_existing_files is None
                kept = sum(file in mod.files for file in conflicts) if replace_existing_files is False else 0
                copied = 0 if awaiting else len(mod.files) - kept
            else:
                try:
                    conflicts = remove_mod_files(
                        mod, path, preserve_modified_ini=preserve_modified_ini, ini_resolution=resolution
                    )
                except MissingModBackupError as error:
                    store.set_applied_hash(path, mod.mod_id, None)
                    store.last_scan_path.unlink(missing_ok=True)
                    log.warning("Mod %s failed, backup missing: %s", phase, mod_id)
                    return {"applied": True, "copied_file_count": 0, "conflicting_files": [], "warning": str(error)}
                awaiting = bool(conflicts) and preserve_modified_ini is None
                copied = 0
            _record_outcome(store, mod, path, applied, conflicts, awaiting)
        except NetworkError as error:
            log.warning("Mod %s failed, network error: %s", phase, mod_id)
            return {"error": str(error)}
        except (OSError, ValueError):
            log.exception("Mod %s failed: %s", phase, mod_id)
            raise

        log.info("Mod %s completed: %s", phase, mod_id)
        result: dict[str, object] = {"applied": applied, "copied_file_count": copied, "conflicting_files": conflicts}
        if ini_warnings:
            log.warning("Skipped INI resolution adjustment: %s", mod_id)
            result["ini_adjustment_warning"] = " ".join(ini_warnings)
        return result

    async def get_mod_ini(self, mod_id: str, game_path: str) -> dict[str, object]:
        with _logged_failure("Reading mod INI failed: %s", mod_id):
            mod, installed_game_path = self._resolve_mod_game(mod_id, game_path)
            content, exists = read_mod_ini(mod, installed_game_path)
        return {"ini_file": mod.ini_file, "content": content, "exists": exists}

    async def get_mod_log(self, mod_id: str, game_path: str, name: str) -> dict[str, object]:
        with _logged_failure("Reading mod log failed: %s", mod_id):
            mod, installed_game_path = self._resolve_mod_game(mod_id, game_path)
            content, truncated = read_mod_log(mod, installed_game_path, name)
        return {"name": name, "content": content, "truncated": truncated}

    async def get_mod_readme(self, mod_id: str) -> dict[str, str]:
        mod = self._get_mod_definition(mod_id)
        try:
            content = fetch_mod_readme(mod)
        except NetworkError as error:
            log.warning("Mod README download failed: %s", mod_id)
            return {"title": mod.name, "content": f"**Could not load the ReadMe.**\n\n{error}"}
        except (OSError, ValueError):
            log.exception("Reading mod README failed: %s", mod_id)
            raise
        return {"title": mod.name, "content": content}

    async def save_mod_ini(self, mod_id: str, game_path: str, content: str) -> dict[str, object]:
        with _logged_failure("Saving mod INI failed: %s", mod_id):
            mod, installed_game_path = self._resolve_mod_game(mod_id, game_path)
            permissions_preserved = write_mod_ini(mod, installed_game_path, content)
        if not permissions_preserved:
            log.warning("Could not keep INI file permissions: %s", mod_id)
        return {"saved": True, "ini_file": mod.ini_file, "permissions_preserved": permissions_preserved}

    @staticmethod
    def _update_repository() -> str:
        configured = get_store().load_settings().catalog_repository_url
        return configured or read_catalog_repository()[0]

    async def check_plugin_update(self) -> dict[str, object]:
        if not SELF_UPDATE_ENABLED:
            return {"error": "Plugin updates are delivered through the Decky Plugin Store."}
        installed = read_installed_version(Path(plugin_dir))
        try:
            return await asyncio.to_thread(
                check_for_update, self._update_repository(), installed
            )
        except (OSError, ValueError) as error:
            log.warning("Plugin update check failed")
            return _error_result(error)

    async def install_plugin_update(self) -> dict[str, object]:
        if not SELF_UPDATE_ENABLED:
            return {"error": "Plugin updates are delivered through the Decky Plugin Store."}
        log.info("Plugin update started")
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
            log.warning("Plugin update failed")
            return _error_result(error)
        log.info("Plugin update completed")
        return result

    async def _main(self) -> None:
        store = get_store()
        log.info("Plugin started")
        self._startup_refresh = asyncio.create_task(self._refresh_catalog(store))

    @staticmethod
    async def _refresh_catalog(store: PluginStore) -> None:
        """Fetch the newest catalog in the background; the bundled or cached one stays in use on failure."""
        log.info("Startup catalog refresh started")

        def refresh():
            with pack_operation():
                return refresh_catalog_on_start(store, store.load_settings())

        try:
            changed = await asyncio.to_thread(refresh)
        except Exception:
            log.warning("Startup catalog refresh failed")
            return
        log.info("Startup catalog refresh completed (changed: %s)", changed)

    async def _unload(self) -> None:
        if task := getattr(self, "_startup_refresh", None):
            task.cancel()
        log.info("Plugin stopped")

    async def _uninstall(self) -> None:
        """Free the disk space of downloaded zips and caches; settings, logs and applied fixes stay."""
        get_store().clear_local_data()
        log.info("Plugin uninstalled, local caches deleted")
