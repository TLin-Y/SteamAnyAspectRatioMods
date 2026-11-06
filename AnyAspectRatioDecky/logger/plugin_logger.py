import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path

from helper.constants import (
    LOG_CONTEXT_FIELDS,
    LOG_CONTEXT_FIELD_FORMAT,
    LOG_CONTEXT_VALUE_PATTERN,
    LOG_EVENT_FIELD_FORMAT,
    LOG_INVALID_CONTEXT_MESSAGE,
    LOG_INVALID_EVENT_MESSAGE,
    LOG_EVENT_PATTERN,
)

LOG_FILE_MAX_BYTES = 512 * 1024
LOG_FILE_BACKUPS = 2
LOG_FILE_FORMAT = "%(asctime)s %(levelname)s %(message)s"


def attach_file_handler(logger: logging.Logger, path: Path) -> RotatingFileHandler:
    path.parent.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(
        path,
        maxBytes=LOG_FILE_MAX_BYTES,
        backupCount=LOG_FILE_BACKUPS,
        encoding="utf-8",
    )
    handler.setFormatter(logging.Formatter(LOG_FILE_FORMAT))
    logger.addHandler(handler)
    return handler


def clear_file_handler_logs(handler: RotatingFileHandler) -> None:
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


class PluginLogger:
    def __init__(self, logger: logging.Logger) -> None:
        self._logger = logger

    def info(
        self,
        event: str,
        *,
        operation_id: str | None = None,
        game_id: str | None = None,
        mod_id: str | None = None,
        phase: str | None = None,
    ) -> None:
        self._write(logging.INFO, event, operation_id, game_id, mod_id, phase)

    def warning(
        self,
        event: str,
        *,
        operation_id: str | None = None,
        game_id: str | None = None,
        mod_id: str | None = None,
        phase: str | None = None,
    ) -> None:
        self._write(logging.WARNING, event, operation_id, game_id, mod_id, phase)

    def exception(
        self,
        event: str,
        *,
        operation_id: str | None = None,
        game_id: str | None = None,
        mod_id: str | None = None,
        phase: str | None = None,
    ) -> None:
        self._write(logging.ERROR, event, operation_id, game_id, mod_id, phase, exc_info=True)

    def _write(
        self,
        level: int,
        event: str,
        operation_id: str | None,
        game_id: str | None,
        mod_id: str | None,
        phase: str | None,
        *,
        exc_info: bool = False,
    ) -> None:
        if not LOG_EVENT_PATTERN.fullmatch(event):
            raise ValueError(LOG_INVALID_EVENT_MESSAGE)

        values = (operation_id, game_id, mod_id, phase)
        fields = [LOG_EVENT_FIELD_FORMAT.format(event=event)]
        for key, value in zip(LOG_CONTEXT_FIELDS, values):
            if value is not None:
                if not LOG_CONTEXT_VALUE_PATTERN.fullmatch(value):
                    raise ValueError(LOG_INVALID_CONTEXT_MESSAGE.format(key=key))
                fields.append(LOG_CONTEXT_FIELD_FORMAT.format(key=key, value=value))

        message = " ".join(fields)
        if exc_info:
            self._logger.exception(message)
        else:
            self._logger.log(level, message)
