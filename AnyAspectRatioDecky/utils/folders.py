import os
import shutil
import tempfile
from collections.abc import Generator, Iterator
from contextlib import contextmanager
from pathlib import Path, PurePosixPath

from helper.errors import PluginError
from helper.validator import Validator
from utils import files


def resolve_inside(root: Path, relative_path: str) -> Path:
    if not Validator.Paths.is_relative(relative_path):
        raise PluginError(f"Unsafe path: {relative_path}")
    parts = PurePosixPath(relative_path).parts
    target = root.joinpath(*parts)
    if target.is_symlink():
        raise PluginError(f"Path is a symbolic link: {relative_path}")
    current = root
    for part in parts[:-1]:
        current = current / part
        if current.is_symlink():
            raise PluginError(f"Path traverses a symbolic link: {relative_path}")
    try:
        target.parent.resolve(strict=False).relative_to(root)
    except ValueError as error:
        raise PluginError(f"Path escapes its folder: {relative_path}") from error
    return target


def remove_empty_parents(paths: list[Path], root: Path) -> None:
    """Delete each path's now-empty parent folders, up to (not including) root."""
    for path in paths:
        parent = path.parent
        while parent != root:
            try:
                parent.rmdir()
            except OSError:
                break
            parent = parent.parent


def walk_files(root: Path, max_depth: int) -> Iterator[Path]:
    base_depth = len(root.parts)
    for current, dirnames, filenames in os.walk(root):
        current_path = Path(current)
        if len(current_path.parts) - base_depth >= max_depth:
            dirnames[:] = []
        dirnames[:] = sorted(name for name in dirnames if not Validator.Files.is_hidden(name))
        for filename in sorted(filenames):
            if not Validator.Files.is_hidden(filename):
                yield current_path / filename


def stats(root: Path, ignore_errors: bool = False) -> tuple[int, int]:
    if not root.is_dir():
        return 0, 0

    def raise_walk_error(error: OSError) -> None:
        raise error

    count = total = 0
    for current, _, filenames in os.walk(root, onerror=None if ignore_errors else raise_walk_error):
        for name in filenames:
            path = Path(current) / name
            try:
                if not path.is_symlink():
                    total += path.stat().st_size
                    count += 1
            except OSError:
                if not ignore_errors:
                    raise
    return count, total


def remove_tree(path: Path) -> None:
    shutil.rmtree(path, ignore_errors=True)


@contextmanager
def scratch(path: Path) -> Generator[Path, None, None]:
    """An empty folder at a fixed path, deleted on exit unless moved away."""
    remove_tree(path)
    try:
        yield path
    finally:
        remove_tree(path)


@contextmanager
def temporary(parent: Path | None = None) -> Generator[Path, None, None]:
    if parent is not None:
        parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=files.TEMPORARY_PREFIX, dir=parent) as folder:
        yield Path(folder)
