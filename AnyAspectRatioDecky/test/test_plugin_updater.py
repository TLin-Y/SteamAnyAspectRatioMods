import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from scanner import plugin_updater

REPO = "https://github.com/TLin-Y/SteamAnyAspectRatioMods"
DOWNLOAD = f"{REPO}/releases/download/v1.5.0/AnyAspectRatio-1.5.0.zip"


def release(tag="v1.5.0", assets=None):
    if assets is None:
        assets = [
            {"name": "AnyAspectRatio-Offline-1.5.0.zip", "browser_download_url": DOWNLOAD + "x"},
            {"name": "AnyAspectRatio-1.5.0.zip", "browser_download_url": DOWNLOAD},
        ]
    return json.dumps({"tag_name": tag, "assets": assets, "html_url": REPO}).encode()


def build_zip(path: Path, files: dict[str, str]) -> None:
    with ZipFile(path, "w") as archive:
        for name, content in files.items():
            archive.writestr(name, content)


class _Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        return None


class PluginUpdaterTests(unittest.TestCase):
    def test_version_comparison_ignores_trailing_zeroes_and_prefix(self):
        self.assertEqual(plugin_updater.parse_version("v1.4.0"), (1, 4))
        self.assertGreater(plugin_updater.parse_version("1.10"), plugin_updater.parse_version("1.9.9"))
        self.assertIsNone(plugin_updater.parse_version("nightly"))

    def test_newer_release_is_offered_with_the_plugin_zip_not_the_offline_pack(self):
        with mock.patch.object(plugin_updater, "read_url_limited", return_value=release()):
            info = plugin_updater.check_for_update(REPO, "1.4.0")
        self.assertTrue(info["update_available"])
        self.assertEqual(info["download_url"], DOWNLOAD)

    def test_same_version_is_not_an_update(self):
        with mock.patch.object(plugin_updater, "read_url_limited", return_value=release("v1.4.0")):
            self.assertFalse(plugin_updater.check_for_update(REPO, "1.4.0")["update_available"])

    def test_assets_from_another_repository_are_ignored(self):
        assets = [
            {
                "name": "AnyAspectRatio-1.5.0.zip",
                "browser_download_url": "https://github.com/evil/repo/releases/download/v1/AnyAspectRatio-1.5.0.zip",
            }
        ]
        with mock.patch.object(plugin_updater, "read_url_limited", return_value=release(assets=assets)):
            self.assertEqual(plugin_updater.check_for_update(REPO, "1.4.0")["download_url"], "")

    def test_update_is_extracted_over_the_plugin_folder_and_keeps_other_files(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            plugin_dir = root / "plugin"
            plugin_dir.mkdir()
            (plugin_dir / "keep.txt").write_text("keep")
            (plugin_dir / "main.py").write_text("old")
            archive = root / "update.zip"
            build_zip(
                archive,
                {
                    "AnyAspectRatio/plugin.json": json.dumps({"name": "AnyAspectRatio", "version": "1.5.0"}),
                    "AnyAspectRatio/main.py": "new",
                    "AnyAspectRatio/dist/index.js": "js",
                },
            )
            version = plugin_updater._extract_plugin_zip(archive, plugin_dir)
            self.assertEqual(version, "1.5.0")
            self.assertEqual((plugin_dir / "main.py").read_text(), "new")
            self.assertEqual((plugin_dir / "keep.txt").read_text(), "keep")
            self.assertEqual((plugin_dir / "dist" / "index.js").read_text(), "js")

    def test_zip_for_another_plugin_or_with_unsafe_paths_is_rejected(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            plugin_dir = root / "plugin"
            plugin_dir.mkdir()
            other = root / "other.zip"
            build_zip(other, {"plugin.json": json.dumps({"name": "Other"})})
            with self.assertRaises(ValueError):
                plugin_updater._extract_plugin_zip(other, plugin_dir)
            unsafe = root / "unsafe.zip"
            build_zip(
                unsafe,
                {
                    "plugin.json": json.dumps({"name": "AnyAspectRatio"}),
                    "../escape.txt": "x",
                },
            )
            with self.assertRaises(ValueError):
                plugin_updater._extract_plugin_zip(unsafe, plugin_dir)
            self.assertFalse((root / "escape.txt").exists())

    def test_read_only_plugin_folder_saves_the_zip_for_manual_install(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            source = root / "source.zip"
            build_zip(source, {"plugin.json": json.dumps({"name": "AnyAspectRatio"})})
            with (
                mock.patch.object(plugin_updater, "open_url", side_effect=lambda url: _Response(source.read_bytes())),
                mock.patch.object(plugin_updater, "_extract_plugin_zip", side_effect=PermissionError),
            ):
                result = plugin_updater.install_update(REPO, DOWNLOAD, root / "plugin", root / "Downloads")
            self.assertFalse(result["installed"])
            self.assertTrue(Path(result["saved_zip"]).is_file())

    def test_download_must_come_from_the_configured_repository(self):
        with self.assertRaises(ValueError):
            plugin_updater.install_update(
                REPO,
                "https://github.com/evil/repo/releases/download/v1/AnyAspectRatio-1.zip",
                Path("."),
                Path("."),
            )


if __name__ == "__main__":
    unittest.main()
