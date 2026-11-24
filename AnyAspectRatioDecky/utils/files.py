import hashlib
import io
import os
import shutil
import stat
import tempfile
from collections.abc import Callable
from contextlib import suppress
from pathlib import Path

from helper import PLUGIN_NAME
from utils import streams

# Every temp file the plugin creates starts with this, so a leftover is easy to recognise.
TEMPORARY_PREFIX = f".{PLUGIN_NAME.lower()}-"


def read_limited(path: Path, max_bytes: int, description: str) -> bytes:
    with path.open("rb") as stream:
        return streams.read_limited(stream, max_bytes, description)


def write_stream(stream, path: Path, max_bytes: int, description: str) -> int:
    """Write stream to path (parent folders are created). Returns the bytes written."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as target:
        return streams.copy_limited(stream, target, max_bytes, description)


# This is a must, follow the async safe IO style to support sandboxed SteamOS: DroidDeck
# First create a temporary file, after the real file is only ever replaced once.
def stage_stream(
    stream, directory: Path | None, max_bytes: int, description: str, suffix: str = ""
) -> tuple[Path, int]:
    if directory is not None:
        directory.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=TEMPORARY_PREFIX, suffix=suffix, dir=directory)
    staged = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as target:
            size = streams.copy_limited(stream, target, max_bytes, description)
    except BaseException:
        staged.unlink(missing_ok=True)
        raise
    return staged, size


def stage_bytes(directory: Path, content: bytes) -> Path:
    return stage_stream(io.BytesIO(content), directory, len(content), "Content")[0]


def stage_file(path: Path, directory: Path) -> Path:
    size = path.stat().st_size
    with path.open("rb") as stream:
        staged, copied = stage_stream(stream, directory, size, path.name)
    if copied != size:
        staged.unlink(missing_ok=True)
        raise OSError(f"{path.name} changed while being copied")
    return staged


# Async safe, must compatible with DroidDeck
def write_bytes_atomic(
    path: Path,
    content: bytes,
    *,
    keep_mode: bool = False,
    before_replace: Callable[[], None] | None = None,
) -> bool:
    """Replace path with content in one step (parent folders are created).

    keep_mode gives the new file the old file's chmod (644 for a new file).
    Returns False when keep_mode was asked but the permissions could not be set.
    """
    mode = stat.S_IMODE(path.stat().st_mode) if keep_mode and path.exists() else 0o644
    staged = stage_bytes(path.parent, content)
    mode_kept = True
    try:
        if keep_mode:
            try:
                os.chmod(staged, mode)
            except PermissionError:
                mode_kept = False
        if before_replace is not None:
            before_replace()
        os.replace(staged, path)
    finally:
        staged.unlink(missing_ok=True)
    return mode_kept


# Windows refuses to replace or delete a read-only file.
def _clear_read_only(path: Path) -> None:
    if os.name == "nt" and path.is_file():
        os.chmod(path, stat.S_IREAD | stat.S_IWRITE)


def delete(path: Path) -> None:
    _clear_read_only(path)
    path.unlink(missing_ok=True)


# Some filesystems (sandboxed SteamOS, exFAT) refuse chmod; the content matters more.
def copy_mode(source: Path, target: Path) -> None:
    with suppress(OSError):
        shutil.copymode(source, target)


def move(source: Path, target: Path) -> None:
    _clear_read_only(target)
    os.replace(source, target)


def copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def replace_with_backup(staged: Path, target: Path, backup: Path | None, action: str) -> None:
    renamed = False
    try:
        if backup is not None and target.is_file() and not backup.exists():
            target.rename(backup)
            renamed = True
        move(staged, target)
    except OSError as error:
        if renamed:
            try:
                backup.rename(target)
            except OSError as restore_error:
                raise OSError(
                    f"{action} failed ({error}) and restoration failed "
                    f"({restore_error}); restore {backup.name} manually"
                ) from restore_error
        raise


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(streams.CHUNK_BYTES), b""):
            digest.update(block)
    return digest.hexdigest()


# Mod archives are checked on every use; a file unchanged since its last check is not hashed again.
_known_hashes: dict[Path, tuple[tuple[int, int, int], str]] = {}


def matches_sha256(path: Path, expected: str) -> bool:
    status = path.stat()
    signature = (status.st_ino, status.st_size, status.st_mtime_ns)
    known = _known_hashes.get(path)
    if known is None or known[0] != signature:
        known = (signature, sha256(path))
        _known_hashes[path] = known
    return known[1] == expected


def equal(first: Path, second: Path) -> bool:
    if first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as left, second.open("rb") as right:
        while True:
            left_chunk = left.read(streams.CHUNK_BYTES)
            if left_chunk != right.read(streams.CHUNK_BYTES):
                return False
            if not left_chunk:
                return True
