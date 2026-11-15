import logging
from collections.abc import Generator
from contextlib import contextmanager
from logging.handlers import RotatingFileHandler
from pathlib import Path

from helper import PLUGIN_NAME
from helper.errors import FAILURES, PluginError
from utils import sizes, text_files

# Must be bounded, the log I/O will cause performance issues if too large in DroidDeck
LOG_FILE_MAX_BYTES = 512 * sizes.KIB # 512 KB based on my RP Nova testing, SD OLED no such issue.
LOG_FILE_BACKUPS = 2
LOG_FILE_FORMAT = "%(asctime)s %(levelname)s %(message)s"

# Control whole plugin log print.
class PluginLogger:
    def __init__(self) -> None:
        self._handler: RotatingFileHandler | None = None
        self.bind(logging.getLogger(PLUGIN_NAME))

    def bind(self, base: logging.Logger) -> None:
        self._base = base
        self.debug = base.debug
        self.info = base.info
        self.warning = base.warning

    def error(self, message: str, error: BaseException | None = None) -> None:
        self._base.error(message, exc_info=error)

    @contextmanager
    def on_error(self, message: str) -> Generator[None, None, None]:
        try:
            yield
        except PluginError as error:
            self.warning("%s: %s", message, error)
            raise
        except FAILURES as error:
            self.error(message, error)
            raise
        except Exception as error:
            self.error(message, error)
            detail = str(error).strip()
            raise PluginError(type(error).__name__ + (f": {detail}" if detail else "")) from error

    def attach_file(self, path: Path) -> None:
        if self._handler is not None:
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        self._handler = RotatingFileHandler(
            path, maxBytes=LOG_FILE_MAX_BYTES, backupCount=LOG_FILE_BACKUPS, encoding=text_files.TEXT_ENCODING
        )
        self._handler.setFormatter(logging.Formatter(LOG_FILE_FORMAT))
        self._base.addHandler(self._handler)

    def read_tail(self, max_lines: int) -> str:
        content, _ = text_files.read_tail(Path(self._file().baseFilename), LOG_FILE_MAX_BYTES)
        return "\n".join(content.splitlines()[-max_lines:])

    def clear_files(self) -> None:
        handler = self._file()
        handler.acquire()
        try:
            handler.flush()
            if handler.stream is not None:
                handler.stream.close()
                handler.stream = None
            path = Path(handler.baseFilename)
            path.unlink(missing_ok=True)
            for index in range(1, handler.backupCount + 1):
                Path(f"{path}.{index}").unlink(missing_ok=True)
        finally:
            handler.release()

    def close(self) -> None:
        if self._handler is not None:
            self._base.removeHandler(self._handler)
            self._handler.close()
            self._handler = None

    def _file(self) -> RotatingFileHandler:
        if self._handler is None:
            raise PluginError("Plugin log file is not open")
        return self._handler


log = PluginLogger()
