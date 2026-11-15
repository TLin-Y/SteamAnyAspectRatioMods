from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from support.builders import app_for, archive_source, catalog_mod, mod, shortcuts_vdf, write_zip
from support.sandbox import GameTest

from scanner.mod_catalog import ModDefinition
from scanner.non_steam_shortcuts import parse_shortcuts, scan_non_steam_shortcuts
from scanner.steam_library_scanner import InstalledApp, SteamScanReport
from scanner.supported_game_scanner import SupportedGame, SupportedGameScanReport, match_supported_games

MOD = mod(archive_source(), "fix.dll", "fix.ini", ini_file="fix.ini", executable="game.exe",
          wine_dll_overrides=("dinput8=n,b",))
OTHER_GAME = "123456"
UNKNOWN_APP = "999999"
SHORTCUT_ID = 3000000001


class MatchingTest(GameTest):
    def match(self, *mods: ModDefinition, apps=(), shortcuts=(), local_mods=()) -> SupportedGameScanReport:
        return match_supported_games(SteamScanReport(1, 1, list(apps), [], list(shortcuts)), mods, local_mods)

    def steam_app(self, entry: ModDefinition = MOD, **changes: object) -> InstalledApp:
        return replace(app_for(entry, self.game), **changes)

    def shortcut(self, executable: str, app_id: str = UNKNOWN_APP) -> InstalledApp:
        return replace(app_for(MOD, self.game), app_id=app_id, install_dir="different",
                       shortcut_executable=str(self.game / executable))

    def only_game(self, entry: ModDefinition = MOD, **app_changes: object) -> SupportedGame:
        (game,) = self.match(entry, apps=[self.steam_app(entry, **app_changes)]).supported_games
        return game


class SteamGameMatchTests(MatchingTest):
    def test_file_status_follows_the_installed_mod_files(self) -> None:
        self.touch(MOD.executable)
        report = self.match(MOD, apps=[self.steam_app()])
        (game,) = report.supported_games
        details = game.to_dict()
        self.assertEqual((True, "not_detected", list(MOD.files)), (game.executable_found, game.file_status, game.files_missing))
        self.assertEqual((list(MOD.files), MOD.ini_file, list(MOD.wine_dll_overrides)),
                         (details["mod_files"], details["ini_file"], details["wine_dll_overrides"]))
        self.assertEqual(1, len(report.to_dict()["supported_games"]))
        self.touch(MOD.files[0])
        self.assertEqual("partial", self.only_game().file_status)
        self.touch(*MOD.files)
        self.assertEqual(("complete", str(self.game / MOD.ini_file)),
                         (self.only_game().file_status, self.only_game().ini_path))

    def test_every_mod_for_the_same_game_is_listed(self) -> None:
        second = replace(MOD, mod_id="second-mod", name="Second mod")
        self.touch(MOD.executable)
        report = self.match(MOD, second, apps=[self.steam_app()])
        self.assertEqual([MOD.mod_id, "second-mod"], [game.mod_id for game in report.supported_games])
        self.assertEqual({MOD.mod_id, "second-mod"}, set(report.mods))
        self.assertFalse(any(game.matched_by_executable for game in report.supported_games))

    def test_restored_backup_target_counts_only_while_its_backup_exists(self) -> None:
        ai = catalog_mod("ai-the-somnium-files")
        (backup,) = ai.file_backups
        self.touch(backup.target)
        restored = self.only_game(ai)
        self.assertEqual(("not_detected", []), (restored.file_status, restored.files_present))
        self.assertNotIn(backup.target, restored.mod_files)
        self.touch(backup.backup)
        applied = self.only_game(ai)
        self.assertEqual("partial", applied.file_status)
        self.assertIn(backup.target, applied.files_present)
        self.assertIn(backup.target, applied.mod_files)

    def test_wrong_install_folder_falls_back_to_a_unique_executable(self) -> None:
        other = replace(MOD, mod_id="other", steam_app_id=OTHER_GAME, executable="other.exe")
        moved = {"install_dir": "different-directory"}
        cases = (  # what: executables present, app changes, mods, matched mod ids, warnings
            ("no executable, only a warning", (), moved, (MOD,), [], 1),
            ("own executable", ("game.exe",), moved, (MOD,), [MOD.mod_id], 1),
            ("unknown app with its executable", ("game.exe",), {**moved, "app_id": UNKNOWN_APP}, (MOD,), [MOD.mod_id], 0),
            ("known app never becomes another game", ("other.exe",), moved, (MOD, other), [], 1),
        )
        for what, executables, changes, mods, expected, warnings in cases:
            with self.subTest(what):
                self.clear_game()
                self.touch(*executables)
                report = self.match(*mods, apps=[self.steam_app(**changes)])
                self.assertEqual(expected, [game.mod_id for game in report.supported_games])
                self.assertEqual(warnings, sum("does not match catalog directory" in w for w in report.warnings))
                for game in report.supported_games:
                    self.assertEqual((True, True, True, changes.get("app_id", MOD.steam_app_id)),
                                     (game.executable_found, game.matched_by_executable,
                                      game.to_dict()["matched_by_executable"], game.steam_app_id))

    def test_catalog_games_match_their_steam_folder_or_executable(self) -> None:
        expedition = catalog_mod("clairobscurfix")
        cases = (  # mod id, executable present, Steam install folder (None: a non-Steam shortcut), by executable
            ("absolumfix", "x64.steam/Absolum.exe", "Absolum", False),
            ("absolumfix", "x64.steam/Absolum.exe", "Other", True),
            *(("guacamelee-2", exe, folder, folder is None) for exe in ("Guac2_64.exe", "Guac2_x64.exe")
              for folder in ("Guacamelee2", None)),
            ("guacamelee-stce", "Game.exe", "DrinkBox_Game4", False),
            ("guacamelee-stce", "GuacSTCE.exe", "Guacamelee.STCE", True),
            ("rayman-legends", "Rayman Legends.exe", "Rayman Legends", False),
            ("rayman-legends", "Rayman Legends.exe", "Rayman.Legends", True),
            *(("clairobscurfix", exe, None, True) for exe in expedition.executables),
            *((MOD.mod_id, "other.exe", folder, True) for folder in ("different", None)),
        )
        alternative = replace(MOD, executable="game-primary.exe", executable_alternatives=("other.exe",))
        for mod_id, executable, folder, by_executable in cases:
            with self.subTest(mod=mod_id, executable=executable, folder=folder):
                entry = alternative if mod_id == MOD.mod_id else catalog_mod(mod_id)
                self.clear_game()
                self.touch(executable)
                if folder is None:
                    report = self.match(entry, shortcuts=[self.shortcut(executable, entry.steam_app_id)])
                else:
                    report = self.match(entry, apps=[self.steam_app(entry, install_dir=folder)])
                (game,) = report.supported_games
                self.assertEqual((entry.steam_app_id, str(self.game / executable), str(self.game), True, by_executable),
                                 (game.steam_app_id, game.executable_path, game.game_path, game.executable_found,
                                  game.matched_by_executable))

    def test_ambiguous_matches_ask_the_player_to_choose(self) -> None:
        nested = replace(MOD, executable="dir/game.exe")
        cases = (  # what: mods, executables present, scan arguments, matched mod ids
            ("same file name in two games, Steam", (nested, replace(nested, mod_id="other", steam_app_id=OTHER_GAME,
                                                                    executable="other/game.exe")),
             ("dir/game.exe",), {"apps": [self.steam_app(app_id=UNKNOWN_APP, install_dir="different")]}, {MOD.mod_id}),
            ("same file name in two games, shortcut", (nested, replace(nested, mod_id="other", steam_app_id=OTHER_GAME,
                                                                       executable="other/game.exe")),
             ("dir/game.exe",), {"shortcuts": [self.shortcut("dir/game.exe")]}, {MOD.mod_id}),
            ("known app in another folder shares its executable", (MOD, replace(MOD, mod_id="other", steam_app_id=OTHER_GAME)),
             ("game.exe",), {"apps": [self.steam_app(install_dir="different-directory")]}, {MOD.mod_id}),
            ("two games' executables in one folder", (replace(MOD, executable="first.exe"),
                                                       replace(MOD, mod_id="other", steam_app_id=OTHER_GAME,
                                                               executable="second.exe")),
             ("first.exe", "second.exe"), {"apps": [self.steam_app(app_id=UNKNOWN_APP)]}, {MOD.mod_id, "other"}),
        )
        for what, mods, executables, scan, expected in cases:
            with self.subTest(what):
                self.clear_game()
                self.touch(*executables)
                games = self.match(*mods, **scan).supported_games
                self.assertEqual(expected, {game.mod_id for game in games})
                groups = {game.ambiguity_group for game in games}
                self.assertEqual(1, len(groups))
                self.assertIsNotNone(games[0].ambiguity_group)
                self.assertEqual(games[0].ambiguity_group, games[0].to_dict()["ambiguity_group"])

    def test_fallback_probes_each_file_once_and_only_under_existing_folders(self) -> None:
        found = replace(MOD, executable="missing.exe", executable_alternatives=("found.exe",))
        other = replace(found, mod_id="other", steam_app_id=OTHER_GAME, executable_alternatives=("other.exe",))
        self.touch("found.exe")
        probes: dict[Path, int] = {}
        real_is_file = Path.is_file

        def count(path: Path) -> bool:
            probes[path] = probes.get(path, 0) + 1
            return real_is_file(path)

        with patch.object(Path, "is_file", count):
            (game,) = self.match(found, other, apps=[self.steam_app(app_id=UNKNOWN_APP, install_dir="different")]).supported_games
        self.assertEqual((found.mod_id, str(self.game / "found.exe")), (game.mod_id, game.executable_path))
        self.assertEqual({1}, set(probes.values()))

        nested = replace(MOD, executable="missing/path/game.exe")
        with patch.object(Path, "is_file", side_effect=AssertionError("unexpected file probe")):
            self.assertEqual([], self.match(nested, apps=[self.steam_app(app_id=UNKNOWN_APP, install_dir="x")]).supported_games)

        with patch("scanner.supported_game_scanner.os.scandir", side_effect=PermissionError("denied")):
            report = self.match(MOD, apps=[self.steam_app(app_id=UNKNOWN_APP, install_dir="different")])
        self.assertEqual([], report.supported_games)
        self.assertTrue(any("denied" in warning for warning in report.warnings))

    def test_missing_executable_or_destination_is_warned_about(self) -> None:
        moved = replace(MOD, executable="new/game.exe", source=replace(MOD.source, destination="old/Binaries"))
        report = self.match(moved, apps=[self.steam_app()])
        self.assertFalse(report.supported_games[0].executable_found)
        self.assertTrue(any("no catalogued executable found" in warning for warning in report.warnings))
        self.assertTrue(any("catalog destination" in w and "old" in w and "Binaries" in w for w in report.warnings))

        expedition = catalog_mod("clairobscurfix")
        self.touch("Expedition33_Steam.exe")
        report = self.match(expedition, apps=[self.steam_app(expedition)])
        self.assertTrue(report.supported_games[0].executable_found)
        self.assertTrue(any("catalog destination" in warning for warning in report.warnings))
        (self.game / expedition.source.destination).mkdir(parents=True)
        report = self.match(expedition, apps=[self.steam_app(expedition)])
        self.assertFalse(any("catalog destination" in warning for warning in report.warnings))


WUKONG = mod(archive_source(), "b1/Binaries/Win64/WukongTweak.asi", mod_id="wukongtweak", steam_app_id="2358720",
             install_dir="BlackMythWukong", executable="b1/Binaries/Win64/b1.exe", wine_dll_overrides=("dsound=n,b",))


class ShortcutMatchTests(MatchingTest):
    def setUp(self) -> None:
        super().setUp()
        self.touch(WUKONG.executable)
        self.executable = self.game / WUKONG.executable

    def shortcuts(self, exe: str | Path, start_dir: Path | None = None, name: str = "Black Myth GOG") -> list[InstalledApp]:
        return parse_shortcuts(shortcuts_vdf((SHORTCUT_ID, name, str(exe), str(start_dir or self.game))))

    def test_shortcut_in_steam_userdata_matches_by_executable(self) -> None:
        self.write({"userdata/1/config/shortcuts.vdf": shortcuts_vdf((SHORTCUT_ID, "Black Myth GOG", str(self.executable),
                                                                      str(self.game)))}, self.folder("steam"))
        shortcuts = scan_non_steam_shortcuts([self.root / "steam"])
        (game,) = self.match(WUKONG, shortcuts=shortcuts).supported_games
        self.assertEqual((str(SHORTCUT_ID), "wukongtweak", True), (game.steam_app_id, game.mod_id, game.executable_found))
        steam_app = InstalledApp("2358720", "Black Myth: Wukong", "BlackMythWukong", "", str(self.game))
        (game,) = self.match(WUKONG, apps=[steam_app], shortcuts=shortcuts).supported_games
        self.assertEqual("2358720", game.steam_app_id)

    def test_shortcuts_that_never_match(self) -> None:
        self.touch("other.exe", "run.sh")
        for what, exe in (("another executable", self.game / "other.exe"), ("a wrapper script", self.game / "run.sh"),
                          ("a relative path", WUKONG.executable)):
            with self.subTest(what):
                self.assertEqual([], self.match(WUKONG, shortcuts=self.shortcuts(exe)).supported_games)

    def test_nested_shortcut_resolves_and_installs_into_the_game_root(self) -> None:
        archive = write_zip(self.root / "mod.zip", {WUKONG.files[0]: b"mod contents"})
        local = replace(WUKONG, source=replace(WUKONG.source, kind="local_archive", path=str(archive)))
        (game,) = self.match(local, shortcuts=self.shortcuts(self.executable, self.executable.parent)).supported_games
        self.assertEqual((str(self.game), str(self.executable)), (game.game_path, game.executable_path))
        self.assertEqual([], self.apply(local))
        self.assert_files({WUKONG.files[0]: "mod contents"})

    def test_shared_file_names_ask_the_player_to_choose(self) -> None:
        cases = (
            ("two mods share the executable name", (WUKONG, replace(WUKONG, mod_id="other", executable="elsewhere/b1.exe")),
             {(WUKONG.mod_id, str(self.game))}),
            ("one mod fits two game roots", (replace(WUKONG, executable_alternatives=("b1.exe",)),),
             {(WUKONG.mod_id, str(self.game)), (WUKONG.mod_id, str(self.executable.parent))}),
        )
        for what, mods, expected in cases:
            with self.subTest(what):
                games = self.match(*mods, shortcuts=self.shortcuts(self.executable)).supported_games
                self.assertEqual(expected, {(game.mod_id, game.game_path) for game in games})
                self.assertEqual(1, len({game.ambiguity_group for game in games}))
                self.assertIsNotNone(games[0].ambiguity_group)

    def test_elden_ring_launcher_shortcuts_match_only_beside_the_game(self) -> None:
        elden_ring = catalog_mod("elden-ring-ultrawide-fix")
        root = self.root / "localGames/ELDEN RING"
        for launcher in ("modengine2_launcher.exe", "start_protected_game.exe", "start_game_in_offline_mode.exe",
                         "start_protected_game-old.exe"):
            with self.subTest(launcher=launcher):
                self.write({f"Game/{launcher}": ""}, root)
                (root / "Game/eldenring.exe").unlink(missing_ok=True)
                shortcuts = self.shortcuts(root / "Game" / launcher, root / "Game", "Elden Ring custom")
                self.assertEqual([], self.match(elden_ring, shortcuts=shortcuts).supported_games)
                self.write({"Game/eldenring.exe": "game"}, root)
                games = self.match(elden_ring, shortcuts=shortcuts).supported_games
                if launcher == "start_protected_game-old.exe":
                    self.assertEqual([], games)
                    continue
                (game,) = games
                self.assertEqual((str(root), "elden-ring-ultrawide-fix", True, True, str(SHORTCUT_ID)),
                                 (game.game_path, game.mod_id, game.offline_only, game.matched_by_executable,
                                  game.steam_app_id))
