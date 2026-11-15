import hashlib
import io
from unittest.mock import patch

from support.sandbox import SandboxTest

from utils import files, folders, sizes, streams


class FilesTests(SandboxTest):
    def test_size_limits_name_the_limit(self) -> None:
        self.assertEqual(["256 KiB", "64 MiB", "2 GiB"],
                         [sizes.label(256 * sizes.KIB), sizes.label(64 * sizes.MIB), sizes.label(2 * sizes.GIB + 1)])
        self.assertEqual(b"x" * 1024, streams.read_limited(io.BytesIO(b"x" * 1024), 1024, "Mod INI"))
        with self.assertRaisesRegex(ValueError, "^Mod INI exceeds the 1 KiB limit$"):
            streams.read_limited(io.BytesIO(b"x" * 1025), 1024, "Mod INI")

    def test_unchanged_file_is_hashed_once(self) -> None:
        path = self.root / "archive.zip"
        path.write_bytes(b"one")
        with patch("utils.files.sha256", wraps=files.sha256) as hashing:
            self.assertTrue(files.matches_sha256(path, hashlib.sha256(b"one").hexdigest()))
            self.assertFalse(files.matches_sha256(path, "0" * 64))
            self.assertEqual(1, hashing.call_count)
            path.write_bytes(b"two!")
            self.assertTrue(files.matches_sha256(path, hashlib.sha256(b"two!").hexdigest()))
            self.assertEqual(2, hashing.call_count)

    def test_atomic_write_replaces_in_one_step_or_not_at_all(self) -> None:
        self.assertTrue(files.write_bytes_atomic(self.root / "a/b/new.bin", b"new"))
        self.write({"file.ini": "original"})

        def changed() -> None:
            raise ValueError("changed meanwhile")

        with self.assertRaisesRegex(ValueError, "changed meanwhile"):
            files.write_bytes_atomic(self.root / "file.ini", b"new", before_replace=changed)
        self.assert_files({"a/b/new.bin": "new", "file.ini": "original"})

        with patch("utils.files.os.chmod", side_effect=PermissionError("denied")):
            self.assertFalse(files.write_bytes_atomic(self.root / "file.ini", b"new", keep_mode=True))
        self.assert_files({"file.ini": "new"})

    def test_stage_file_copies_into_a_temporary_file(self) -> None:
        self.write({"backup.bin": "data"})
        staged = files.stage_file(self.root / "backup.bin", self.root / "out")
        self.assertTrue(staged.name.startswith(files.TEMPORARY_PREFIX))
        self.assertEqual(b"data", staged.read_bytes())


class FoldersTests(SandboxTest):
    def test_resolve_inside_never_leaves_the_folder(self) -> None:
        self.assertEqual(self.root / "bin" / "mod.asi", folders.resolve_inside(self.root, "bin/mod.asi"))
        for unsafe in ("../x", "/etc/passwd", "a/../../x", "a\\b", ""):
            with self.subTest(path=unsafe), self.assertRaisesRegex(ValueError, "Unsafe path"):
                folders.resolve_inside(self.root, unsafe)

    def test_walk_files_skips_hidden_entries_and_stops_at_the_depth(self) -> None:
        names = ("top.zip", ".hidden.zip", "one/a.zip", "one/two/b.zip", "one/two/three/c.zip", ".git/d.zip")
        self.write(dict.fromkeys(names, "z"))
        found = [path.relative_to(self.root).as_posix() for path in folders.walk_files(self.root, 2)]
        self.assertEqual(["top.zip", "one/a.zip", "one/two/b.zip"], found)

    def test_stats_count_files_and_bytes(self) -> None:
        self.assertEqual((0, 0), folders.stats(self.root / "missing"))
        self.write({"a.bin": "12", "sub/b.bin": "345"})
        self.assertEqual((2, 5), folders.stats(self.root))
