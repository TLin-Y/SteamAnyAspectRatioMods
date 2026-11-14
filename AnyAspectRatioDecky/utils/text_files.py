import json
import os
from pathlib import Path

from utils import files

TEXT_ENCODING = "utf-8"
BOM_TEXT_ENCODING = "utf-8-sig"


def load_json(path: Path) -> object:
    return json.loads(path.read_text(encoding=TEXT_ENCODING))


def write_json_atomic(path: Path, document: object, indent: int | None = 2) -> None:
    files.write_bytes_atomic(path, json.dumps(document, indent=indent).encode(TEXT_ENCODING))


def read_tail(path: Path, max_bytes: int) -> tuple[str, bool]:
    with path.open("rb") as stream:
        size = stream.seek(0, os.SEEK_END)
        stream.seek(max(0, size - max_bytes))
        content = stream.read(max_bytes)
    return content.decode(TEXT_ENCODING, errors="replace"), size > max_bytes
