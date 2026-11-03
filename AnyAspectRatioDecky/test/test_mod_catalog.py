import json
import tempfile
import unittest
from pathlib import Path

from helper.constants import (
    MOD_CATALOG_FILE_STATUS_COMPLETE,
    MOD_CATALOG_FILE_STATUS_NONE,
    MOD_CATALOG_FILE_STATUS_PARTIAL,
    MOD_CATALOG_MODS_KEY,
    MOD_FILES_KEY,
)
from scanner import (
    InstalledApp,
    SteamScanReport,
    SupportedGame,
    load_mod_catalog,
    match_supported_games,
)


class ModCatalogTests(unittest.TestCase):
    def test_loads_audited_repository_catalog(self) -> None:
        catalog = load_mod_catalog()
        mod = next(mod for mod in catalog if mod.steam_app_id == "3841510")

        self.assertEqual("DW3CE_RE", mod.install_dir)
        self.assertEqual(3, len(mod.files))

    def test_rejects_unsafe_mod_file_path(self) -> None:
        catalog_path = Path(__file__).resolve().parents[1] / "catalog.json"
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
        document[MOD_CATALOG_MODS_KEY][0][MOD_FILES_KEY][0] = "../outside.dll"

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "unsafe relative path"):
                load_mod_catalog(path)


class SupportedGameScannerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.game_path = Path(self.temporary_directory.name) / "game"
        self.game_path.mkdir()
        self.mod = load_mod_catalog()[0]
        self.app = InstalledApp(
            app_id=self.mod.steam_app_id,
            name="Test Game",
            install_dir=self.mod.install_dir,
            library_path=self.temporary_directory.name,
            game_path=str(self.game_path),
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _scan(self) -> SupportedGame:
        return match_supported_games(
            SteamScanReport(1, 1, [self.app], []),
            (self.mod,),
        ).supported_games[0]

    def test_matches_game_and_detects_missing_mod_files(self) -> None:
        (self.game_path / self.mod.executable).touch()

        report = match_supported_games(
            SteamScanReport(1, 1, [self.app], []),
            (self.mod,),
        )
        game = report.supported_games[0]

        self.assertTrue(game.executable_found)
        self.assertEqual(MOD_CATALOG_FILE_STATUS_NONE, game.file_status)
        self.assertEqual(list(self.mod.files), game.files_missing)
        self.assertEqual(1, len(report.to_dict()["supported_games"]))

    def test_reports_partial_and_complete_mod_file_presence(self) -> None:
        (self.game_path / self.mod.executable).touch()
        (self.game_path / self.mod.files[0]).touch()

        partial = self._scan()
        self.assertEqual(MOD_CATALOG_FILE_STATUS_PARTIAL, partial.file_status)

        for relative_path in self.mod.files:
            (self.game_path / relative_path).touch()
        complete = self._scan()
        self.assertEqual(MOD_CATALOG_FILE_STATUS_COMPLETE, complete.file_status)
        self.assertEqual(MOD_CATALOG_FILE_STATUS_COMPLETE, complete.file_status)
        self.assertEqual(str(self.game_path / self.mod.ini_file), complete.ini_path)

    def test_ignores_app_with_mismatched_install_directory(self) -> None:
        mismatched_app = InstalledApp(
            app_id=self.app.app_id,
            name=self.app.name,
            install_dir="different-directory",
            library_path=self.app.library_path,
            game_path=self.app.game_path,
        )
        report = match_supported_games(
            SteamScanReport(1, 1, [mismatched_app], []),
            (self.mod,),
        )

        self.assertEqual([], report.supported_games)
        self.assertEqual(1, len(report.warnings))
        self.assertIn("does not match catalog directory", report.warnings[0])


if __name__ == "__main__":
    unittest.main()
