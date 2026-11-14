from collections.abc import Generator
from contextlib import contextmanager
from pathlib import Path
from zipfile import BadZipFile, ZipFile, ZipInfo, is_zipfile

from helper.errors import PluginError
from helper.validator import Validator
from utils import files, sizes, streams


def is_zip(path: Path) -> bool:
    return is_zipfile(path)


@contextmanager
def open_zip(path: Path, invalid_message: str) -> Generator[ZipFile, None, None]:
    try:
        with ZipFile(path) as archive:
            yield archive
    except BadZipFile as error:
        raise PluginError(invalid_message) from error


def file_names(path: Path) -> list[str]:
    with ZipFile(path) as archive:
        return [
            info.filename
            for info in archive.infolist()
            if not info.is_dir() and not Validator.Files.is_os_junk(info.filename)
        ]


def read_file(archive: ZipFile, info: ZipInfo, max_bytes: int, description: str) -> bytes:
    if info.file_size > max_bytes:
        raise PluginError(f"{description} exceeds the {sizes.label(max_bytes)} limit")
    with archive.open(info) as stream:
        return streams.read_limited(stream, max_bytes, description)

# For safer async IO
def stage_file(archive: ZipFile, info: ZipInfo, directory: Path, max_bytes: int, description: str) -> tuple[Path, int]:
    """Extract a file to a new tmp file in directory. The caller moves or deletes it."""
    with archive.open(info) as stream:
        return files.stage_stream(stream, directory, max_bytes, description)

def extract_file(archive: ZipFile, info: ZipInfo, path: Path, max_bytes: int, description: str) -> int:
    with archive.open(info) as stream:
        return files.write_stream(stream, path, max_bytes, description)
