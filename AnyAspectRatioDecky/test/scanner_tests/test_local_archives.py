import json
from unittest.mock import patch

from support.builders import write_zip
from support.sandbox import GameTest

from scanner.local_archives import build_local_mods, scan_local_archives
from scanner.mod_catalog import load_mod_catalog
from scanner.steam_library_scanner import InstalledApp, SteamScanReport
from scanner.supported_game_scanner import match_supported_games

APP_ID = "42424"


class LocalArchiveTests(GameTest):
    """Zips the player drops into a folder are matched to installed games by name or app id."""

    def setUp(self) -> None:
        super().setUp()
        self.archives = self.folder("zips")
        self.index = self.root / "index.json"
        self.touch("bin/CoolGame.exe", "bin/UnityCrashHandler64.exe")
        self.app = InstalledApp(APP_ID, "Cool Game", "Cool Game", str(self.root), str(self.game))

    def zip(self, name: str, entries: dict[str, bytes | str]) -> None:
        write_zip(self.archives / name, entries)

    def scan(self, **options: object):
        return scan_local_archives(str(self.archives), **options)

    def test_name_match_installs_beside_the_game_executable(self) -> None:
        self.zip("Cool-Game-v1.2.0.zip", {"dinput8.dll": "dll", "fix.ini": "ini", "__MACOSX/x": "junk"})
        self.zip("Other Game.zip", {"a.txt": "a"})
        self.write({"broken.zip": "not a zip"}, self.archives)
        archives, warnings = self.scan(index_path=self.index)
        self.assertEqual(["Cool-Game-v1.2.0", "Other Game"], [archive.name for archive in archives])
        self.assertTrue(any("broken.zip" in warning for warning in warnings))
        self.assertTrue(self.index.is_file())

        (local,), warnings = build_local_mods(archives, [self.app], set())
        self.assertEqual((f"local-{APP_ID}", "bin", ("bin/dinput8.dll", "bin/fix.ini"), ("dinput8=n,b",)),
                         (local.mod_id, local.source.destination, local.files, local.wine_dll_overrides))
        self.assertTrue(any("Other Game" in warning for warning in warnings))

    def test_app_id_in_the_file_name_matches_unless_the_catalog_has_the_game(self) -> None:
        self.zip(f"anything [{APP_ID}].zip", {"a.dll": "1"})
        archives, _ = self.scan()
        self.assertEqual(1, len(build_local_mods(archives, [self.app], set())[0]))
        mods, warnings = build_local_mods(archives, [self.app], {APP_ID})
        self.assertEqual([], list(mods))
        self.assertTrue(any("catalog mod" in warning for warning in warnings))

    def test_each_unusable_archive_reports_why(self) -> None:
        self.zip("Evil.zip", {"../escape.dll": "1"})
        self.zip("Empty.zip", {"__MACOSX/x": "junk", "Thumbs.db": "junk"})
        self.zip("Collide.zip", {"Fix.dll": "1", "fix.dll": "2"})
        self.zip("Big.zip", {f"f{index}.dll": "1" for index in range(4)})
        with patch("scanner.local_archives.MAX_LOCAL_ARCHIVE_FILES", 3):
            archives, warnings = self.scan()
        self.assertEqual([], archives)
        report = "\n".join(warnings)
        for reason in ("unsafe", "Empty.zip", "archive contains no files", "entry names collide", "more than 3 files"):
            self.assertIn(reason, report)

    def test_index_is_reused_only_for_the_same_folder_version_and_archives(self) -> None:
        self.zip("Cool Game.zip", {"a.dll": "1"})
        self.scan(index_path=self.index)
        with patch("scanner.local_archives.list_archive_files") as listing:
            self.assertEqual(1, len(self.scan(index_path=self.index)[0]))
        listing.assert_not_called()

        document = json.loads(self.index.read_text(encoding="utf-8"))
        for stale in ({**document, "version": 0}, {**document, "directory": "/elsewhere"}, {**document, "archives": []}):
            with self.subTest(stale=stale):
                self.index.write_text(json.dumps(stale), encoding="utf-8")
                with patch("scanner.local_archives.list_archive_files", return_value=(["a.dll"], None)) as listing:
                    self.assertEqual(1, len(self.scan(index_path=self.index)[0]))
                listing.assert_called_once()

    def test_local_mod_is_listed_and_applied_like_a_catalog_mod(self) -> None:
        self.zip("Cool Game.zip", {"mod.dll": "new", "data/x.bin": "x"})
        local_mods, _ = build_local_mods(self.scan()[0], [self.app], set())
        report = match_supported_games(SteamScanReport(1, 1, [self.app], []), load_mod_catalog(), local_mods)
        game = next(game for game in report.supported_games if game.mod_id == f"local-{APP_ID}")
        self.assertEqual(("local_archive", "not_detected"), (game.source_kind, game.file_status))
        self.assertEqual([], self.apply(local_mods[0]))
        self.assert_files({"bin/mod.dll": "new", "bin/data/x.bin": "x"})
