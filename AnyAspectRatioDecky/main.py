import sys
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
    LOG_EVENT_PLUGIN_STARTED,
    LOG_EVENT_PLUGIN_STOPPED,
    LOG_EVENT_MOD_FILE_OPERATION_COMPLETED,
    LOG_EVENT_MOD_FILE_OPERATION_FAILED,
    LOG_EVENT_MOD_FILE_OPERATION_STARTED,
    LOG_EVENT_SUPPORTED_GAME_SCAN_COMPLETED,
    LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED,
    LOG_EVENT_SUPPORTED_GAME_SCAN_STARTED,
    LOG_SCAN_PHASE_FORMAT,
)
from logger.plugin_logger import PluginLogger
from scanner import scan_supported_games as scan_supported_games_report
from scanner.mod_catalog import ModDefinition, load_mod_catalog
from scanner.mod_installer import (
    apply_mod_files,
    fetch_mod_readme,
    read_mod_ini,
    remove_mod_files,
    write_mod_ini,
)


log = PluginLogger(decky.logger)


class Plugin:
    @staticmethod
    def _get_mod_definition(mod_id: str) -> ModDefinition:
        mod = next(
            (candidate for candidate in load_mod_catalog() if candidate.mod_id == mod_id),
            None,
        )
        if mod is None:
            raise ValueError("Mod is not present in the catalog")
        return mod

    def _resolve_mod_game(
        self,
        mod_id: str,
        game_path: str,
    ) -> tuple[ModDefinition, str]:
        mod = self._get_mod_definition(mod_id)

        game = next(
            (
                candidate
                for candidate in scan_supported_games_report().supported_games
                if candidate.mod_id == mod_id
                and candidate.game_path == game_path
            ),
            None,
        )
        if game is None or game.game_path is None:
            raise ValueError("Game installation is no longer available")
        return mod, game.game_path

    async def get_plugin_status(self) -> dict[str, str]:
        log.info(LOG_EVENT_BACKEND_STATUS_REQUESTED)
        return {
            BACKEND_STATUS_FIELD: BACKEND_STATUS_READY,
            BACKEND_MESSAGE_FIELD: BACKEND_READY_MESSAGE,
        }

    async def scan_supported_games(self) -> dict[str, object]:
        log.info(LOG_EVENT_SUPPORTED_GAME_SCAN_STARTED)
        try:
            report = scan_supported_games_report()
        except (OSError, ValueError):
            log.exception(LOG_EVENT_SUPPORTED_GAME_SCAN_FAILED)
            raise
        log.info(
            LOG_EVENT_SUPPORTED_GAME_SCAN_COMPLETED,
            phase=LOG_SCAN_PHASE_FORMAT.format(
                libraries=report.steam_scan.libraries_scanned,
                apps=report.steam_scan.apps_scanned,
                supported=len(report.supported_games),
            ),
        )
        return report.to_dict()

    async def set_mod_applied(
        self,
        mod_id: str,
        game_path: str,
        applied: bool,
        replace_existing_files: bool | None = None,
    ) -> dict[str, object]:
        mod = self._get_mod_definition(mod_id)

        phase = "apply" if applied else "remove"
        log.info(
            LOG_EVENT_MOD_FILE_OPERATION_STARTED,
            mod_id=mod_id,
            phase=phase,
        )
        try:
            report = scan_supported_games_report()
            game = next(
                (
                    candidate
                    for candidate in report.supported_games
                    if candidate.mod_id == mod_id
                    and candidate.game_path == game_path
                ),
                None,
            )
            if game is None or game.game_path is None:
                raise ValueError("Game installation is no longer available")

            if applied:
                conflicting_files = apply_mod_files(
                    mod,
                    game.game_path,
                    replace_existing_files=replace_existing_files,
                )
            else:
                remove_mod_files(mod, game.game_path)
                conflicting_files = []
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
            "file_count": len(mod.files),
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
        log.info(LOG_EVENT_PLUGIN_STARTED)

    async def _unload(self) -> None:
        log.info(LOG_EVENT_PLUGIN_STOPPED)
