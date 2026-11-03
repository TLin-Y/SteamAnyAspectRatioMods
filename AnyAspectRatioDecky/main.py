import sys
from pathlib import Path

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
    LOG_EVENT_STEAM_SCAN_COMPLETED,
    LOG_EVENT_STEAM_SCAN_STARTED,
    LOG_SCAN_PHASE_FORMAT,
)
from logger.plugin_logger import PluginLogger
from scanner import discover_steam_apps


log = PluginLogger(decky.logger)


class Plugin:
    async def get_plugin_status(self) -> dict[str, str]:
        log.info(LOG_EVENT_BACKEND_STATUS_REQUESTED)
        return {
            BACKEND_STATUS_FIELD: BACKEND_STATUS_READY,
            BACKEND_MESSAGE_FIELD: BACKEND_READY_MESSAGE,
        }

    async def scan_steam_libraries(self) -> dict[str, object]:
        log.info(LOG_EVENT_STEAM_SCAN_STARTED)
        report = discover_steam_apps()
        log.info(
            LOG_EVENT_STEAM_SCAN_COMPLETED,
            phase=LOG_SCAN_PHASE_FORMAT.format(
                libraries=report.libraries_scanned,
                apps=report.apps_scanned,
            ),
        )
        return report.to_dict()

    async def _main(self) -> None:
        log.info(LOG_EVENT_PLUGIN_STARTED)

    async def _unload(self) -> None:
        log.info(LOG_EVENT_PLUGIN_STOPPED)
