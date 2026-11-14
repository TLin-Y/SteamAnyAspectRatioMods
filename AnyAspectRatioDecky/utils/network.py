import socket
import ssl
import tempfile
from collections.abc import Generator
from contextlib import contextmanager
from http.client import IncompleteRead
from pathlib import Path
from typing import BinaryIO
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import urlopen

from helper.errors import PluginError
from utils import files, streams

# This plugin is Linux based (Steam OS), so it's okish to hardcode CA paths.
# TODO: Make this configurable once we have more use cases.
DOWNLOAD_TIMEOUT_SECONDS = 30
SYSTEM_CA_BUNDLE_PATHS = (
    Path("/etc/ssl/certs/ca-certificates.crt"),
    Path("/etc/ssl/cert.pem"),
)

class NetworkError(PluginError, OSError):
    """Any failed internet access."""

class OfflineModeError(NetworkError):
    """Any network access while offline mode is on."""

_offline_mode = False

def set_offline_mode(enabled: bool) -> None:
    global _offline_mode
    _offline_mode = enabled

def is_offline_mode() -> bool:
    return _offline_mode

def create_ssl_context() -> ssl.SSLContext:
    for ca_bundle in SYSTEM_CA_BUNDLE_PATHS:
        if ca_bundle.is_file():
            return ssl.create_default_context(cafile=str(ca_bundle))
    return ssl.create_default_context()

# I don't want cover all possible network errors, just be nice on few most common ones.
def describe_network_error(error: BaseException, url: str) -> str:
    host = urlparse(url).hostname or "the server"
    match error:
        case HTTPError(code=404):
            return f"{host} HTTP 404. The file was not found."
        case HTTPError(code=code):
            return f"{host} HTTP {code}."
        case URLError(reason=BaseException() as reason):
            return describe_network_error(reason, url)
        case TimeoutError():
            return f"Timed out while contacting {host}. Check your internet connection."
        case socket.gaierror():
            return f"No internet connection: could not find {host}."
        case ssl.SSLError():
            return f"A secure connection to {host} could not be made."
        case _:
            detail = str(getattr(error, "reason", error)).strip()
            return f"Could not reach {host}" + (f": {detail}." if detail else ".")

class _GuardedResponse:
    """Turns connection failures while reading a download into NetworkError."""

    def __init__(self, response, url: str) -> None:
        self._response = response
        self._url = url
        self._stream = None

    def __enter__(self):
        self._stream = self._response.__enter__()
        return self

    def __exit__(self, *exc_info) -> bool | None:
        return self._response.__exit__(*exc_info)

    def read(self, size: int = -1) -> bytes:
        try:
            return self._stream.read(size)
        except (OSError, IncompleteRead) as error:
            raise NetworkError(describe_network_error(error, self._url)) from error


def open_url(url: str):
    if _offline_mode:
        raise OfflineModeError(
            "Offline mode is on, so nothing is downloaded. "
            "Import the offline pack on the Mods tab to install mods."
        )
    try:
        response = urlopen(url, timeout=DOWNLOAD_TIMEOUT_SECONDS, context=create_ssl_context())
    except OSError as error:
        raise NetworkError(describe_network_error(error, url)) from error
    return _GuardedResponse(response, url)


def read_url_limited(url: str, max_bytes: int, description: str) -> bytes:
    with open_url(url) as response:
        return streams.read_limited(response, max_bytes, description)


def download(url: str, path: Path, max_bytes: int, description: str) -> int:
    with open_url(url) as response:
        return files.write_stream(response, path, max_bytes, description)


def download_staged(
    url: str, directory: Path | None, max_bytes: int, description: str, suffix: str = ""
) -> tuple[Path, int]:
    """Download to a new temp file in dir (None: the system temp)"""
    with open_url(url) as response:
        return files.stage_stream(response, directory, max_bytes, description, suffix)


@contextmanager
def download_buffered(url: str, max_bytes: int, description: str) -> Generator[BinaryIO, None, None]:
    with tempfile.TemporaryFile() as buffer:
        with open_url(url) as response:
            streams.copy_limited(response, buffer, max_bytes, description)
        buffer.seek(0)
        yield buffer
