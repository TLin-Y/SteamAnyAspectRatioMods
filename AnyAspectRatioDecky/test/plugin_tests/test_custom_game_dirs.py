from helper.errors import PluginError
from store.plugin_store import MAX_CUSTOM_GAME_DIRS, Settings
from support.plugin import PluginTest


class CustomGameDirTests(PluginTest):
    def test_folders_are_added_once_removed_and_kept_by_other_settings(self) -> None:
        games = self.folder("Games")
        self.assertEqual([str(games)], self.call("add_custom_game_dir", f" {games} ")["custom_game_dirs"])
        self.assertEqual([str(games)], self.call("add_custom_game_dir", str(games))["custom_game_dirs"])

        self.call("save_settings", "", "", "", "", "1600", "900")
        self.assertEqual((str(games),), self.store.load_settings().custom_game_dirs)
        self.assertEqual([str(games)], list(self.call("get_settings")["settings"]["custom_game_dirs"]))

        self.assertEqual([], self.call("remove_custom_game_dir", str(games))["custom_game_dirs"])
        self.assertEqual((), self.store.load_settings().custom_game_dirs)

    def test_only_existing_absolute_folders_are_accepted(self) -> None:
        for folder in ("", "relative/games", str(self.root / "missing"), 7):
            with self.subTest(folder=folder), self.assertRaises(PluginError):
                self.call("add_custom_game_dir", folder)
        self.store.save_settings(Settings(custom_game_dirs=tuple(f"/games/{n}" for n in range(MAX_CUSTOM_GAME_DIRS))))
        with self.assertRaisesRegex(PluginError, "At most"):
            self.call("add_custom_game_dir", str(self.folder("Games")))

    def test_a_saved_folder_list_of_the_wrong_shape_is_ignored(self) -> None:
        self.store.settings_path.write_text('{"custom_game_dirs": [1, 2], "offline_mode": true}', encoding="utf-8")
        settings = self.store.load_settings()
        self.assertEqual(((), True), (settings.custom_game_dirs, settings.offline_mode))
