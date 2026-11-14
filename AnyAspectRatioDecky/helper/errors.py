from collections.abc import Generator
from contextlib import contextmanager

# What a broken file, folder, download or input raises; anything else is a bug.
FAILURES = (OSError, ValueError)


class PluginError(ValueError):
    """An expected failure. Its message is written for the user, so it is logged without a traceback.

    Plugin methods raise it as-is: Decky passes str(error) to the frontend, which shows it.
    """


@contextmanager
def context(message: str) -> Generator[None, None, None]:
    """Re-raise any failure inside as PluginError("<message>: <error>")."""
    try:
        yield
    except FAILURES as error:
        raise PluginError(f"{message}: {error}") from error


@contextmanager
def collect(warnings: list[str], message: str) -> Generator[None, None, None]:
    """Record any failure inside as the warning "<message>: <error>" and continue after the block."""
    try:
        yield
    except FAILURES as error:
        warnings.append(f"{message}: {error}")
