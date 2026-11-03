import io
import logging
import unittest

from plugin_logger import PluginLogger


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
