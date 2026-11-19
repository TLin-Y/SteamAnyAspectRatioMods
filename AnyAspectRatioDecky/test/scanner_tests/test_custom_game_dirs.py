from dataclasses import replace

from support.builders import app_for, archive_source, mod
from support.sandbox import GameTest

from scanner.steam_library_scanner import SteamScanReport
from scanner.supported_game_scanner import CUSTOM_APP_ID, match_supported_games

MOD = mod(archive_source(), "fix.dll", executable="bin/game.exe")
OTHER = replace(MOD, mod_id="other-mod", name="Other mod", steam_app_id="456", install_dir="Other Game",
                executable="other.exe")


class CustomGameFolderTests(GameTest):
    def match(self, *folders: str, apps=()):
        return match_supported_games(SteamScanReport(1, 1, list(apps), []), (MOD, OTHER), (), folders)

    def test_a_folder_is_a_game_or_a_folder_of_games(self) -> None:
        library = self.folder("Games")
        self.write({"bin/game.exe": "", "fix.dll": ""}, self.folder("Games/First"))
        self.write({"other.exe": ""}, self.folder("Games/Second"))
        self.folder("Games/Empty")
        self.folder("Games/.hidden")
        report = self.match(str(library))
        found = {(game.mod_id, game.app_name, game.game_path) for game in report.supported_games}
        self.assertEqual({
            (MOD.mod_id, "First", str(library / "First")),
            (OTHER.mod_id, "Second", str(library / "Second")),
        }, found)
        self.assertTrue(all(game.steam_app_id == CUSTOM_APP_ID and game.matched_by_executable
                            for game in report.supported_games))
        (first,) = [game for game in report.supported_games if game.mod_id == MOD.mod_id]
        self.assertEqual("complete", first.file_status)

        self.assertEqual([MOD.mod_id], [game.mod_id for game in self.match(str(library / "First")).supported_games])

    def test_folders_steam_already_knows_are_left_to_steam(self) -> None:
        self.write({"bin/game.exe": "", "other.exe": ""})
        report = self.match(str(self.game.parent), str(self.game), apps=[app_for(MOD, self.game)])
        self.assertEqual([(MOD.steam_app_id, str(self.game))],
                         [(game.steam_app_id, game.game_path) for game in report.supported_games])

    def test_a_folder_added_twice_or_nested_is_matched_once(self) -> None:
        library = self.folder("Games")
        self.write({"bin/game.exe": ""}, self.folder("Games/First"))
        report = self.match(str(library), str(library / "First"), str(library))
        self.assertEqual([str(library / "First")], [game.game_path for game in report.supported_games])

    def test_a_missing_folder_is_a_warning(self) -> None:
        missing = self.root / "unplugged"
        report = self.match(str(missing))
        self.assertEqual([], report.supported_games)
        self.assertTrue(any(str(missing) in warning for warning in report.warnings))
