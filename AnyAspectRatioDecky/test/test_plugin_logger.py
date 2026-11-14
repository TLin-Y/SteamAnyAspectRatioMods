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

from logger.plugin_logger import attach_file_handler, clear_file_handler_logs
from store.plugin_store import PluginStore


class PluginLoggerFileTests(unittest.TestCase):
    def test_file_handler_writes_events_to_plugin_log(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "logs" / "plugin.log"
            base_logger = logging.Logger("test.file", level=logging.INFO)
            handler = attach_file_handler(base_logger, path)
            base_logger.info("Settings saved")
            handler.close()
            self.assertIn("INFO Settings saved", path.read_text(encoding="utf-8"))

    def test_clear_removes_active_and_rotated_logs_then_logging_resumes(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            path = Path(directory) / "logs" / "plugin.log"
            base_logger = logging.Logger("test.clear", level=logging.INFO)
            handler = attach_file_handler(base_logger, path)
            stack.callback(handler.close)
            base_logger.info("old entry")
            for index in (1, 2):
                Path(f"{path}.{index}").write_text("old", encoding="utf-8")
            clear_file_handler_logs(handler)
            self.assertFalse(path.exists())
            self.assertFalse(Path(f"{path}.1").exists())
            self.assertFalse(Path(f"{path}.2").exists())
            base_logger.info("new entry")
            content = path.read_text(encoding="utf-8")
            self.assertIn("new entry", content)
            self.assertNotIn("old entry", content)

    def test_clear_local_data_rpc_removes_logs_and_keeps_settings(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            store = PluginStore(Path(directory))
            store.ensure_directories()
            store.settings_path.write_text("{}", encoding="utf-8")
            base_logger = logging.Logger("test.clear.rpc", level=logging.INFO)
            handler = attach_file_handler(base_logger, store.log_path)
            stack.callback(handler.close)
            base_logger.info("old log")
            fake_decky = types.ModuleType("decky")
            fake_decky.logger = base_logger
            with patch.dict(sys.modules, {"decky": fake_decky}):
                main = importlib.import_module("main")
            with (
                patch.object(main, "get_store", return_value=store),
                patch.object(main, "_file_handler", handler),
            ):
                result = asyncio.run(main.Plugin().clear_local_data())
            self.assertEqual({}, result)
            self.assertFalse(store.log_path.exists())
            self.assertTrue(store.settings_path.exists())
            base_logger.info("new log")
            self.assertIn("new log", store.log_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
