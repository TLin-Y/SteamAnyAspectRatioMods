import json
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

from helper.constants import (
    MOD_CATALOG_FILE_STATUS_COMPLETE,
    MOD_CATALOG_FILE_STATUS_NONE,
    MOD_CATALOG_FILE_STATUS_PARTIAL,
    MOD_CATALOG_MODS_KEY,
    MOD_FILES_KEY,
    MOD_SOURCE_ARCHIVE,
    MOD_SOURCE_DIRECTORY,
    MOD_SOURCE_FILES,
    MOD_SOURCE_KEY,
    MOD_SOURCE_PATH_KEY,
)
from scanner import (
    InstalledApp,
    SteamScanReport,
    SupportedGame,
    load_mod_catalog,
    match_supported_games,
)
from scanner.mod_installer import mod_readme_path
from scanner.mod_catalog import summarize_credits


class ModCatalogTests(unittest.TestCase):
    def test_credits_default_override_and_ranking(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        document["mods"][0]["credits"] = [{"name": "Other", "url": "https://example.com/o"}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            authors = summarize_credits(load_mod_catalog(path))
            self.assertEqual(["TLin-Y", "Other"], [author["name"] for author in authors])
            self.assertEqual([17, 1], [author["mod_count"] for author in authors])

            document["mods"][0]["credits"] = [{"name": "Bad", "url": "http://x"}]
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)

        everyone = summarize_credits(load_mod_catalog())
        self.assertEqual([("TLin-Y", 18, "")], [(a["name"], a["mod_count"], a["url"]) for a in everyone])

    def test_loads_audited_repository_catalog(self) -> None:
        catalog = load_mod_catalog()
        mod = next(mod for mod in catalog if mod.steam_app_id == "3841510")
        repository_root = Path(__file__).resolve().parents[2]

        self.assertEqual(18, len(catalog))
        self.assertEqual("DW3CE_RE", mod.install_dir)
        self.assertEqual(3, len(mod.files))
        self.assertEqual(
            15,
            sum(candidate.source.kind == MOD_SOURCE_ARCHIVE for candidate in catalog),
        )
        self.assertEqual(
            1,
            sum(candidate.source.kind == MOD_SOURCE_DIRECTORY for candidate in catalog),
        )
        self.assertEqual(
            2,
            sum(candidate.source.kind == MOD_SOURCE_FILES for candidate in catalog),
        )
        for candidate in catalog:
            self.assertTrue(
                (repository_root / mod_readme_path(candidate)).is_file(),
                candidate.mod_id,
            )
            if candidate.source.kind == MOD_SOURCE_ARCHIVE:
                self.assertIsNotNone(candidate.source.path)
                archive_path = repository_root / candidate.source.path
                self.assertTrue(archive_path.is_file(), candidate.mod_id)
                with ZipFile(archive_path) as archive:
                    members = set(archive.namelist())
                for installed_file in candidate.files:
                    relative_path = Path(installed_file)
                    if candidate.source.destination:
                        relative_path = relative_path.relative_to(
                            candidate.source.destination
                        )
                    self.assertIn(relative_path.as_posix(), members, candidate.mod_id)
            elif candidate.source.kind == MOD_SOURCE_DIRECTORY:
                source_directory = repository_root / candidate.source.path
                self.assertTrue(source_directory.is_dir(), candidate.mod_id)
                for installed_file in candidate.files:
                    self.assertTrue(
                        (source_directory / installed_file).is_file(),
                        candidate.mod_id,
                    )
            else:
                for source_file in candidate.source.files:
                    self.assertTrue(
                        (repository_root / source_file.path).is_file(),
                        candidate.mod_id,
                    )

    def test_supports_folder_and_direct_file_sources(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}

        ninja_gaiden = catalog["ninja-gaiden-sigma-2"]
        self.assertEqual("directory", ninja_gaiden.source.kind)
        self.assertTrue(
            (Path(__file__).resolve().parents[2] / ninja_gaiden.source.path).is_dir()
        )

        ori = catalog["ori-and-the-blind-forest-definitive-edition"]
        self.assertIsNone(ori.ini_file)
        self.assertEqual(MOD_SOURCE_FILES, ori.source.kind)
        self.assertEqual(1, len(ori.source.files))

        trials = catalog["trials-of-mana"]
        self.assertEqual(MOD_SOURCE_FILES, trials.source.kind)
        self.assertEqual(3, len(trials.source.files))

    def test_rejects_unsafe_repository_source_path(self) -> None:
        catalog_path = Path(__file__).resolve().parents[1] / "catalog.json"
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
        document[MOD_CATALOG_MODS_KEY][0][MOD_SOURCE_KEY][MOD_SOURCE_PATH_KEY] = (
            "../outside.zip"
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "unsafe relative path"):
                load_mod_catalog(path)

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

    def _touch(self, relative_path: str) -> None:
        path = self.game_path / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()

    def test_matches_game_and_detects_missing_mod_files(self) -> None:
        self._touch(self.mod.executable)

        report = match_supported_games(
            SteamScanReport(1, 1, [self.app], []),
            (self.mod,),
        )
        game = report.supported_games[0]

        self.assertTrue(game.executable_found)
        self.assertEqual(MOD_CATALOG_FILE_STATUS_NONE, game.file_status)
        self.assertEqual(list(self.mod.files), game.files_missing)
        self.assertEqual(list(self.mod.files), game.to_dict()["mod_files"])
        self.assertEqual(self.mod.ini_file, game.to_dict()["ini_file"])
        self.assertEqual(
            list(self.mod.wine_dll_overrides),
            game.to_dict()["wine_dll_overrides"],
        )
        self.assertEqual(1, len(report.to_dict()["supported_games"]))

    def test_reports_partial_and_complete_mod_file_presence(self) -> None:
        self._touch(self.mod.executable)
        self._touch(self.mod.files[0])

        partial = self._scan()
        self.assertEqual(MOD_CATALOG_FILE_STATUS_PARTIAL, partial.file_status)

        for relative_path in self.mod.files:
            self._touch(relative_path)
        complete = self._scan()
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
