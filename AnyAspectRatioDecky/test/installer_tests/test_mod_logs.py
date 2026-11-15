from support.builders import archive_source, mod
from support.fakes import symlinked
from support.sandbox import GameTest

from installer.mod_logs import existing_mod_logs, read_mod_log


class ModLogTests(GameTest):
    def test_logs_are_scoped_to_the_mod_and_read_from_the_end(self) -> None:
        fix = mod(archive_source(), "nested/Fix.ASI", "nested/Other.asi", "loader.dll")
        self.write({"nested/Fix.log": b"prefix" + b"x" * (256 * 1024), "nested/loader.log": "unrelated"})
        self.assertEqual(["nested/Fix.log"], existing_mod_logs(fix.files, str(self.game)))
        self.assertEqual(("x" * (256 * 1024), True), read_mod_log(fix, str(self.game), "nested/Fix.log"))
        with self.assertRaisesRegex(ValueError, "not associated"):
            read_mod_log(fix, str(self.game), "nested/loader.log")
        with self.assertRaisesRegex(ValueError, "no longer available"):
            read_mod_log(fix, str(self.game), "nested/Other.log")
        self.write({"nested/Fix.log": b"invalid \xff text"})
        self.assertEqual(("invalid � text", False), read_mod_log(fix, str(self.game), "nested/Fix.log"))

    def test_symlinked_log_is_rejected(self) -> None:
        fix = mod(archive_source(), "fix.asi")
        with symlinked("fix.log"):
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                read_mod_log(fix, str(self.game), "fix.log")
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                existing_mod_logs(fix.files, str(self.game))
