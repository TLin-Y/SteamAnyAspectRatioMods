import tempfile
import unittest
from pathlib import Path

from store import mod_sources
from utils import files, network

PLUGIN_ROOT = Path(__file__).resolve().parents[2]

Tree = dict[str, bytes | str | None]
"""Relative path -> content; None means the file must not exist."""


def _content(value: bytes | str) -> bytes:
    return value.encode("utf-8") if isinstance(value, str) else value


class SandboxTest(unittest.TestCase):
    """Every test gets an empty self.root; plugin-wide state is reset after it."""

    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        self.addCleanup(network.set_offline_mode, False)
        self.addCleanup(mod_sources.set_pack_dir, None)
        self.addCleanup(mod_sources.set_archive_cache_dir, None)
        self.addCleanup(files._known_hashes.clear)

    def folder(self, relative: str) -> Path:
        path = self.root / relative
        path.mkdir(parents=True, exist_ok=True)
        return path

    def write(self, tree: Tree, root: Path | None = None) -> None:
        for relative, value in tree.items():
            path = (root or self.root) / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(_content(value))

    def assert_files(self, expected: Tree, root: Path | None = None) -> None:
        """Each file has the given content (or is absent for None), and no staged temp file was left behind."""
        base = root or self.root
        for relative, value in expected.items():
            with self.subTest(file=relative):
                path = base / relative
                if value is None:
                    self.assertFalse(path.exists(), f"{relative} should not exist")
                else:
                    self.assertEqual(_content(value), path.read_bytes())
        self.assert_no_leftovers(base)

    def assert_no_leftovers(self, root: Path | None = None) -> None:
        self.assertEqual([], list((root or self.root).rglob(f"{files.TEMPORARY_PREFIX}*")))
