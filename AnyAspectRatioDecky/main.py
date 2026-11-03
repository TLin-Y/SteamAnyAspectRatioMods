import decky


class Plugin:
    async def get_plugin_status(self) -> dict[str, str]:
        decky.logger.info("Plugin status requested")
        return {
            "status": "ready",
            "message": "AnyAspectRatio Decky backend is responding.",
        }

    async def _main(self) -> None:
        decky.logger.info("AnyAspectRatio Decky plugin started")

    async def _unload(self) -> None:
        decky.logger.info("AnyAspectRatio Decky plugin stopped")
