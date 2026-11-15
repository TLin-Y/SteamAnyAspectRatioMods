import io
from collections.abc import Callable, Generator
from contextlib import contextmanager
from unittest.mock import Mock, patch

from utils import network

RAW = "https://raw.githubusercontent.com/example/mods/main"

Content = bytes | Callable[[str], bytes]


@contextmanager
def serving(content: Content) -> Generator[Mock, None, None]:
    """Every download returns content (or content(url)); yields the urlopen mock to inspect the requests."""

    def respond(url: str, **_: object) -> io.BytesIO:
        return io.BytesIO(content(url) if callable(content) else content)

    with patch("utils.network.urlopen", side_effect=respond) as opener:
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
