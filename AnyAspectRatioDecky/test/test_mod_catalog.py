import json
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch
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
    def test_readme_sits_beside_archive_zips(self) -> None:
        from scanner.mod_installer import mod_readme_path

        mods = {mod.mod_id: mod for mod in load_mod_catalog()}
        self.assertEqual("Dark Souls III/README.md", mod_readme_path(mods["dark-souls-iii"]))

        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        document["mods"] = [document["mods"][0]]
        document["mods"][0]["source"]["path"] = "external/Lyall/SekiroFix/SekiroFix_0.0.1.zip"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            self.assertEqual(
                "external/Lyall/SekiroFix/README.md",
                mod_readme_path(load_mod_catalog(path)[0]),
            )

    def test_credits_default_override_and_ranking(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        document["mods"] = [mod for mod in document["mods"] if "credits" not in mod]
        document["mods"][0]["credits"] = [{"name": "Other", "url": "https://example.com/o"}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            authors = summarize_credits(load_mod_catalog(path))
            self.assertEqual(["TLin-Y", "Other"], [author["name"] for author in authors])
            self.assertEqual([21, 1], [author["mod_count"] for author in authors])

            document["mods"][0]["credits"] = [{"name": "Bad", "url": "http://x"}]
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)

            document["mods"][0]["credits"] = [
                {"name": "Bad", "support": [{"label": "Pay", "url": "http://x"}]}
            ]
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)

        everyone = {a["name"]: a for a in summarize_credits(load_mod_catalog())}
        self.assertEqual(("", 22), (everyone["TLin-Y"]["url"], everyone["TLin-Y"]["mod_count"]))
        self.assertEqual(["Ko-fi"], [link["label"] for link in everyone["TLin-Y"]["support"]])
        self.assertEqual(
            ["Patreon", "Ko-fi"], [link["label"] for link in everyone["Lyall"]["support"]]
        )

    def test_loads_audited_repository_catalog(self) -> None:
        catalog = [mod for mod in load_mod_catalog() if not mod.credits or mod.credits[0].name == "TLin-Y"]
        mod = next(mod for mod in catalog if mod.steam_app_id == "3841510")
        repository_root = Path(__file__).resolve().parents[2]

        self.assertEqual(22, len(catalog))
        self.assertEqual("DW3CE_RE", mod.install_dir)
        self.assertEqual(3, len(mod.files))
        self.assertEqual(
            19,
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

    def test_visions_of_mana_install_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "visions-of-mana")
        destination = "VisionsofMana/Binaries/Win64"
        self.assertEqual("VisionsofMana", mod.install_dir)
        self.assertEqual(f"{destination}/VisionsofMana-Win64-Shipping.exe", mod.executable)
        self.assertEqual(destination, mod.source.destination)
        self.assertEqual(
            {f"{destination}/{name}" for name in ("dxgi.dll", "VoM_ARFix.asi", "VoM_ARFix.ini")},
            set(mod.files),
        )
        self.assertEqual(f"{destination}/VoM_ARFix.ini", mod.ini_file)

    def test_palworld_catalog_paths(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}
        palworld = catalog["palworld"]
        self.assertEqual("Palworld", palworld.install_dir)
        self.assertEqual(
            "Pal/Binaries/Win64/Palworld-Win64-Shipping.exe",
            palworld.executable,
        )
        self.assertEqual(
            {
                "Pal/Binaries/Win64/winmm.dll",
                "Pal/Binaries/Win64/Palworld_ARFix.asi",
                "Pal/Binaries/Win64/Palworld_ARFix.ini",
            },
            set(palworld.files),
        )

    def test_undertale_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "undertale")
        self.assertEqual("Undertale", mod.install_dir)
        self.assertEqual("UNDERTALE.exe", mod.executable)
        self.assertEqual(MOD_SOURCE_ARCHIVE, mod.source.kind)
        self.assertEqual(
            "external/Twig/Undertale/Undertale-Ultrawide-Fix.zip", mod.source.path
        )
        self.assertEqual(("data.win",), mod.files)
        self.assertEqual("data.win.bak", mod.backup_file)
        self.assertEqual("external/Twig/Undertale/README.md", mod_readme_path(mod))
        self.assertEqual(("Twig",), tuple(credit.name for credit in mod.credits))

    def test_elden_ring_offline_launcher_and_archive_paths(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}
        mod = catalog["elden-ring-ultrawide-fix"]
        self.assertTrue(mod.offline_only)
        self.assertTrue(catalog["dark-souls-iii"].offline_only)
        self.assertIsNone(catalog["dark-souls-iii"].launcher_replacement)
        self.assertEqual("ELDEN RING", mod.install_dir)
        self.assertEqual("Game/eldenring.exe", mod.executable)
        self.assertEqual((
            "Game/modengine2_launcher.exe",
            "Game/start_protected_game.exe",
            "Game/start_game_in_offline_mode.exe",
        ), mod.executable_alternatives)
        self.assertEqual("Game", mod.source.destination)
        self.assertEqual("Game/start_game_in_offline_mode.exe", mod.launcher_replacement.source)
        self.assertEqual("Game/start_protected_game.exe", mod.launcher_replacement.target)
        self.assertEqual("Game/start_protected_game-old.exe", mod.launcher_replacement.backup)
        self.assertEqual(("TechieW",), tuple(credit.name for credit in mod.credits))
        self.assertEqual("external/TechieW/ELDEN RING/README.md", mod_readme_path(mod))
        root = Path(__file__).resolve().parents[2]
        self.assertTrue((root / mod_readme_path(mod)).is_file())
        with ZipFile(root / mod.source.path) as archive:
            self.assertEqual(
                {f"Game/{info.filename}" for info in archive.infolist() if not info.is_dir()},
                set(mod.files),
            )

    def test_rejects_invalid_offline_launcher_metadata(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        entry = next(mod for mod in document["mods"] if mod["id"] == "elden-ring-ultrawide-fix")
        document["mods"] = [entry]
        original = dict(entry["launcher_replacement"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            for change in (
                {"source": "../outside.exe"},
                {"source": "Game/missing.exe"},
                {"target": "Game/eldenring.exe"},
                {"target": original["source"]},
                {"backup": "Game/dinput8.dll"},
                {"backup": "Game/modengine2_launcher.exe"},
                {"backup": original["target"]},
                {"backup": "other/backup.exe"},
                {"source": 123},
                {"extra": "value"},
            ):
                with self.subTest(change=change):
                    entry["launcher_replacement"] = {**original, **change}
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_mod_catalog(path)
            entry["launcher_replacement"] = original
            for value in (False, "true", 1):
                with self.subTest(offline_only=value):
                    entry["offline_only"] = value
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_mod_catalog(path)

    def test_beast_of_reincarnation_install_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "beastofreincarnationfix")
        destination = "BeastOfReincarnation/Binaries/Win64"
        self.assertEqual("BeastOfReincarnation", mod.install_dir)
        self.assertEqual(destination, mod.source.destination)
        self.assertEqual(
            {f"{destination}/{name}" for name in (
                "BeastOfReincarnationFix.asi", "BeastOfReincarnationFix.ini", "dsound.dll"
            )},
            set(mod.files),
        )
        self.assertEqual(f"{destination}/BeastOfReincarnationFix.ini", mod.ini_file)

    def test_guacamelee_2_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "guacamelee-2")
        self.assertEqual("534550", mod.steam_app_id)
        self.assertEqual("Guacamelee 2", mod.install_dir)
        self.assertEqual("Guac2_64.exe", mod.executable)
        self.assertEqual(("Guac2_x64.exe",), mod.executable_alternatives)
        self.assertEqual(
            ("dinput8.dll", "Guac2_ARFix.asi", "Guac2_ARFix.ini"),
            mod.files,
        )
        self.assertEqual("Guac2_ARFix.ini", mod.ini_file)

    def test_verified_external_mod_install_paths(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}
        for mod_id, app_id, install_dir, executable, asi_path in (
            ("wofffix", "552700", "WOFF", "WOFF.exe", "WOFFFix.asi"),
            ("doa6fix", "838380", "Dead or Alive 6", "doa6.exe", "DOA6Fix.asi"),
            (
                "so4fix", "609150", "STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster",
                "StarOceanTheLastHope.exe", "SO4Fix.asi",
            ),
            (
                "mdaraincodeplusfix", "2903950", "MDA - RAIN CODE Plus",
                "RainCodePlus.exe", "RainCodePlus/Binaries/Win64/MDARainCodePlusFix.asi",
            ),
        ):
            with self.subTest(mod_id=mod_id):
                mod = catalog[mod_id]
                self.assertEqual(app_id, mod.steam_app_id)
                self.assertEqual(install_dir, mod.install_dir)
                self.assertEqual(executable, mod.executable)
                self.assertIn(asi_path, mod.files)

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

    def test_executable_alternatives_are_validated(self) -> None:
        catalog_path = Path(__file__).resolve().parents[1] / "catalog.json"
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
        entry = document["mods"][0]
        document["mods"] = [entry]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            for value in ("other.exe", [""], [123], ["../outside.exe"], [entry["executable"]]):
                with self.subTest(value=value):
                    entry["executable_alternatives"] = value
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_mod_catalog(path)

    def test_expedition_33_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "clairobscurfix")
        self.assertEqual("Expedition 33", mod.install_dir)
        self.assertEqual("Sandfall/Binaries/Win64/SandFall-Win64-Shipping.exe", mod.executable)
        self.assertEqual(("Expedition33_Steam.exe",), mod.executable_alternatives)
        self.assertEqual("Sandfall/Binaries/Win64", mod.source.destination)

    def test_archive_checksums_match_the_zips_in_the_repository(self) -> None:
        # Fails after a zip changes until scripts/update_catalog_hashes.py is run.
        scripts = str(Path(__file__).resolve().parents[1] / "scripts")
        sys.path.insert(0, scripts)
        try:
            from update_catalog_hashes import update
        finally:
            sys.path.remove(scripts)
        self.assertFalse(update(check=True), "run scripts/update_catalog_hashes.py")
        for mod in load_mod_catalog():
            if mod.source.kind == MOD_SOURCE_ARCHIVE:
                self.assertEqual(64, len(mod.source.sha256), mod.mod_id)



class ManualModTests(unittest.TestCase):
    def test_manual_mods_are_listed_but_never_installed(self) -> None:
        from scanner.mod_installer import apply_mod_files, remove_mod_files

        mods = {mod.mod_id: mod for mod in load_mod_catalog()}
        wild_hearts, dbxv2 = mods["wildheartsfix"], mods["dbxv2fix"]
        self.assertTrue(wild_hearts.manual and dbxv2.manual)
        self.assertEqual("", wild_hearts.executable)
        self.assertFalse(mods["dark-souls-iii"].manual)

        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / "WILD HEARTS"
            game.mkdir()
            app = InstalledApp("1938010", "WILD HEARTS", "WILD HEARTS", directory, str(game))
            report = match_supported_games(SteamScanReport(1, 1, [app], []), (wild_hearts,))
            self.assertEqual(1, len(report.supported_games))
            self.assertTrue(report.supported_games[0].to_dict()["manual"])

            with self.assertRaisesRegex(ValueError, "manually"):
                apply_mod_files(wild_hearts, str(game))
            with self.assertRaisesRegex(ValueError, "manually"):
                remove_mod_files(wild_hearts, str(game))
            self.assertEqual([], list(game.iterdir()))

    def test_executable_is_required_unless_manual(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        entry = next(mod for mod in document["mods"] if mod["id"] == "wildheartsfix")
        document["mods"] = [entry]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            entry["manual"] = False
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)
            entry["manual"] = "yes"
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
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

    def test_reports_every_mod_for_the_same_game(self) -> None:
        other = replace(self.mod, mod_id="second-mod", name="Second mod")
        report = match_supported_games(
            SteamScanReport(1, 1, [self.app], []),
            (self.mod, other),
        )

        self.assertEqual(
            [self.mod.mod_id, "second-mod"],
            [game.mod_id for game in report.supported_games],
        )
        self.assertEqual({self.mod.mod_id, "second-mod"}, set(report.mods))

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

    def test_mismatched_steam_directory_uses_unique_executable(self) -> None:
        self._touch(self.mod.executable)
        app = replace(self.app, install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod,))
        self.assertEqual([self.mod.mod_id], [game.mod_id for game in report.supported_games])
        self.assertTrue(report.supported_games[0].executable_found)
        self.assertTrue(report.supported_games[0].matched_by_executable)
        self.assertTrue(report.supported_games[0].to_dict()["matched_by_executable"])
        self.assertEqual(1, len(report.warnings))

    def test_unknown_steam_app_uses_unique_executable(self) -> None:
        self._touch(self.mod.executable)
        app = replace(self.app, app_id="999999", install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod,))
        self.assertEqual([self.mod.mod_id], [game.mod_id for game in report.supported_games])
        self.assertEqual("999999", report.supported_games[0].steam_app_id)

    def test_known_steam_id_does_not_fallback_to_another_game(self) -> None:
        other = replace(self.mod, mod_id="other", steam_app_id="123456", executable="other.exe")
        self._touch(other.executable)
        app = replace(self.app, install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod, other))
        self.assertEqual([], report.supported_games)

    def test_executable_alternatives_match_both_steam_and_shortcuts(self) -> None:
        mod = replace(self.mod, executable="game-primary.exe", executable_alternatives=("other.exe",))
        self._touch("other.exe")
        for is_shortcut in (False, True):
            with self.subTest(is_shortcut=is_shortcut):
                app = replace(
                    self.app, app_id="999999", install_dir="different",
                    shortcut_executable=str(self.game_path / "other.exe"),
                )
                scan = SteamScanReport(1, 1, [] if is_shortcut else [app], [], [app] if is_shortcut else [])
                (game,) = match_supported_games(scan, (mod,)).supported_games
                self.assertTrue(game.executable_found)
                self.assertEqual(str(self.game_path / "other.exe"), game.executable_path)

    def test_guacamelee_alternative_executable_matches_steam_install(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "guacamelee-2")
        self._touch("Guac2_x64.exe")
        app = InstalledApp(
            app_id=mod.steam_app_id,
            name="Guacamelee! 2",
            install_dir="Guacamelee! 2",
            library_path=self.app.library_path,
            game_path=self.app.game_path,
        )

        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))

        (game,) = report.supported_games
        self.assertEqual("534550", game.steam_app_id)
        self.assertEqual(str(self.game_path / "Guac2_x64.exe"), game.executable_path)
        self.assertTrue(game.matched_by_executable)

    def test_ambiguous_executable_names_require_selection(self) -> None:
        mod = replace(self.mod, executable="dir/game.exe")
        other = replace(mod, mod_id="other", steam_app_id="123456", executable="other/game.exe")
        self._touch(mod.executable)
        for is_shortcut in (False, True):
            with self.subTest(is_shortcut=is_shortcut):
                app = replace(
                    self.app, app_id="999999", install_dir="different",
                    shortcut_executable=str(self.game_path / mod.executable),
                )
                scan = SteamScanReport(1, 1, [] if is_shortcut else [app], [], [app] if is_shortcut else [])
                report = match_supported_games(scan, (mod, other))
                (game,) = report.supported_games
                self.assertEqual(mod.mod_id, game.mod_id)
                self.assertIsNotNone(game.ambiguity_group)
                self.assertEqual(game.ambiguity_group, game.to_dict()["ambiguity_group"])

    def test_ambiguous_executable_for_mismatched_known_steam_id_requires_selection(self) -> None:
        mod = replace(self.mod, executable="game.exe")
        other = replace(mod, mod_id="other", steam_app_id="123456")
        self._touch(mod.executable)
        app = replace(self.app, install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod, other))
        (game,) = report.supported_games
        self.assertEqual(mod.mod_id, game.mod_id)
        self.assertIsNotNone(game.ambiguity_group)

    def test_known_steam_id_keeps_all_mods_when_executable_is_shared(self) -> None:
        mod = replace(self.mod, executable="game.exe")
        other = replace(mod, mod_id="other")
        self._touch(mod.executable)
        report = match_supported_games(SteamScanReport(1, 1, [self.app], []), (mod, other))
        self.assertEqual([mod.mod_id, "other"], [game.mod_id for game in report.supported_games])
        self.assertTrue(all(not game.matched_by_executable for game in report.supported_games))

    def test_file_probes_are_cached_for_one_scan_without_missing_alternatives(self) -> None:
        mod = replace(self.mod, executable="missing.exe", executable_alternatives=("found.exe",))
        other = replace(mod, mod_id="other", steam_app_id="123456", executable="missing.exe",
                        executable_alternatives=("other.exe",))
        self._touch("found.exe")
        shortcut = replace(self.app, app_id="999999", install_dir="different")
        original_is_file = Path.is_file
        probes: dict[Path, int] = {}

        def count_probe(path: Path) -> bool:
            probes[path] = probes.get(path, 0) + 1
            return original_is_file(path)

        with patch.object(Path, "is_file", count_probe):
            report = match_supported_games(
                SteamScanReport(1, 1, [shortcut], []), (mod, other)
            )
        self.assertEqual([mod.mod_id], [game.mod_id for game in report.supported_games])
        self.assertEqual(str(self.game_path / "found.exe"), report.supported_games[0].executable_path)
        self.assertTrue(all(count == 1 for count in probes.values()), probes)

    def test_fallback_does_not_probe_paths_with_absent_root_components(self) -> None:
        mod = replace(self.mod, executable="missing/path/game.exe")
        app = replace(self.app, app_id="999999", install_dir="different")
        with patch.object(Path, "is_file", side_effect=AssertionError("unexpected file probe")):
            report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.assertEqual([], report.supported_games)

    def test_fallback_reports_unreadable_game_directory(self) -> None:
        app = replace(self.app, app_id="999999", install_dir="different")
        with patch("scanner.supported_game_scanner.os.scandir", side_effect=PermissionError("denied")):
            report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod,))
        self.assertEqual([], report.supported_games)
        self.assertTrue(any("denied" in warning for warning in report.warnings))

    def test_warns_when_known_game_executable_or_destination_is_missing(self) -> None:
        mod = replace(
            self.mod, executable="new/game.exe",
            source=replace(self.mod.source, destination="old/Binaries"),
        )
        report = match_supported_games(SteamScanReport(1, 1, [self.app], []), (mod,))
        self.assertEqual(1, len(report.supported_games))
        self.assertFalse(report.supported_games[0].executable_found)
        self.assertTrue(any("no catalogued executable found" in warning for warning in report.warnings))
        self.assertTrue(any("catalog destination" in warning for warning in report.warnings))
        self.assertTrue(any("old" in warning and "Binaries" in warning for warning in report.warnings))

    def test_launcher_match_warns_when_nested_destination_is_missing(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "clairobscurfix")
        self._touch("Expedition33_Steam.exe")
        app = replace(self.app, app_id=mod.steam_app_id, install_dir=mod.install_dir)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.assertTrue(report.supported_games[0].executable_found)
        self.assertTrue(any("catalog destination" in warning for warning in report.warnings))
        destination = self.game_path / mod.source.destination
        destination.mkdir(parents=True)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.assertFalse(any("catalog destination" in warning for warning in report.warnings))

    def test_multiple_unique_executables_in_one_directory_require_selection(self) -> None:
        mod = replace(self.mod, executable="first.exe")
        other = replace(mod, mod_id="other", steam_app_id="123456", executable="second.exe")
        self._touch(mod.executable)
        self._touch(other.executable)
        app = replace(self.app, app_id="999999")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod, other))
        self.assertEqual({mod.mod_id, other.mod_id}, {game.mod_id for game in report.supported_games})
        self.assertEqual(1, len({game.ambiguity_group for game in report.supported_games}))
        self.assertIsNotNone(report.supported_games[0].ambiguity_group)

    def test_expedition_33_alternatives_at_game_root(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "clairobscurfix")
        for executable in (mod.executable, *mod.executable_alternatives):
            with self.subTest(executable=executable):
                self._touch(executable)
                shortcut = replace(
                    self.app, app_id="999999", install_dir="different",
                    shortcut_executable=str(self.game_path / executable),
                )
                report = match_supported_games(SteamScanReport(0, 0, [], [], [shortcut]), (mod,))
                (game,) = report.supported_games
                self.assertEqual(str(self.game_path / executable), game.executable_path)
                self.assertEqual(str(self.game_path), game.game_path)
                (self.game_path / executable).unlink()


if __name__ == "__main__":
    unittest.main()
