from unittest.mock import patch

from support.builders import archive_source, loose_mod, mod, zip_bytes
from support.fakes import replace_fails, serving, symlinked
from support.sandbox import GameTest

from scanner.mod_catalog import ModBackup

PATCHER = mod(
    archive_source(), "helper.dll", "Managed/Game.dll", "other.dll", "fix.ini", ini_file="fix.ini",
    backups=(ModBackup("Managed/Game.dll", "Managed/Game.dll.bak"), ModBackup("other.dll", "other.dll.bak")),
)
ARCHIVE = zip_bytes({
    "helper.dll": b"helper", "Managed/Game.dll": b"patched game", "other.dll": b"patched other",
    "fix.ini": b"[Resolution]\nWidth=1280\nHeight=800\n",
})
ORIGINALS = {"Managed/Game.dll": "original game", "other.dll": "original other"}
BACKED_UP = {"Managed/Game.dll.bak": "original game", "other.dll.bak": "original other"}
NO_BACKUPS = dict.fromkeys(BACKED_UP)


class SingleFileBackupTests(GameTest):
    def test_original_is_renamed_to_its_backup_and_restored_on_remove(self) -> None:
        for patched in (loose_mod({"data.win": "source/data.win"}, backup_file="data.win.bak"),
                        mod(archive_source("fixes/undertale.zip"), "data.win", backup_file="data.win.bak")):
            with self.subTest(kind=patched.source.kind):
                self.write({"data.win": "original"})
                with serving(zip_bytes({"data.win": b"ultrawide"}) if patched.source.path else b"ultrawide"):
                    for _ in range(2):
                        self.assertEqual([], self.apply(patched, replace_existing_files=True))
                        self.assert_files({"data.win": "ultrawide", "data.win.bak": "original"})
                self.assertEqual([], self.remove(patched))
                self.assert_files({"data.win": "original", "data.win.bak": None})


class BackupTests(GameTest):
    def install(self) -> None:
        self.write(ORIGINALS)
        with serving(ARCHIVE):
            self.assertEqual([], self.apply(PATCHER, replace_existing_files=True))

    def test_reapply_keeps_the_first_backups_and_remove_restores_only_targets(self) -> None:
        self.install()
        with serving(ARCHIVE):
            self.assertEqual([], self.apply(PATCHER, replace_existing_files=True))
            self.assert_files({**BACKED_UP, "Managed/Game.dll": "patched game", "helper.dll": "helper"})
            self.write({"fix.ini": "custom ini"})
            self.assertEqual(["fix.ini"], self.remove(PATCHER))
            self.assert_files(BACKED_UP)
            self.assertEqual([], self.remove(PATCHER, preserve_modified_ini=True))
        self.assert_files({**ORIGINALS, **NO_BACKUPS, "fix.ini": "custom ini", "helper.dll": None})

    def test_restore_is_not_limited_by_the_mod_download_size(self) -> None:
        self.install()
        with patch("store.mod_sources.MAX_MOD_FILE_SIZE_BYTES", 4):
            self.assertEqual([], self.remove(PATCHER, preserve_modified_ini=False))
        self.assert_files(ORIGINALS)

    def test_declining_or_unanswered_conflicts_leave_originals_untouched(self) -> None:
        self.write(ORIGINALS)
        with serving(ARCHIVE):
            for choice in (None, False):
                with self.subTest(choice=choice):
                    self.assertEqual(["Managed/Game.dll", "other.dll"],
                                     self.apply(PATCHER, replace_existing_files=choice))
                    self.assert_files({**ORIGINALS, **NO_BACKUPS})
        self.assert_files({"helper.dll": "helper"})

    def test_missing_original_and_backup_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing"):
            self.apply(PATCHER, replace_existing_files=True)
        self.write({**ORIGINALS, "helper.dll": "helper"})
        with self.assertRaisesRegex(ValueError, "Properties > Installed Files"):
            self.remove(PATCHER, preserve_modified_ini=False)
        self.assert_files({"helper.dll": "helper"})

        # A target that is already patched but has lost its backup is left alone, never backed up as "original".
        self.write({"Managed/Game.dll": "patched game"})
        with serving(ARCHIVE):
            self.assertEqual([], self.apply(PATCHER, replace_existing_files=True))
        self.assert_files({"Managed/Game.dll": "patched game", "Managed/Game.dll.bak": None,
                           "other.dll.bak": "original other"})

    def test_failed_replacement_puts_the_renamed_original_back(self) -> None:
        self.write(ORIGINALS)
        with serving(ARCHIVE), replace_fails("Game.dll", "replacement blocked"):
            with self.assertRaisesRegex(PermissionError, "replacement blocked"):
                self.apply(PATCHER, replace_existing_files=True)
        self.assert_files({"Managed/Game.dll": "original game", "Managed/Game.dll.bak": None})

    def test_failed_restore_keeps_every_backup_for_a_retry(self) -> None:
        self.install()
        with replace_fails(None, "restore blocked"), self.assertRaisesRegex(PermissionError, "restore blocked"):
            self.remove(PATCHER, preserve_modified_ini=False)
        self.assert_files({**BACKED_UP, "helper.dll": "helper", "Managed/Game.dll": "patched game"})

        with replace_fails("other.dll", "second restore blocked"):
            with self.assertRaisesRegex(PermissionError, "second restore blocked"):
                self.remove(PATCHER, preserve_modified_ini=False)
        self.assert_files({**BACKED_UP, "helper.dll": "helper", "Managed/Game.dll": "original game",
                           "other.dll": "patched other"})

        self.assertEqual([], self.remove(PATCHER, preserve_modified_ini=False))
        self.assert_files({**ORIGINALS, **NO_BACKUPS, "helper.dll": None})

    def test_symlinked_or_folder_backup_is_rejected_before_any_change(self) -> None:
        self.write(ORIGINALS)
        with symlinked("Game.dll.bak"):
            for operation in (self.apply, self.remove):
                with self.subTest(operation=operation.__name__), self.assertRaisesRegex(ValueError, "symbolic link"):
                    operation(PATCHER)
        (self.game / "Managed/Game.dll.bak").mkdir()
        for operation in (self.apply, self.remove):
            with self.subTest(operation=operation.__name__), self.assertRaisesRegex(ValueError, "not a regular file"):
                operation(PATCHER)
