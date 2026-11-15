from dataclasses import replace
from unittest.mock import patch

from support.builders import archive_source, mod, shortcuts_vdf
from support.plugin import PluginTest

from scanner.mod_catalog import load_mod_catalog
from scanner.non_steam_shortcuts import parse_shortcuts

SHARED_EXE = tuple(entry for entry in load_mod_catalog() if entry.executable == "game.exe")
DOA5 = next(entry for entry in SHARED_EXE if entry.mod_id == "doa5lrfix")


class GameChoiceTests(PluginTest):
    """A shortcut whose executable fits several catalog games: the player picks one, and the plugin remembers it."""

    def setUp(self) -> None:
        super().setUp()
        self.touch("game.exe")
        self.shortcuts = parse_shortcuts(shortcuts_vdf((3000000001, "DOA5", str(self.game / "game.exe"), str(self.game))))

    def choices(self, *mods):
        mods = mods or SHARED_EXE
        return self.catalog(lambda: mods, lambda: self.report(*mods, shortcuts=self.shortcuts))

    def games(self) -> list[dict]:
        return self.call("scan_supported_games")["supported_games"]

    def cached_games(self) -> list[dict]:
        return self.call("get_cached_scan")["report"]["supported_games"]

    def test_every_game_sharing_the_executable_is_offered_as_one_choice(self) -> None:
        self.assertEqual({"doa5lrfix", "sotdfix", "p5strikersfix"}, {entry.mod_id for entry in SHARED_EXE})
        report = self.report(*SHARED_EXE, shortcuts=self.shortcuts)
        self.assertEqual({entry.mod_id for entry in SHARED_EXE}, {game.mod_id for game in report.supported_games})
        self.assertEqual(1, len({game.ambiguity_group for game in report.supported_games}))
        self.assertTrue(all(game.ambiguity_group and game.matched_by_executable for game in report.supported_games))
        with self.scanning(report):
            for entry in SHARED_EXE:
                self.assertEqual((entry, str(self.game)), self.main.Plugin._resolve_mod_game(entry.mod_id, str(self.game)))
            with self.assertRaisesRegex(ValueError, "no longer available"):
                self.main.Plugin._resolve_mod_game(DOA5.mod_id, str(self.root))

    def test_a_choice_is_saved_hides_the_others_and_is_cleared_with_the_mod(self) -> None:
        with self.choices():
            initial = self.call("scan_supported_games")
            self.assertEqual(3, len(initial["supported_games"]))
            choice = next(game for game in initial["supported_games"] if game["mod_id"] == DOA5.mod_id)
            group, chosen = choice["ambiguity_group"], (choice["ambiguity_group"], choice["mod_id"], choice["game_path"])
            with self.assertRaisesRegex(ValueError, "no longer available"):
                self.call("select_matched_game", group, "sotdfix", str(self.root))
            self.call("select_matched_game", *chosen)
            self.assertEqual({"mod_id": DOA5.mod_id, "game_path": str(self.game)}, self.store.load_matched_games()[group])

            self.assertIsNone(self.call("get_cached_scan"))
            self.store.save_last_scan(initial)
            for games in (self.cached_games(), self.games()):
                self.assertEqual([(DOA5.mod_id, True)], [(game["mod_id"], game["match_locked"]) for game in games])
            with self.assertRaisesRegex(ValueError, "changed"):
                self.call("clear_matched_game", group, "sotdfix", str(self.game))
            self.assertEqual(DOA5.mod_id, self.main.Plugin._resolve_mod_game(DOA5.mod_id, str(self.game))[0].mod_id)
            with self.assertRaisesRegex(ValueError, "no longer available"):
                self.main.Plugin._resolve_mod_game("sotdfix", str(self.game))

            # A remove that fails anywhere keeps the saved choice.
            self.store.set_matched_game("another-shortcut", DOA5.mod_id, str(self.game))
            self.store.set_matched_game("unrelated", DOA5.mod_id, str(self.root))
            failures = (
                ("removal failed", OSError, patch.object(self.main, "remove_mod_files", side_effect=OSError("removal failed"))),
                ("applied version could not be saved", ValueError,
                 patch.object(self.store, "set_applied_hash", side_effect=OSError("hash write failed"))),
                ("files were removed.*saved game match", ValueError,
                 patch.object(self.store, "remove_matched_game", side_effect=OSError("match write failed"))),
            )
            for message, error, failure in failures:
                with self.subTest(message), patch.object(self.main, "remove_mod_files", return_value=[]), failure:
                    with self.assertRaisesRegex(error, message):
                        self.call("set_mod_applied", DOA5.mod_id, str(self.game), False)
                self.assertIn(group, self.store.load_matched_games())

            self.write(dict.fromkeys(DOA5.files, "modified"))
            with self.assertRaisesRegex(ValueError, "Remove the mod files"):
                self.call("clear_matched_game", *chosen)

            self.write({"DOA5LRFix.log": "mod log"})
            self.assertEqual(["DOA5LRFix.log"], self.games()[0]["log_files"])
            log = self.call("get_mod_log", DOA5.mod_id, str(self.game), "DOA5LRFix.log")
            self.assertEqual(("mod log", False), (log["content"], log["truncated"]))
            (self.game / "DOA5LRFix.log").unlink()
            self.assertEqual([], self.cached_games()[0]["log_files"])

            self.write({"DOA5LRFix.log": "mod log"})
            with patch("installer.staging.read_source_file", return_value=b"original"):
                result = self.call("set_mod_applied", DOA5.mod_id, str(self.game), False)
                self.assertEqual([DOA5.ini_file], result["conflicting_files"])
                self.assert_files({"DOA5LRFix.log": "mod log", **dict.fromkeys(DOA5.files, "modified")})
                self.assertIn(group, self.store.load_matched_games())
                self.call("set_mod_applied", DOA5.mod_id, str(self.game), False, None, True)
            self.assert_files({"DOA5LRFix.log": None, DOA5.ini_file: "modified",
                               **{name: None for name in DOA5.files if name != DOA5.ini_file}})
            self.assertEqual({"unrelated": {"mod_id": DOA5.mod_id, "game_path": str(self.root)}},
                             self.store.load_matched_games())
            after = self.games()
            self.assertEqual({entry.mod_id for entry in SHARED_EXE}, {game["mod_id"] for game in after})
            self.assertFalse(any(game["match_locked"] for game in after))

            self.call("select_matched_game", *chosen)
            with patch.object(self.main, "remove_mod_files", return_value=[]):
                self.call("set_mod_applied", DOA5.mod_id, str(self.game), False)
            self.assertNotIn(group, self.store.load_matched_games())
            (self.game / DOA5.ini_file).unlink()
            self.call("select_matched_game", *chosen)
            self.call("clear_matched_game", *chosen)
            self.assertNotIn(group, self.store.load_matched_games())

    def test_a_manual_mod_choice_can_be_cleared_even_with_files_present(self) -> None:
        manual = mod(archive_source(), "WukongTweak.asi", mod_id="wukongtweak", executable="game.exe", manual=True)
        other = replace(manual, mod_id="other")
        self.write({manual.files[0]: "existing"})
        with self.choices(manual, other):
            choice = next(game for game in self.games() if game["mod_id"] == manual.mod_id)
            self.call("select_matched_game", choice["ambiguity_group"], choice["mod_id"], choice["game_path"])
            self.call("clear_matched_game", choice["ambiguity_group"], choice["mod_id"], choice["game_path"])
            self.assertEqual({}, self.store.load_matched_games())
            self.assert_files({manual.files[0]: "existing"})
            self.assertEqual(2, len(self.games()))
