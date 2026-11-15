from support.packs import StoreTest

from store.plugin_store import Settings, validate_settings


class SettingsTests(StoreTest):
    def test_valid_settings_are_normalised_and_saved(self) -> None:
        self.assertEqual(Settings(), validate_settings(Settings()))
        normalised = validate_settings(Settings("https://github.com/owner/repo.git/", "feature/x", "a/b.json", ""))
        self.assertEqual("https://github.com/owner/repo", normalised.catalog_repository_url)
        self.assertTrue(validate_settings(Settings(offline_mode=True)).offline_mode)

        self.assertEqual(Settings(), self.store.load_settings())
        self.store.save_settings(Settings(catalog_ref="dev", ini_resolution_width=1920, ini_resolution_height=1080,
                                          offline_mode=True))
        loaded = self.store.load_settings()
        self.assertEqual(("dev", 1920, 1080, True), (loaded.catalog_ref, loaded.ini_resolution_width,
                                                     loaded.ini_resolution_height, loaded.offline_mode))

    def test_unsafe_values_are_rejected(self) -> None:
        for bad in (
            Settings(catalog_repository_url="http://github.com/o/r"),
            Settings(catalog_repository_url="https://example.com/o/r"),
            Settings(catalog_repository_url="https://github.com/o"),
            Settings(catalog_ref="a..b"),
            Settings(catalog_path="../catalog.json"),
            Settings(catalog_path="catalog.txt"),
            Settings(local_archive_dir="relative/dir"),
            Settings(local_archive_dir="/definitely/not/a/folder"),
            *(Settings(ini_resolution_width=width, ini_resolution_height=height)
              for width, height in ((0, 800), (1280, -1), (16385, 800), (True, 800))),
        ):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate_settings(bad)

    def test_a_non_boolean_offline_flag_in_the_file_is_ignored(self) -> None:
        self.store.settings_path.write_text('{"offline_mode": "yes"}', encoding="utf-8")
        self.assertFalse(self.store.load_settings().offline_mode)
