import asyncio
import importlib
import logging
import sys
import types
from collections.abc import Callable, Generator
from contextlib import ExitStack, contextmanager
from functools import cache
from pathlib import Path
from unittest.mock import patch

from logger.plugin_logger import PluginLogger
from scanner.mod_catalog import ModDefinition
from scanner.steam_library_scanner import InstalledApp, SteamScanReport
from scanner.supported_game_scanner import SupportedGameScanReport, match_supported_games
from store.dependencies import CatalogSource
from support.packs import PackTest

LOGGER_NAME = "plugin_tests"


def file_logger(stack: ExitStack, name: str, path: Path) -> PluginLogger:
    """A plugin logger writing to path, closed when stack closes."""
    plugin_log = PluginLogger()
    plugin_log.bind(logging.Logger(name, level=logging.INFO))
    plugin_log.attach_file(path)
    stack.callback(plugin_log.close)
    return plugin_log


@cache
def plugin_module():
    """main.py, imported once with a stand-in for Decky's `decky` module (whose logger it binds on import)."""
    decky = types.ModuleType("decky")
    decky.logger = logging.getLogger(LOGGER_NAME)
    with patch.dict(sys.modules, {"decky": decky}):
        return importlib.import_module("main")


class PluginTest(PackTest):
    """self.plugin is the Decky entry point, working on self.store and a game in self.game."""

    def setUp(self) -> None:
        super().setUp()
        self.main = plugin_module()
        self.plugin = self.main.Plugin()
        self.game = self.folder("game")
        self.workdir = self.game
        stores = patch.object(self.main, "get_store", return_value=self.store)
        stores.start()
        self.addCleanup(stores.stop)

    def call(self, method: str, *args: object, **kwargs: object):
        """Run one of the plugin's async RPC methods the way Decky does."""
        return asyncio.run(getattr(self.plugin, method)(*args, **kwargs))

    def report(self, *mods: ModDefinition, apps: list[InstalledApp] = (), shortcuts=()) -> SupportedGameScanReport:
        return match_supported_games(SteamScanReport(1, 1, list(apps), [], list(shortcuts)), mods)

    @contextmanager
    def scanning(self, report: SupportedGameScanReport) -> Generator[None, None, None]:
        """The plugin's scan returns this report."""
        with patch.object(self.main.Plugin, "_scan", return_value=report):
            yield

    @contextmanager
    def catalog(self, mods: Callable[[], tuple[ModDefinition, ...]],
                report: Callable[[], SupportedGameScanReport]) -> Generator[None, None, None]:
        """The active catalog and the Steam scan are computed on every call, so tests can change them in between."""
        with (
            patch.object(self.main, "load_active_catalog", side_effect=lambda *_: CatalogSource("test", tuple(mods()))),
            patch.object(self.main, "scan_supported_games_report", side_effect=lambda *_, **__: report()),
        ):
            yield
