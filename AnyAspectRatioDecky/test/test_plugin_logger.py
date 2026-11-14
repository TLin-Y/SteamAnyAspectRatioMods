import asyncio
import importlib
import logging
import sys
import tempfile
import types
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch

from logger.plugin_logger import PluginLogger
from store.plugin_store import PluginStore


def file_logger(stack: ExitStack, name: str, path: Path) -> tuple[PluginLogger, logging.Logger]:
    base = logging.Logger(name, level=logging.INFO)
    plugin_log = PluginLogger()
    plugin_log.bind(base)
    plugin_log.attach_file(path)
    stack.callback(plugin_log.close)
    return plugin_log, base


class PluginLoggerFileTests(unittest.TestCase):
    def test_file_handler_writes_events_to_plugin_log(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            path = Path(directory) / "logs" / "plugin.log"
            plugin_log, _ = file_logger(stack, "test.file", path)
            plugin_log.info("Settings saved")
            plugin_log.close()
            self.assertIn("INFO Settings saved", path.read_text(encoding="utf-8"))

    def test_error_includes_traceback_only_when_given_the_exception(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            path = Path(directory) / "plugin.log"
            plugin_log, _ = file_logger(stack, "test.error", path)
            plugin_log.error("Plain failure")
            try:
                raise OSError("disk gone")
            except OSError as error:
                plugin_log.error("Copy failed: 50% done", error)
            plugin_log.close()
            content = path.read_text(encoding="utf-8")
            self.assertIn("ERROR Plain failure\n", content)
            self.assertIn("ERROR Copy failed: 50% done\nTraceback", content)
            self.assertIn("OSError: disk gone", content)

    def test_on_error_logs_and_reraises(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            path = Path(directory) / "plugin.log"
            plugin_log, _ = file_logger(stack, "test.on_error", path)
            with self.assertRaises(ValueError), plugin_log.on_error("Scan failed"):
                raise ValueError("bad catalog")
            plugin_log.close()
            self.assertIn("ValueError: bad catalog", path.read_text(encoding="utf-8"))

    def test_read_tail_returns_newest_lines(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            path = Path(directory) / "plugin.log"
            plugin_log, _ = file_logger(stack, "test.tail", path)
            path.write_bytes(b"one\ntwo\nthree \xff\n")
            self.assertEqual("two\nthree �", plugin_log.read_tail(2))

    def test_clear_removes_active_and_rotated_logs_then_logging_resumes(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            path = Path(directory) / "logs" / "plugin.log"
            plugin_log, _ = file_logger(stack, "test.clear", path)
            plugin_log.info("old entry")
            for index in (1, 2):
                Path(f"{path}.{index}").write_text("old", encoding="utf-8")
            plugin_log.clear_files()
            self.assertFalse(path.exists())
            self.assertFalse(Path(f"{path}.1").exists())
            self.assertFalse(Path(f"{path}.2").exists())
            plugin_log.info("new entry")
            content = path.read_text(encoding="utf-8")
            self.assertIn("new entry", content)
            self.assertNotIn("old entry", content)

    def test_clear_local_data_rpc_removes_logs_and_keeps_settings(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            store = PluginStore(Path(directory))
            store.ensure_directories()
            store.settings_path.write_text("{}", encoding="utf-8")
            plugin_log, base = file_logger(stack, "test.clear.rpc", store.log_path)
            plugin_log.info("old log")
            fake_decky = types.ModuleType("decky")
            fake_decky.logger = base
            with patch.dict(sys.modules, {"decky": fake_decky}):
                main = importlib.import_module("main")
            with (
                patch.object(main, "get_store", return_value=store),
                patch.object(main, "log", plugin_log),
            ):
                result = asyncio.run(main.Plugin().clear_local_data())
            self.assertEqual({}, result)
            self.assertFalse(store.log_path.exists())
            self.assertTrue(store.settings_path.exists())
            plugin_log.info("new log")
            self.assertIn("new log", store.log_path.read_text(encoding="utf-8"))

    def test_error_result_reports_the_message_or_the_error_type(self) -> None:
        fake_decky = types.ModuleType("decky")
        fake_decky.logger = logging.getLogger("test.error_result")
        with patch.dict(sys.modules, {"decky": fake_decky}):
            main = importlib.import_module("main")
        self.assertEqual({"error": "denied"}, main._error_result(PermissionError("denied")))
        self.assertEqual({"error": "TimeoutError"}, main._error_result(TimeoutError()))


if __name__ == "__main__":
    unittest.main()
