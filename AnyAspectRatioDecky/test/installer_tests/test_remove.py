from support.builders import RAW, archive_source, loose_mod, mod
from support.fakes import failing, fetched, serving, symlinked, unlink_fails
from support.sandbox import GameTest

FIX = {"fix.dll": "source/fix.dll", "fix.ini": "source/fix.ini"}


class RemoveTests(GameTest):
    def test_deletes_only_catalogued_files_even_when_partly_installed(self) -> None:
        fix = loose_mod({"mods/fix.dat": "fix.dat", "fix.ini": "fix.ini"})
        self.write({"mods/fix.dat": "mod", "mods/user.dat": "user"})
        self.assertEqual([], self.remove(fix))
        self.assert_files({"mods/fix.dat": None, "fix.ini": None, "mods/user.dat": "user"})

    def test_modified_ini_is_asked_about_then_kept_or_deleted(self) -> None:
        fix = loose_mod(FIX, ini_file="fix.ini")
        for keep, ini in ((True, "user configuration"), (False, None)):
            with self.subTest(keep=keep):
                self.write({"fix.dll": "mod", "fix.ini": "user configuration"})
                with serving(b"default configuration") as network:
                    self.assertEqual(["fix.ini"], self.remove(fix))
                    self.assert_files({"fix.dll": "mod", "fix.ini": "user configuration"})
                    self.assertEqual([], self.remove(fix, preserve_modified_ini=keep))
                self.assertEqual({f"{RAW}/source/fix.ini"}, set(fetched(network)))
                self.assert_files({"fix.dll": None, "fix.ini": ini})

    def test_unreachable_source_counts_as_a_modified_ini(self) -> None:
        fix = loose_mod(FIX, ini_file="fix.ini")
        self.write({"fix.dll": "mod", "fix.ini": "user configuration"})
        with failing(OSError("offline")) as network:
            self.assertEqual(["fix.ini"], self.remove(fix))
            self.assertEqual([], self.remove(fix, preserve_modified_ini=True))
            self.assert_files({"fix.ini": "user configuration"})
            self.assertEqual([], self.remove(fix, preserve_modified_ini=False))
        self.assertEqual(2, network.call_count)
        self.assert_files({"fix.ini": None})


class RemoveLogTests(GameTest):
    def test_deletes_the_logs_named_after_each_asi_and_nothing_else(self) -> None:
        fix = mod(archive_source(), "mods/Fix.ASI", "mods/Other.asi", "fix.ini")
        self.write({"mods/Fix.ASI": "mod", "mods/Fix.log": "log", "mods/Other.log": "log", "mods/unrelated.log": "keep"})
        self.assertEqual([], self.remove(fix))
        self.assert_files({"mods/Fix.ASI": None, "mods/Fix.log": None, "mods/Other.log": None,
                           "mods/unrelated.log": "keep"})

    def test_unsafe_or_locked_log_stops_the_remove(self) -> None:
        fix = mod(archive_source(), "fix.asi")
        self.write({"fix.asi": "mod"})
        with symlinked("fix.log"), self.assertRaisesRegex(ValueError, "symbolic link"):
            self.remove(fix)
        self.assert_files({"fix.asi": "mod"})
        self.write({"fix.log": "log"})
        with unlink_fails("fix.log", "log is locked"), self.assertRaisesRegex(PermissionError, "log is locked"):
            self.remove(fix)
