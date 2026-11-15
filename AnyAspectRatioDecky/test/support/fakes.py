import io
import os
from collections.abc import Callable, Generator
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import Mock, patch

from support.builders import RAW
from utils import network

Content = bytes | dict[str, bytes] | Callable[[str], bytes]
"""What a download returns: the same bytes, bytes per repository path, or bytes per URL."""


def response_body(content: Content, url: str) -> bytes:
    match content:
        case bytes():
            return content
        case dict():
            return content[url.removeprefix(f"{RAW}/")]
        case _:
            return content(url)


@contextmanager
def serving(content: Content) -> Generator[Mock, None, None]:
    """Downloads return content; yields the urlopen mock to inspect the requests."""
    with patch("utils.network.urlopen", side_effect=lambda url, **_: io.BytesIO(response_body(content, url))) as opener:
        yield opener


@contextmanager
def failing(error: BaseException) -> Generator[Mock, None, None]:
    with patch("utils.network.urlopen", side_effect=error) as opener:
        yield opener


@contextmanager
def no_network() -> Generator[Mock, None, None]:
    with failing(AssertionError("network used")) as opener:
        yield opener


@contextmanager
def offline() -> Generator[None, None, None]:
    network.set_offline_mode(True)
    try:
        yield
    finally:
        network.set_offline_mode(False)


def fetched(opener: Mock) -> list[str]:
    return [call.args[0] for call in opener.call_args_list]


@contextmanager
def replace_fails(name: str | None, message: str) -> Generator[None, None, None]:
    """Moving a file onto `name` (any file for None) raises PermissionError(message)."""
    real_replace = os.replace

    def guarded(source: Path, target: Path) -> None:
        if name is None or Path(target).name == name:
            raise PermissionError(message)
        real_replace(source, target)

    with patch("utils.files.os.replace", side_effect=guarded):
        yield


@contextmanager
def unlink_fails(name: str, message: str) -> Generator[None, None, None]:
    real_unlink = Path.unlink

    def guarded(path: Path, *args: object, **kwargs: object) -> None:
        if path.name == name:
            raise PermissionError(message)
        real_unlink(path, *args, **kwargs)

    with patch.object(Path, "unlink", autospec=True, side_effect=guarded):
        yield


@contextmanager
def symlinked(name: str) -> Generator[None, None, None]:
    """Paths named `name` look like symbolic links."""
    with patch.object(Path, "is_symlink", autospec=True, side_effect=lambda path: path.name == name):
        yield
