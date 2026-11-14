from collections.abc import Generator
from contextlib import contextmanager
from pathlib import Path
from zipfile import BadZipFile, ZipFile, ZipInfo, is_zipfile

from helper.validator import Validator
from utils import files, streams

__all__ = ["BadZipFile", "ZipFile", "ZipInfo", "is_zip", "member_names", "open_zip", "read_member",
           "stage_member", "extract_member"]


def is_zip(path: Path) -> bool:
    return is_zipfile(path)


@contextmanager
def open_zip(path: Path, invalid_message: str) -> Generator[ZipFile, None, None]:
    """Open a zip; a damaged zip, found now or while reading it, raises ValueError(invalid_message)."""
    try:
        with ZipFile(path) as archive:
            yield archive
    except BadZipFile as error:
        raise ValueError(invalid_message) from error


def member_names(path: Path) -> list[str]:
    """The files in a zip, without folders and the junk macOS and Windows add.

    Raises BadZipFile or OSError when the zip cannot be read.
    """
    with ZipFile(path) as archive:
        return [
            info.filename
            for info in archive.infolist()
            if not info.is_dir() and not Validator.Files.is_os_junk(info.filename)
        ]


def read_member(archive: ZipFile, info: ZipInfo, max_bytes: int, description: str) -> bytes:
    """A member's content; refuses one larger than max_bytes before or while reading."""
    if info.file_size > max_bytes:
        raise ValueError(f"{description} exceeds the {streams.size_label(max_bytes)} limit")
    with archive.open(info) as stream:
        return streams.read_limited(stream, max_bytes, description)


def stage_member(archive: ZipFile, info: ZipInfo, directory: Path, max_bytes: int, description: str) -> tuple[Path, int]:
    """Extract a member to a new temporary file in directory. The caller moves or deletes it."""
    with archive.open(info) as stream:
        return files.stage_stream(stream, directory, max_bytes, description)


def extract_member(archive: ZipFile, info: ZipInfo, path: Path, max_bytes: int, description: str) -> int:
    """Extract a member to path (parent folders are created). Returns the bytes written."""
    with archive.open(info) as stream:
        return files.write_stream(stream, path, max_bytes, description)
