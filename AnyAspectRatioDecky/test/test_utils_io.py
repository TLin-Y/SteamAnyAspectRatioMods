import io
import socket
import ssl
import tempfile
import unittest
from email.message import Message
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError, URLError
from zipfile import ZipFile

from utils import files, folders, ini, network, sizes, streams, zips


class FilesTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()

    def leftovers(self) -> list[Path]:
        return list(self.root.rglob(f"{files.TEMPORARY_PREFIX}*"))

    def test_size_limits_name_the_limit(self) -> None:
        self.assertEqual("256 KiB", sizes.label(256 * sizes.KIB))
        self.assertEqual("64 MiB", sizes.label(64 * sizes.MIB))
        self.assertEqual("2 GiB", sizes.label(2 * sizes.GIB + 1))
        with self.assertRaisesRegex(ValueError, "^Mod INI exceeds the 1 KiB limit$"):
            streams.read_limited(io.BytesIO(b"x" * 1025), 1024, "Mod INI")
        self.assertEqual(b"x" * 1024, streams.read_limited(io.BytesIO(b"x" * 1024), 1024, "Mod INI"))

    def test_write_bytes_atomic_creates_folders_and_leaves_no_temporary_file(self) -> None:
        target = self.root / "a" / "b" / "file.bin"
        self.assertTrue(files.write_bytes_atomic(target, b"new"))
        self.assertEqual(b"new", target.read_bytes())
        self.assertEqual([], self.leftovers())

    def test_failed_check_before_replace_keeps_the_original(self) -> None:
        target = self.root / "file.ini"
        target.write_bytes(b"original")

        def changed() -> None:
            raise ValueError("changed meanwhile")

        with self.assertRaisesRegex(ValueError, "changed meanwhile"):
            files.write_bytes_atomic(target, b"new", before_replace=changed)
        self.assertEqual(b"original", target.read_bytes())
        self.assertEqual([], self.leftovers())

    def test_keep_mode_reports_when_permissions_cannot_be_set(self) -> None:
        target = self.root / "file.ini"
        target.write_bytes(b"original")
        with patch("utils.files.os.chmod", side_effect=PermissionError("denied")):
            self.assertFalse(files.write_bytes_atomic(target, b"new", keep_mode=True))
        self.assertEqual(b"new", target.read_bytes())

    def test_stage_file_copies_into_a_temporary_file(self) -> None:
        source = self.root / "backup.bin"
        source.write_bytes(b"data")
        staged = files.stage_file(source, self.root / "out")
        self.assertTrue(staged.name.startswith(files.TEMPORARY_PREFIX))
        self.assertEqual(b"data", staged.read_bytes())

    def test_resolve_inside_never_leaves_the_folder(self) -> None:
        self.assertEqual(self.root / "bin" / "mod.asi", folders.resolve_inside(self.root, "bin/mod.asi"))
        for unsafe in ("../x", "/etc/passwd", "a/../../x", "a\\b", ""):
            with self.subTest(path=unsafe), self.assertRaisesRegex(ValueError, "Unsafe path"):
                folders.resolve_inside(self.root, unsafe)

    def test_walk_files_skips_hidden_entries_and_stops_at_the_depth(self) -> None:
        for name in ("top.zip", ".hidden.zip", "one/a.zip", "one/two/b.zip", "one/two/three/c.zip", ".git/d.zip"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"z")
        found = [path.relative_to(self.root).as_posix() for path in folders.walk_files(self.root, 2)]
        self.assertEqual(["top.zip", "one/a.zip", "one/two/b.zip"], found)

    def test_folder_stats(self) -> None:
        self.assertEqual((0, 0), folders.stats(self.root / "missing"))
        (self.root / "sub").mkdir()
        (self.root / "a.bin").write_bytes(b"12")
        (self.root / "sub" / "b.bin").write_bytes(b"345")
        self.assertEqual((2, 5), folders.stats(self.root))


class IniTests(unittest.TestCase):
    def test_values_keep_their_position_after_a_bom_and_before_a_comment(self) -> None:
        lines = b"\xef\xbb\xbf[ Resolution ]\r\n  Width =\t1280.0  ; pixels\r\n1Bad=1\nHeight=800\r".splitlines(keepends=True)
        sections = ini.values_by_section(lines)
        self.assertEqual([b"resolution"], list(sections))
        self.assertEqual({b"width", b"height"}, set(sections[b"resolution"]))
        (width,) = sections[b"resolution"][b"width"]
        (height,) = sections[b"resolution"][b"height"]
        self.assertEqual((b"1280.0", b"800"), (width.read(lines), height.read(lines)))

    def test_number_and_resolution_formats(self) -> None:
        for value, expected in ((b"1280", True), (b"16.00", True), (b"", False), (b".5", False), (b"5.", False),
                                (b"1.2.3", False), (b"-5", False), ("١".encode(), False)):
            with self.subTest(value=value):
                self.assertIs(expected, ini.is_decimal(value))
        for value, expected in ((b"1280x800", b""), (b'"1280x800"', b'"'), (b"'1920x1080'", b"'"),
                                (b'"1280x800', None), (b"0x800", None), (b"1280X800", None), (b"auto", None)):
            with self.subTest(value=value):
                self.assertEqual(expected, ini.resolution_quote(value))


class ZipsTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.archive = self.root / "mod.zip"
        with ZipFile(self.archive, "w") as archive:
            archive.writestr("bin/", b"")
            archive.writestr("bin/mod.asi", b"asi")
            archive.writestr("__MACOSX/._mod.asi", b"junk")
            archive.writestr("Thumbs.db", b"junk")

    def test_member_names_drop_folders_and_os_junk(self) -> None:
        self.assertEqual(["bin/mod.asi"], zips.file_names(self.archive))

    def test_damaged_zip_raises_the_callers_message(self) -> None:
        broken = self.root / "broken.zip"
        broken.write_bytes(b"not a zip")
        self.assertFalse(zips.is_zip(broken))
        with self.assertRaisesRegex(ValueError, "^Mod archive is invalid$"):
            with zips.open_zip(broken, "Mod archive is invalid"):
                pass

    def test_reading_and_extracting_respect_the_limit(self) -> None:
        with zips.open_zip(self.archive, "invalid") as archive:
            info = archive.getinfo("bin/mod.asi")
            self.assertEqual(b"asi", zips.read_file(archive, info, 3, "Mod file"))
            with self.assertRaisesRegex(ValueError, "exceeds"):
                zips.read_file(archive, info, 2, "Mod file")
            target = self.root / "out" / "mod.asi"
            self.assertEqual(3, zips.extract_file(archive, info, target, 3, "Mod file"))
        self.assertEqual(b"asi", target.read_bytes())


class NetworkTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.addCleanup(network.set_offline_mode, False)

    def test_downloads_write_files_and_staged_copies(self) -> None:
        with patch("utils.network.urlopen", side_effect=lambda *args, **kwargs: io.BytesIO(b"payload")):
            self.assertEqual(7, network.download("https://example.com/a", self.root / "a.bin", 10, "Download"))
            staged, size = network.download_staged("https://example.com/b", self.root, 10, "Download", suffix=".zip")
            with network.download_buffered("https://example.com/c", 10, "Download") as buffer:
                self.assertEqual(b"payload", buffer.read())
        self.assertEqual(b"payload", (self.root / "a.bin").read_bytes())
        self.assertEqual((7, ".zip"), (size, staged.suffix))

    def test_network_errors_are_described_for_the_user(self) -> None:
        url = "https://example.com/catalog.json"
        cases = (
            (HTTPError(url, 404, "Not Found", Message(), None), "example.com HTTP 404. The file was not found."),
            (HTTPError(url, 500, "Server Error", Message(), None), "example.com HTTP 500."),
            (URLError(TimeoutError()), "Timed out while contacting example.com. Check your internet connection."),
            (URLError(socket.gaierror(-2, "unknown")), "No internet connection: could not find example.com."),
            (ssl.SSLCertVerificationError("bad"), "A secure connection to example.com could not be made."),
            (URLError("refused"), "Could not reach example.com: refused."),
            (OSError(), "Could not reach example.com."),
        )
        for error, message in cases:
            with self.subTest(error=error):
                self.assertEqual(message, network.describe_network_error(error, url))

    def test_offline_mode_blocks_every_request(self) -> None:
        network.set_offline_mode(True)
        self.assertTrue(network.is_offline_mode())
        with patch("utils.network.urlopen", side_effect=AssertionError("network used")):
            with self.assertRaises(network.OfflineModeError):
                network.download("https://example.com/a", self.root / "a.bin", 10, "Download")

    def test_failed_download_leaves_no_staged_file(self) -> None:
        with patch("utils.network.urlopen", side_effect=lambda *args, **kwargs: io.BytesIO(b"x" * 11)):
            with self.assertRaisesRegex(ValueError, "exceeds"):
                network.download_staged("https://example.com/a", self.root, 10, "Download")
        self.assertEqual([], list(self.root.iterdir()))


if __name__ == "__main__":
    unittest.main()
