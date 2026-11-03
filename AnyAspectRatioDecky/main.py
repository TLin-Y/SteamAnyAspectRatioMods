import sys
from pathlib import Path

import decky

plugin_dir = str(Path(__file__).resolve().parent)
if plugin_dir not in sys.path:
    sys.path.insert(0, plugin_dir)

from plugin_logger import PluginLogger


log = PluginLogger(decky.logger)


class Plugin:
    async def get_plugin_status(self) -> dict[str, str]:
        log.info("backend.status_requested")
        return {
            "status": "ready",
            "message": "AnyAspectRatio Decky backend is responding.",
        }

    async def _main(self) -> None:
        log.info("plugin.started")

    async def _unload(self) -> None:
        log.info("plugin.stopped")
