import asyncio
import importlib
import logging
import os
import re
import sys
import threading
import types
from pathlib import Path

PLUGIN_NAME = "AnyAspectRatio"

# The frontend's callables, minus the Decky self-updater (desktop releases update separately).
EXPOSED_METHODS = (
    "add_custom_game_dir",
    "remove_custom_game_dir",
    "clear_local_data",
    "clear_matched_game",
    "get_cached_scan",
    "get_credits",
    "get_disk_usage",
    "get_log",
    "get_mod_ini",
    "get_mod_log",
    "get_mod_readme",
    "get_offline_mode",
    "get_settings",
    "import_offline_pack",
    "save_mod_ini",
    "save_settings",
    "scan_supported_games",
    "select_matched_game",
    "set_mod_applied",
    "set_offline_mode",
    "update_current_resolution",
    "update_dependencies",
)


NATIVE_WINDOWS = sys.platform == "win32"
# build.py names the release files AnyAspectRatio-Desktop-<version>-<suffix>.
RELEASE_SUFFIX = {"win32": "Windows.exe", "darwin": "macOS.zip"}.get(sys.platform, "Linux.tar.xz")
RELEASE_ASSET = re.compile(rf"^{PLUGIN_NAME}-Desktop-v?\d+(?:\.\d+)*-{re.escape(RELEASE_SUFFIX)}$")


def _drop_wine_overrides(method: str, result: object) -> None:
    """Windows loads the ASI loader DLL from the game folder, so the UI must not plan Steam launch options."""
    if method == "get_cached_scan" and isinstance(result, dict):
        result = result.get("report")
    elif method != "scan_supported_games":
        return
    if isinstance(result, dict):
        for game in result.get("supported_games", []):
            game["wine_dll_overrides"] = []


# The packaged app's unpacked files (PyInstaller); None when run from a checkout.
BUNDLE_DIR = getattr(sys, "_MEIPASS", None)


_BUNDLED_QT_VARIABLES = (
    "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "QT_STYLE_OVERRIDE", "QTWEBENGINE_CHROMIUM_FLAGS",
)


def system_environment() -> dict[str, str]:
    """The environment for programs of the system (xdg-open, wine).

    On Linux the packaged app points LD_LIBRARY_PATH (the user's value kept in LD_LIBRARY_PATH_ORIG) and
    Qt's variables at its own bundled Qt; a system file manager or browser must not load those.
    """
    environment = dict(os.environ)
    if BUNDLE_DIR and sys.platform.startswith("linux"):
        original = environment.pop("LD_LIBRARY_PATH_ORIG", None)
        for name in _BUNDLED_QT_VARIABLES:
            environment.pop(name, None)
        if original is not None:
            environment["LD_LIBRARY_PATH"] = original
    return environment


def plugin_root() -> Path:
    """The Decky plugin folder; the packaged app keeps its catalog.json at the bundle root."""
    return Path(BUNDLE_DIR) if BUNDLE_DIR else Path(__file__).resolve().parents[1] / "AnyAspectRatioDecky"


def default_data_dir(platform: str = sys.platform, environment=os.environ, home: Path | None = None) -> Path:
    home = Path.home() if home is None else home
    if platform == "win32":
        base = environment.get("LOCALAPPDATA") or home / "AppData" / "Local"
    elif platform == "darwin":
        base = home / "Library" / "Application Support"
    else:
        base = environment.get("XDG_DATA_HOME") or home / ".local" / "share"
    return Path(base) / PLUGIN_NAME


def load_plugin_module(data_dir: Path, logger: logging.Logger):
    """Decky's main.py, imported with a stand-in `decky` module that supplies the logger and data folder."""
    decky = types.ModuleType("decky")
    decky.logger = logger
    decky.DECKY_PLUGIN_RUNTIME_DIR = str(data_dir)
    sys.modules["decky"] = decky
    root = str(plugin_root())
    if root not in sys.path:
        sys.path.insert(0, root)
    main = importlib.import_module("main")
    main.SELF_UPDATE_ENABLED = False
    return main


class DesktopBackend:
    """Runs the Decky plugin's RPC methods synchronously, one event loop per call."""

    def __init__(self, data_dir: Path | None = None, logger: logging.Logger | None = None) -> None:
        if logger is None:
            logger = logging.getLogger(PLUGIN_NAME)
            logger.setLevel(logging.INFO)
        self._main = load_plugin_module(data_dir or default_data_dir(), logger)
        self._plugin = self._main.Plugin()
        from launch_setup import LaunchSetup  # Imports the plugin packages, so only after load_plugin_module.

        self._launch_setup = LaunchSetup(self._main.get_store().data_dir / "wine_overrides.json")

    def launch(
        self, action: str, app_id: int | str, mod_id: str | None = None, groups: list[str] | str | None = None,
    ) -> object:
        """Read, plan, add or remove a game's Wine DLL overrides (the desktop stand-in for Steam launch options)."""
        if NATIVE_WINDOWS:
            raise self._main.PluginError("Windows games load the mod's DLL from their own folder; nothing to set.")
        game = self._find_game(app_id, mod_id)
        setup = self._launch_setup
        record_key = f"{app_id}:{game.mod_id}"
        with self._main.log.on_error(f"Wine override {action} failed: {game.mod_id}"):
            if action == "info":
                return setup.info(game.game_path, game.executable_path, list(game.wine_dll_overrides))
            if action == "set":
                saved = setup.set_overrides(game.game_path, game.executable_path, str(groups or ""))
                self._main.log.info("Wine overrides set: %s -> %s", game.mod_id, saved)
                return saved
            if action == "read":
                return setup.read(game.game_path, game.executable_path)
            if action == "prepare":
                return setup.prepare(game.game_path, game.executable_path, groups or [])
            if action == "apply":
                result = setup.apply(record_key, game.game_path, game.executable_path, groups or [])
            elif action == "remove":
                result = setup.remove(record_key, game.game_path, game.executable_path)
            else:
                raise ValueError(f"Unknown launch action: {action}")
        self._main.log.info("Wine overrides %s: %s %s -> %s", action, game.mod_id, result["currentOptions"], result["updatedOptions"])
        return result

    def system_info(self) -> dict[str, object]:
        import system_info

        store = self._main.get_store()
        return {
            "device": system_info.device_name(),
            "os": system_info.os_name(),
            "disks": system_info.disks(),
            "games": system_info.game_counts(store.load_last_scan()),
        }

    def games_by_folder(self) -> list[dict[str, object]]:
        """Every installed Steam game and folder game, grouped by the folder that holds them."""
        from scanner.steam_library_scanner import discover_steam_apps
        from scanner.supported_game_scanner import CUSTOM_APP_ID

        cached = self._main.get_store().load_last_scan() or {}
        scanned = cached.get("report", {}).get("supported_games", []) if isinstance(cached, dict) else []
        supported = {game.get("game_path") for game in scanned if isinstance(game, dict)}
        games = [(app.name, app.game_path) for app in discover_steam_apps().apps if app.game_path]
        games += [
            (game.get("app_name", ""), game["game_path"]) for game in scanned
            if isinstance(game, dict) and game.get("steam_app_id") == CUSTOM_APP_ID and game.get("game_path")
        ]
        groups: dict[str, dict[str, dict[str, object]]] = {}
        for name, path in games:
            groups.setdefault(str(Path(path).parent), {})[path] = {"name": name, "path": path, "supported": path in supported}
        return [
            {"folder": folder, "games": sorted(entries.values(), key=lambda game: str(game["name"]).casefold())}
            for folder, entries in sorted(groups.items(), key=lambda item: item[0].casefold())
        ]

    def virtual_desktops(self) -> list[dict[str, object]]:
        import system_info

        return system_info.virtual_desktops()

    def _find_game(self, app_id: int, mod_id: str | None):
        games = [
            game for game in self._main.Plugin._scan().supported_games
            if game.steam_app_id == str(app_id) and game.game_path and (mod_id is None or game.mod_id == mod_id)
        ]
        if not games:
            raise self._main.PluginError("The game is no longer installed. Scan again and retry.")
        return games[0]

    def check_app_update(self) -> dict[str, object]:
        """The Decky updater's check, but for this OS's desktop release file."""
        from installer.plugin_updater import check_for_update, read_installed_version

        with self._main.log.on_error("App update check failed"):
            return check_for_update(self._plugin._update_repository(), read_installed_version(plugin_root()), RELEASE_ASSET)

    def call(self, method: str, *args: object, **kwargs: object) -> object:
        if method == "check_plugin_update":
            return self.check_app_update()
        if method not in EXPOSED_METHODS:
            raise ValueError(f"Unknown method: {method}")
        result = asyncio.run(getattr(self._plugin, method)(*args, **kwargs))
        if NATIVE_WINDOWS:
            _drop_wine_overrides(method, result)
        if method == "get_settings" and isinstance(result, dict):
            result["self_update_enabled"] = True  # Shows the update check; installing opens the download.
        return result

    def start_catalog_refresh(self) -> None:
        store = self._main.get_store()
        self._main.log.info("Desktop app started")
        threading.Thread(
            target=lambda: asyncio.run(self._plugin._refresh_catalog(store)), name="catalog-refresh", daemon=True
        ).start()

    def stop(self) -> None:
        self._main.log.info("Desktop app stopped")
        self._main.log.close()
