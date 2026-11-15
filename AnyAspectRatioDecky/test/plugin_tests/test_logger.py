from contextlib import ExitStack

from support.plugin import file_logger
from support.sandbox import SandboxTest

from helper.errors import PluginError


class PluginLoggerTests(SandboxTest):
    def setUp(self) -> None:
        super().setUp()
        self.path = self.root / "logs/plugin.log"
        stack = ExitStack()
        self.addCleanup(stack.close)
        self.log = file_logger(stack, self.id(), self.path)

    def content(self) -> str:
        self.log.close()
        return self.path.read_text(encoding="utf-8")

    def test_events_and_errors_go_to_the_plugin_log(self) -> None:
        self.log.info("Settings saved")
        self.log.error("Plain failure")
        try:
            raise OSError("disk gone")
        except OSError as error:
            self.log.error("Copy failed: 50% done", error)
        content = self.content()
        for line in ("INFO Settings saved", "ERROR Plain failure\n", "ERROR Copy failed: 50% done\nTraceback",
                     "OSError: disk gone"):
            self.assertIn(line, content)

    def test_on_error_reraises_and_logs_bugs_with_traceback_but_plugin_errors_as_one_line(self) -> None:
        with self.assertRaises(ValueError), self.log.on_error("Scan failed"):
            raise ValueError("bad catalog")
        with self.assertRaisesRegex(PluginError, "^Mod archive is invalid$"), self.log.on_error("Mod apply failed"):
            raise PluginError("Mod archive is invalid")
        with self.assertRaisesRegex(PluginError, "^KeyError: 'mod_id'$") as caught:
            with self.log.on_error("Scan failed"):
                {}["mod_id"]
        self.assertIsInstance(caught.exception.__cause__, KeyError)
        with self.assertRaisesRegex(PluginError, "^RuntimeError$"), self.log.on_error("Scan failed"):
            raise RuntimeError()
        content = self.content()
        self.assertIn("ValueError: bad catalog", content)
        self.assertIn("KeyError: 'mod_id'", content)
        self.assertIn("WARNING Mod apply failed: Mod archive is invalid\n", content)
        self.assertEqual(3, content.count("Traceback"))

    def test_read_tail_returns_the_newest_lines(self) -> None:
        self.path.write_bytes(b"one\ntwo\nthree \xff\n")
        self.assertEqual("two\nthree �", self.log.read_tail(2))

    def test_clear_removes_active_and_rotated_logs_then_logging_resumes(self) -> None:
        self.log.info("old entry")
        self.write({"logs/plugin.log.1": "old", "logs/plugin.log.2": "old"})
        self.log.clear_files()
        self.assert_files({"logs/plugin.log": None, "logs/plugin.log.1": None, "logs/plugin.log.2": None})
        self.log.info("new entry")
        content = self.content()
        self.assertIn("new entry", content)
        self.assertNotIn("old entry", content)
