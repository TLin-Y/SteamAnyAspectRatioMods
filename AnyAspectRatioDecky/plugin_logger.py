import logging
import re
from typing import Optional


_EVENT_PATTERN = re.compile(r"[a-z][a-z0-9_.-]{0,79}\Z")
_VALUE_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}\Z")


class PluginLogger:
    def __init__(self, logger: logging.Logger) -> None:
        self._logger = logger

    def info(
        self,
        event: str,
        *,
        operation_id: Optional[str] = None,
        game_id: Optional[str] = None,
        mod_id: Optional[str] = None,
        phase: Optional[str] = None,
    ) -> None:
        self._write(logging.INFO, event, operation_id, game_id, mod_id, phase)

    def warning(
        self,
        event: str,
        *,
        operation_id: Optional[str] = None,
        game_id: Optional[str] = None,
        mod_id: Optional[str] = None,
        phase: Optional[str] = None,
    ) -> None:
        self._write(logging.WARNING, event, operation_id, game_id, mod_id, phase)

    def exception(
        self,
        event: str,
        *,
        operation_id: Optional[str] = None,
        game_id: Optional[str] = None,
        mod_id: Optional[str] = None,
        phase: Optional[str] = None,
    ) -> None:
        self._write(logging.ERROR, event, operation_id, game_id, mod_id, phase, exc_info=True)

    def _write(
        self,
        level: int,
        event: str,
        operation_id: Optional[str],
        game_id: Optional[str],
        mod_id: Optional[str],
        phase: Optional[str],
        *,
        exc_info: bool = False,
    ) -> None:
        if not _EVENT_PATTERN.fullmatch(event):
            raise ValueError("Log event must be a lowercase event identifier")

        fields = [f"event={event}"]
        for key, value in (
            ("operation_id", operation_id),
            ("game_id", game_id),
            ("mod_id", mod_id),
            ("phase", phase),
        ):
            if value is not None:
                if not _VALUE_PATTERN.fullmatch(value):
                    raise ValueError(f"Invalid log context value for {key}")
                fields.append(f"{key}={value}")

        message = " ".join(fields)
        if exc_info:
            self._logger.exception(message)
        else:
            self._logger.log(level, message)
