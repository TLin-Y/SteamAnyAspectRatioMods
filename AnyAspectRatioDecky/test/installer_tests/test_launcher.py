from dataclasses import replace

from support.builders import app_for, archive_source, catalog_mod, mod, zip_bytes
from support.fakes import no_network, offline, replace_fails, serving, symlinked, unlink_fails
from support.sandbox import PLUGIN_ROOT, GameTest

from installer.launcher import setup_complete
from scanner.mod_catalog import LauncherReplacement
from scanner.steam_library_scanner import SteamScanReport
from scanner.supported_game_scanner import match_supported_games
from store.mod_sources import set_pack_dir

OFFLINE_LAUNCHER = mod(
    archive_source(destination="Game"), "Game/offline.exe", "Game/mods/fix.dll",
    executable="Game/game.exe", offline_only=True,
    launcher_replacement=LauncherReplacement("Game/offline.exe", "Game/protected.exe", "Game/protected-old.exe"),
)
ARCHIVE = zip_bytes({"offline.exe": b"offline", "mods/fix.dll": b"fix"})
ORIGINAL = {"Game/game.exe": "game executable", "Game/protected.exe": "original protected launcher"}
REPLACED = {"Game/protected.exe": "offline", "Game/protected-old.exe": "original protected launcher"}
RESTORED = {**ORIGINAL, "Game/protected-old.exe": None, "Game/offline.exe": None}


class LauncherTests(GameTest):
    def setUp(self) -> None:
        super().setUp()
        self.write(ORIGINAL)

    def install(self, launcher_mod=OFFLINE_LAUNCHER) -> None:
        with serving(ARCHIVE):
            self.assertEqual([], self.apply(launcher_mod, replace_existing_files=True))

    def status(self) -> str:
        report = match_supported_games(SteamScanReport(1, 1, [app_for(OFFLINE_LAUNCHER, self.game)], []), (OFFLINE_LAUNCHER,))
        return report.supported_games[0]

    def test_confirm_apply_reapply_remove_and_scan_status(self) -> None:
        self.write({"Game/mods/unrelated.dll": "keep"})
        self.assertEqual("not_detected", self.status().file_status)
        self.assertTrue(self.status().to_dict()["offline_only"])
        with serving(ARCHIVE):
            self.assertEqual(["Game/protected.exe"], self.apply(OFFLINE_LAUNCHER))
            self.assert_files(RESTORED)
            for _ in range(2):
                self.assertEqual([], self.apply(OFFLINE_LAUNCHER, replace_existing_files=True))
                self.assert_files(REPLACED)
                self.assertTrue(setup_complete(OFFLINE_LAUNCHER, self.game))
                self.assertEqual("complete", self.status().file_status)
        self.write({"Game/protected.exe": "original protected launcher"})
        self.assertEqual("partial", self.status().file_status)
        self.assertIn("Game/protected.exe", self.status().files_missing)
        for _ in range(2):
            self.assertEqual([], self.remove(OFFLINE_LAUNCHER))
            self.assert_files({**RESTORED, "Game/mods/unrelated.dll": "keep"})
        self.assertEqual("not_detected", self.status().file_status)

    def test_declining_the_replacement_leaves_launcher_and_backup_alone(self) -> None:
        with serving(ARCHIVE):
            self.assertEqual(["Game/protected.exe"], self.apply(OFFLINE_LAUNCHER, replace_existing_files=False))
        self.assert_files({"Game/protected.exe": "original protected launcher", "Game/protected-old.exe": None})

    def test_failed_copy_restores_the_launcher_and_the_partial_install_can_be_removed(self) -> None:
        with serving(ARCHIVE), replace_fails("protected.exe", "launcher locked"):
            with self.assertRaisesRegex(PermissionError, "launcher locked"):
                self.apply(OFFLINE_LAUNCHER, replace_existing_files=True)
        self.assert_files({"Game/protected.exe": "original protected launcher", "Game/protected-old.exe": None})
        self.assertEqual([], self.remove(OFFLINE_LAUNCHER))
        self.assert_files(RESTORED)

    def test_failed_remove_keeps_everything_the_replaced_launcher_needs(self) -> None:
        for failure, message in ((replace_fails(None, "locked"), "locked"),
                                 (unlink_fails("fix.dll", "mod locked"), "mod locked")):
            with self.subTest(message=message):
                self.install()
                with failure, self.assertRaisesRegex(PermissionError, message):
                    self.remove(OFFLINE_LAUNCHER)
                self.assert_files({**REPLACED, "Game/offline.exe": "offline"})

    def test_launcher_can_be_a_copy_of_the_game_executable(self) -> None:
        copy_game = replace(OFFLINE_LAUNCHER, launcher_replacement=replace(
            OFFLINE_LAUNCHER.launcher_replacement, source="Game/game.exe"))
        self.install(copy_game)
        self.assert_files({"Game/protected.exe": "game executable"})
        self.assertTrue(setup_complete(copy_game, self.game))
        self.remove(copy_game)
        self.assert_files({**ORIGINAL, "Game/protected-old.exe": None})

    def test_missing_backup_never_saves_the_offline_copy_as_original(self) -> None:
        self.write({"Game/protected.exe": "offline"})
        with serving(ARCHIVE), self.assertRaisesRegex(ValueError, "backup is missing"):
            self.apply(OFFLINE_LAUNCHER, replace_existing_files=True)
        self.assert_files({"Game/protected.exe": "offline", "Game/protected-old.exe": None})

    def test_symlinked_backup_is_rejected_before_any_change(self) -> None:
        with symlinked("protected-old.exe"):
            for operation in (self.apply, self.remove):
                with self.subTest(operation=operation.__name__), self.assertRaisesRegex(ValueError, "symbolic link"):
                    operation(OFFLINE_LAUNCHER)


class CatalogLauncherTests(GameTest):
    def test_elden_ring_archive_from_the_repository_applies_and_removes(self) -> None:
        elden_ring = catalog_mod("elden-ring-ultrawide-fix")
        self.write({"Game/eldenring.exe": "original game", "Game/start_protected_game.exe": "original launcher"})
        set_pack_dir(PLUGIN_ROOT.parent)
        with offline(), no_network():
            self.assertEqual([], self.apply(elden_ring, replace_existing_files=True))
            self.assertTrue(setup_complete(elden_ring, self.game))
            self.assertEqual(self.read("Game/start_game_in_offline_mode.exe"), self.read("Game/start_protected_game.exe"))
            self.assertTrue(all((self.game / relative).is_file() for relative in elden_ring.files))
            self.assertEqual([], self.remove(elden_ring, preserve_modified_ini=False))
        self.assert_files({"Game/start_protected_game.exe": "original launcher", "Game/eldenring.exe": "original game",
                           "Game/start_protected_game-old.exe": None, **dict.fromkeys(elden_ring.files)})
