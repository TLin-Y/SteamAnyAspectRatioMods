import logging
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from backend import EXPOSED_METHODS, RELEASE_SUFFIX, DesktopBackend, default_data_dir, plugin_root


class DesktopBackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.data_dir = Path(tempfile.mkdtemp())
        cls.backend = DesktopBackend(cls.data_dir, logging.Logger("desktop_tests"))

    @classmethod
    def tearDownClass(cls) -> None:
        cls.backend.stop()

    def test_plugin_methods_exist_for_every_exposed_name(self) -> None:
        for name in EXPOSED_METHODS:
            with self.subTest(name=name):
                self.assertTrue(callable(getattr(self.backend._plugin, name)))

    def test_settings_use_the_desktop_data_folder_and_show_the_app_update(self) -> None:
        settings = self.backend.call("get_settings")
        self.assertEqual(str(self.data_dir), settings["paths"]["data_dir"])
        self.assertTrue(settings["self_update_enabled"])
        self.assertEqual("bundled", settings["catalog"]["origin"])

    def test_self_update_and_unknown_methods_are_refused(self) -> None:
        for name in ("install_plugin_update", "_main", "missing"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.backend.call(name)

    def test_update_check_offers_this_os_release_file(self) -> None:
        base = "https://github.com/TLin-Y/SteamAnyAspectRatioMods/releases/download/v9.0.0/"
        names = ["AnyAspectRatio-9.0.0.zip", *(f"AnyAspectRatio-Desktop-9.0.0-{suffix}"
                 for suffix in ("Windows.exe", "macOS.zip", "Linux.tar.xz"))]
        release = {"tag_name": "v9.0.0", "assets": [{"name": n, "browser_download_url": base + n} for n in names]}
        with mock.patch("installer.plugin_updater.latest_release",
                        return_value=("TLin-Y/SteamAnyAspectRatioMods", release, "v9.0.0")):
            info = self.backend.call("check_plugin_update")
        self.assertTrue(info["update_available"])
        self.assertEqual("9.0.0", info["latest_version"])
        self.assertEqual(base + f"AnyAspectRatio-Desktop-9.0.0-{RELEASE_SUFFIX}", info["download_url"])
        self.assertTrue(info["installed_version"])

    def test_data_folder_follows_each_platform_convention(self) -> None:
        home = Path("/home/user")
        cases = (
            ("win32", {"LOCALAPPDATA": "C:/Users/user/AppData/Local"}, Path("C:/Users/user/AppData/Local")),
            ("win32", {}, home / "AppData" / "Local"),
            ("darwin", {"XDG_DATA_HOME": "/ignored"}, home / "Library" / "Application Support"),
            ("linux", {"XDG_DATA_HOME": "/data"}, Path("/data")),
            ("linux", {}, home / ".local" / "share"),
        )
        for platform, environment, base in cases:
            with self.subTest(platform=platform, environment=environment):
                self.assertEqual(base / "AnyAspectRatio", default_data_dir(platform, environment, home))

    def test_installed_games_are_grouped_by_folder_with_supported_ones_marked(self) -> None:
        from unittest.mock import patch

        from scanner.steam_library_scanner import InstalledApp, SteamScanReport

        library = Path("/lib/steamapps/common")
        apps = [
            InstalledApp("1", "Zeta", "Zeta", "/lib", str(library / "Zeta")),
            InstalledApp("2", "alpha", "alpha", "/lib", str(library / "alpha")),
            InstalledApp("3", "Missing", "Missing", "/lib", None),
        ]
        cached = {"report": {"supported_games": [
            {"game_path": str(library / "Zeta"), "steam_app_id": "1"},
            {"game_path": "/games/Own", "steam_app_id": "custom", "app_name": "Own"},
        ]}}
        store = self.backend._main.get_store()
        with (
            patch("scanner.steam_library_scanner.discover_steam_apps", return_value=SteamScanReport(1, 3, apps, [])),
            patch.object(store, "load_last_scan", return_value=cached),
        ):
            groups = self.backend.games_by_folder()
        self.assertEqual([
            {"folder": str(Path("/games")), "games": [{"name": "Own", "path": "/games/Own", "supported": True}]},
            {"folder": str(library), "games": [
                {"name": "alpha", "path": str(library / "alpha"), "supported": False},
                {"name": "Zeta", "path": str(library / "Zeta"), "supported": True},
            ]},
        ], groups)

    def test_plugin_root_holds_the_bundled_catalog(self) -> None:
        self.assertTrue((plugin_root() / "catalog.json").is_file())


if __name__ == "__main__":
    unittest.main()
