import tempfile
import unittest
from pathlib import Path

from helper.constants import (
    STEAM_APP_ID_KEY,
    STEAM_APP_INSTALL_DIRECTORY_KEY,
    STEAM_APP_MANIFEST_FILENAME_FORMAT,
    STEAM_APP_NAME_KEY,
    STEAM_APP_STATE_KEY,
    STEAM_COMMON_DIRECTORY,
    STEAM_LIBRARY_CONFIG_FILE,
    STEAM_LIBRARY_FOLDERS_KEY,
    STEAMAPPS_DIRECTORY,
    VDF_PATH_KEY,
)
from scanner import (
    find_steam_roots,
    scan_steam_libraries,
)
from utils import VdfParseError, parse_vdf


class SteamDiscoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _write(self, path: Path, text: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    @staticmethod
    def _vdf_path(path: Path) -> str:
        return str(path).replace("\\", "/")

    @staticmethod
    def _manifest_path(steamapps: Path, app_id: str) -> Path:
        return steamapps / STEAM_APP_MANIFEST_FILENAME_FORMAT.format(app_id=app_id)

    @staticmethod
    def _app_manifest(app_id: str, name: str, install_dir: str) -> str:
        return (
            f'"{STEAM_APP_STATE_KEY}" {{ "{STEAM_APP_ID_KEY}" "{app_id}" '
            f'"{STEAM_APP_NAME_KEY}" "{name}" '
            f'"{STEAM_APP_INSTALL_DIRECTORY_KEY}" "{install_dir}" }}'
        )

    @staticmethod
    def _library_config(*libraries: Path) -> str:
        entries = " ".join(
            f'"{index}" {{ "{VDF_PATH_KEY}" "{SteamDiscoveryTests._vdf_path(path)}" }}'
            for index, path in enumerate(libraries)
        )
        return f'"{STEAM_LIBRARY_FOLDERS_KEY}" {{ {entries} }}'

    def _create_library(self, root: Path, app_id: str, install_dir: str, name: str) -> Path:
        steamapps = root / STEAMAPPS_DIRECTORY
        common = steamapps / STEAM_COMMON_DIRECTORY / install_dir
        common.mkdir(parents=True)
        self._write(
            steamapps / STEAM_LIBRARY_CONFIG_FILE,
            self._library_config(root),
        )
        self._write(
            self._manifest_path(steamapps, app_id),
            self._app_manifest(app_id, name, install_dir),
        )
        return common

    def test_parse_nested_vdf_and_escaped_values(self) -> None:
        parsed = parse_vdf(
            '"libraryfolders" { "0" { "path" "/home/deck/Steam" } '
            '"1" { "path" "/run/media/deck/SD Card" "apps" { "123" "1" } } }'
        )

        self.assertEqual("/home/deck/Steam", parsed["libraryfolders"]["0"]["path"])
        self.assertEqual("1", parsed["libraryfolders"]["1"]["apps"]["123"])

    def test_parse_rejects_malformed_vdf(self) -> None:
        with self.assertRaises(VdfParseError):
            parse_vdf('"AppState" { "appid" "123"')

    def test_find_steam_roots_uses_known_paths_and_deduplicates(self) -> None:
        steam = self.root / ".local" / "share" / "Steam"
        steam.mkdir(parents=True)

        roots = find_steam_roots(
            home=self.root,
            environment={"STEAM_COMPAT_CLIENT_INSTALL_PATH": str(steam)},
        )

        self.assertEqual([steam.resolve()], roots)

    def test_scans_default_and_removable_libraries(self) -> None:
        default_library = self.root / "Steam"
        sd_library = self.root / "sd-card" / "SteamLibrary"
        self._create_library(default_library, "100", "Game A", "Game A")
        self._create_library(sd_library, "200", "Nested/Game B", "Game B")
        self._write(
            default_library / STEAMAPPS_DIRECTORY / STEAM_LIBRARY_CONFIG_FILE,
            self._library_config(default_library, sd_library),
        )

        report = scan_steam_libraries([default_library])

        self.assertEqual(2, report.libraries_scanned)
        self.assertEqual(["100", "200"], [app.app_id for app in report.apps])
        self.assertEqual("Nested/Game B", report.apps[1].install_dir)
        self.assertEqual(str(sd_library.resolve()), report.apps[1].library_path)

    def test_malformed_manifest_does_not_hide_other_games(self) -> None:
        library = self.root / "Steam"
        self._create_library(library, "100", "Good", "Good Game")
        steamapps = library / STEAMAPPS_DIRECTORY
        self._write(self._manifest_path(steamapps, "200"), '"broken" {')

        report = scan_steam_libraries([library])

        self.assertEqual(["100"], [app.app_id for app in report.apps])
        self.assertEqual(1, len(report.warnings))
        self.assertIn("appmanifest_200.acf", report.warnings[0])

    def test_missing_library_configuration_uses_default_and_warns(self) -> None:
        library = self.root / "Steam"
        self._create_library(library, "100", "Game", "Game")
        (library / STEAMAPPS_DIRECTORY / STEAM_LIBRARY_CONFIG_FILE).unlink()

        report = scan_steam_libraries([library])

        self.assertEqual(1, report.apps_scanned)
        self.assertTrue(any("configuration not found" in warning for warning in report.warnings))

    def test_duplicate_installs_keep_the_largest_folder(self) -> None:
        first = self.root / "Steam"
        second = self.root / "sd" / "SteamLibrary"
        self._create_library(first, "100", "Game", "Game")
        self._create_library(second, "100", "Game", "Game")
        stale = first / STEAMAPPS_DIRECTORY / "common" / "Game"
        moved = second / STEAMAPPS_DIRECTORY / "common" / "Game"
        self._write(stale / "leftover.txt", "x")
        self._write(moved / "game.bin", "x" * 1000)

        report = scan_steam_libraries([first, second])

        self.assertEqual(1, report.apps_scanned)
        self.assertEqual(str(moved.resolve()), report.apps[0].game_path)

        report = scan_steam_libraries([second, first])
        self.assertEqual(str(moved.resolve()), report.apps[0].game_path)

    def test_unavailable_library_is_reported_without_hiding_valid_library(self) -> None:
        valid = self.root / "Steam"
        unavailable = self.root / "missing" / "SteamLibrary"
        self._create_library(valid, "100", "Game", "Game")
        self._write(
            valid / STEAMAPPS_DIRECTORY / STEAM_LIBRARY_CONFIG_FILE,
            self._library_config(valid, unavailable),
        )

        report = scan_steam_libraries([valid])

        self.assertEqual(1, report.apps_scanned)
        self.assertTrue(any("unavailable" in warning for warning in report.warnings))

    def test_rejects_install_path_traversal(self) -> None:
        library = self.root / "Steam"
        steamapps = library / STEAMAPPS_DIRECTORY
        steamapps.mkdir(parents=True)
        self._write(
            steamapps / STEAM_LIBRARY_CONFIG_FILE,
            self._library_config(library),
        )
        self._write(
            self._manifest_path(steamapps, "100"),
            self._app_manifest("100", "Bad", "../Outside"),
        )

        report = scan_steam_libraries([library])

        self.assertEqual([], report.apps)
        self.assertIn("Unsafe install directory", report.warnings[0])

    def test_manifest_id_must_match_filename(self) -> None:
        library = self.root / "Steam"
        steamapps = library / STEAMAPPS_DIRECTORY
        steamapps.mkdir(parents=True)
        self._write(
            steamapps / STEAM_LIBRARY_CONFIG_FILE,
            self._library_config(library),
        )
        self._write(
            self._manifest_path(steamapps, "100"),
            self._app_manifest("200", "Mismatch", "Game"),
        )

        report = scan_steam_libraries([library])

        self.assertEqual([], report.apps)
        self.assertIn("does not match", report.warnings[0])


if __name__ == "__main__":
    unittest.main()
