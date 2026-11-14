import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path

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
