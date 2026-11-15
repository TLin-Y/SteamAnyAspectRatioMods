import io
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def zip_bytes(entries: dict[str, bytes | str]) -> bytes:
    content = io.BytesIO()
    with ZipFile(content, "w", ZIP_DEFLATED) as archive:
        for name, value in entries.items():
            archive.writestr(name, value)
    return content.getvalue()


def write_zip(path: Path, entries: dict[str, bytes | str]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(zip_bytes(entries))
    return path
