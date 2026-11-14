"""Shared text and JSON file access, so every file the plugin reads or writes uses the same encoding."""

import json
import os
import tempfile
from pathlib import Path

TEXT_ENCODING = "utf-8"
# Also accepts a leading byte-order mark, which Windows editors often add to READMEs and Steam VDF files.
BOM_TEXT_ENCODING = "utf-8-sig"


def load_json(path: Path) -> object:
    return json.loads(path.read_text(encoding=TEXT_ENCODING))


def write_json_atomic(path: Path, document: object, indent: int | None = 2) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(dir=path.parent, suffix=".tmp")
    try:
        with os.fdopen(descriptor, "w", encoding=TEXT_ENCODING) as stream:
            json.dump(document, stream, indent=indent)
        os.replace(temporary_name, path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def read_text_tail(path: Path, max_bytes: int) -> tuple[str, bool]:
    """The last max_bytes of a text file, and whether earlier content was skipped."""
    with path.open("rb") as stream:
        size = stream.seek(0, os.SEEK_END)
        stream.seek(max(0, size - max_bytes))
        content = stream.read(max_bytes)
    return content.decode(TEXT_ENCODING, errors="replace"), size > max_bytes
