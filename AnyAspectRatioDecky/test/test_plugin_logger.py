import io
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

from logger.plugin_logger import PluginLogger, attach_file_handler, clear_file_handler_logs
from store import PluginStore


class PluginLoggerFileTests(unittest.TestCase):
    def test_file_handler_writes_events_to_plugin_log(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "logs" / "plugin.log"
            base_logger = logging.Logger("test.file", level=logging.INFO)
            handler = attach_file_handler(base_logger, path)
            PluginLogger(base_logger).info("settings.saved", phase="ok")
            handler.close()
            self.assertIn("event=settings.saved phase=ok", path.read_text(encoding="utf-8"))

    def test_clear_removes_active_and_rotated_logs_then_logging_resumes(self) -> None:
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            path = Path(directory) / "logs" / "plugin.log"
            base_logger = logging.Logger("test.clear", level=logging.INFO)
            handler = attach_file_handler(base_logger, path)
            stack.callback(handler.close)
            logger = PluginLogger(base_logger)
            logger.info("settings.saved", phase="old")
            for index in (1, 2):
                Path(f"{path}.{index}").write_text("old", encoding="utf-8")
            clear_file_handler_logs(handler)
            self.assertFalse(path.exists())
            self.assertFalse(Path(f"{path}.1").exists())
            self.assertFalse(Path(f"{path}.2").exists())
            logger.info("settings.saved", phase="new")
            content = path.read_text(encoding="utf-8")
            self.assertIn("phase=new", content)
            self.assertNotIn("phase=old", content)

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


class PluginLoggerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.stream = io.StringIO()
        self.base_logger = logging.Logger("test.plugin", level=logging.DEBUG)
        self.base_logger.addHandler(logging.StreamHandler(self.stream))
        self.logger = PluginLogger(self.base_logger)

    def test_info_writes_event_and_safe_context(self) -> None:
        self.logger.info(
            "install.started",
            operation_id="op-123",
            game_id="123456",
            mod_id="dw3re",
            phase="download",
        )

        self.assertIn(
            "event=install.started operation_id=op-123 game_id=123456 "
            "mod_id=dw3re phase=download",
            self.stream.getvalue(),
        )

    def test_omits_unspecified_context(self) -> None:
        self.logger.warning("library.unavailable")

        self.assertEqual("event=library.unavailable\n", self.stream.getvalue())

    def test_rejects_multiline_event(self) -> None:
        with self.assertRaises(ValueError):
            self.logger.info("scan.failed\nsecret=value")

    def test_rejects_unstructured_context(self) -> None:
        with self.assertRaises(ValueError):
            self.logger.info("launch_options.updated", phase='WINEDLLOVERRIDES="dxgi=n,b"')

    def test_exception_includes_traceback(self) -> None:
        try:
            raise RuntimeError("expected test failure")
        except RuntimeError:
            self.logger.exception("operation.failed", phase="install")

        output = self.stream.getvalue()
        self.assertIn("event=operation.failed phase=install", output)
        self.assertIn("RuntimeError: expected test failure", output)


if __name__ == "__main__":
    unittest.main()
