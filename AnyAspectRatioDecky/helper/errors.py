from collections.abc import Generator
from contextlib import contextmanager

# What a broken file, folder, download or input raises, anything else is a code bug.
FAILURES = (OSError, ValueError)


class PluginError(ValueError):
    """General class for plugin failure."""


@contextmanager
def wrap(message: str) -> Generator[None, None, None]:
    """Wrap any failure inside PluginError, keeping the original as its cause."""
    try:
        yield
    except FAILURES as error:
        raise PluginError(f"{message}: {error}") from error


@contextmanager
def collect(warnings: list[str], message: str) -> Generator[None, None, None]:
    try:
        yield
    except FAILURES as error:
        warnings.append(f"{message}: {error}")
