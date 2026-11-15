import re
import socket
import ssl
from email.message import Message
from pathlib import Path
from unittest.mock import Mock, patch
from urllib.error import HTTPError, URLError

from support.fakes import failing, no_network, offline, serving
from support.sandbox import SandboxTest

from utils import network

URL = "https://example.com/catalog.json"


class NetworkTests(SandboxTest):
    def test_downloads_write_files_and_staged_copies(self) -> None:
        with serving(b"payload"):
            self.assertEqual(7, network.download(URL, self.root / "a.bin", 10, "Download"))
            staged, size = network.download_staged(URL, self.root, 10, "Download", suffix=".zip")
            with network.download_buffered(URL, 10, "Download") as buffer:
                self.assertEqual(b"payload", buffer.read())
        self.assertEqual((7, ".zip", b"payload"), (size, staged.suffix, staged.read_bytes()))
        self.assertEqual(b"payload", (self.root / "a.bin").read_bytes())

    def test_oversized_download_leaves_no_staged_file(self) -> None:
        with serving(b"x" * 11), self.assertRaisesRegex(ValueError, "exceeds"):
            network.download_staged(URL, self.root, 10, "Download")
        self.assertEqual([], list(self.root.iterdir()))

    def test_failures_become_network_errors_described_for_the_user(self) -> None:
        cases = (
            (HTTPError(URL, 404, "Not Found", Message(), None), "example.com HTTP 404. The file was not found."),
            (HTTPError(URL, 500, "Server Error", Message(), None), "example.com HTTP 500."),
            (URLError(TimeoutError()), "Timed out while contacting example.com. Check your internet connection."),
            (URLError(socket.gaierror(-2, "unknown")), "No internet connection: could not find example.com."),
            (ssl.SSLCertVerificationError("bad"), "A secure connection to example.com could not be made."),
            (URLError("refused"), "Could not reach example.com: refused."),
            (OSError(), "Could not reach example.com."),
        )
        for error, message in cases:
            with self.subTest(message=message):
                self.assertEqual(message, network.describe_network_error(error, URL))
                with failing(error), self.assertRaisesRegex(network.NetworkError, f"^{re.escape(message)}$"):
                    network.open_url(URL)

    def test_offline_mode_blocks_every_request_until_turned_off(self) -> None:
        with offline(), no_network():
            self.assertTrue(network.is_offline_mode())
            with self.assertRaises(network.OfflineModeError):
                network.download(URL, self.root / "a.bin", 10, "Download")
        with serving(b"x") as opener:
            network.open_url(URL)
        opener.assert_called_once()

    def test_ssl_context_uses_an_available_system_ca_bundle(self) -> None:
        expected = Mock(spec=ssl.SSLContext)
        with (
            patch("utils.network.SYSTEM_CA_BUNDLE_PATHS", (Path(__file__),)),
            patch("utils.network.ssl.create_default_context", return_value=expected) as create_context,
        ):
            self.assertIs(expected, network.create_ssl_context())
        create_context.assert_called_once_with(cafile=str(Path(__file__)))
