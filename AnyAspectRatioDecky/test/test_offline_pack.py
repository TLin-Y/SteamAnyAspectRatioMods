import hashlib
import io
import json
import tempfile
import threading
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from scanner.mod_catalog import load_mod_catalog
from scanner.mod_installer import apply_mod_files, fetch_mod_readme, set_pack_dir
from store import PluginStore, Settings, download_release_pack, install_pack
from scanner.mod_installer import OfflineModeError

PLUGIN_ROOT = Path(__file__).resolve().parents[1]


class OfflinePackTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.addCleanup(self.directory.cleanup)
        self.addCleanup(set_pack_dir, None)

        document = json.loads((PLUGIN_ROOT / "catalog.json").read_text(encoding="utf-8"))
        mod = next(entry for entry in document["mods"] if entry["id"] == "dark-souls-iii")
        document["mods"] = [mod]
        self.mod_path = mod["source"]["path"]
        self.repo_zip = self.root / "repo.zip"
        inner = self.root / "mod.zip"
        with ZipFile(inner, "w") as archive:
            for name in ("d3d11.dll", "dinput8.dll", "DS3_ARFix.asi", "DS3_ARFix.ini"):
                archive.writestr(name, name.encode())
        mod["source"]["sha256"] = hashlib.sha256(inner.read_bytes()).hexdigest()
        with ZipFile(self.repo_zip, "w") as archive:
            archive.writestr("repo-main/AnyAspectRatioDecky/catalog.json", json.dumps(document))
            archive.write(inner, f"repo-main/{self.mod_path}")
            archive.writestr("repo-main/Dark Souls III/README.md", "# DS3")
            archive.writestr("repo-main/compiler/dllmain.cpp", "source")

        self.store = PluginStore(self.root / "data")
        self.store.ensure_directories()
        set_pack_dir(self.store.pack_dir)

    def test_install_extracts_only_needed_files_and_works_offline(self) -> None:
        result = install_pack(self.store, Settings(), self.repo_zip)

        self.assertEqual(1, result["mod_count"])
        self.assertEqual(3, result["file_count"])
        self.assertFalse((self.store.pack_dir / "compiler").exists())

        mod = load_mod_catalog(self.store.catalog_path)[0]
        game = self.root / "game"
        game.mkdir()
        with patch("scanner.mod_installer.urlopen", side_effect=AssertionError("network used")):
            self.assertEqual([], apply_mod_files(mod, str(game)))
            self.assertEqual("# DS3", fetch_mod_readme(mod))
        self.assertTrue((game / "Game" / "dinput8.dll").is_file())

    def test_clear_local_data_removes_pack_and_caches_but_keeps_settings(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        self.store.save_settings(Settings(local_archive_dir=""))

        self.store.clear_local_data()

        self.assertEqual(0, self.store.pack_stats()["file_count"])
        self.assertFalse(self.store.catalog_path.exists())
        self.assertTrue(self.store.archive_cache_dir.is_dir())
        self.assertTrue(self.store.settings_path.is_file())

    def test_missing_mod_archive_is_rejected_and_keeps_existing_pack(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        broken = self.root / "broken.zip"
        with ZipFile(self.repo_zip) as source, ZipFile(broken, "w") as target:
            for item in source.infolist():
                if not item.filename.endswith(".zip"):
                    target.writestr(item, source.read(item))

        with self.assertRaises(ValueError):
            install_pack(self.store, Settings(), broken)
        self.assertEqual(3, self.store.pack_stats()["file_count"])

    def test_downloads_latest_versioned_release_and_installs_for_offline_use(self) -> None:
        repository = "TLin-Y/SteamAnyAspectRatioMods"
        tag = "v1.4.0"
        name = "AnyAspectRatio-Offline-Mods-1.4.0.zip"
        url = f"https://github.com/{repository}/releases/download/{tag}/{name}"
        release = {"assets": [{"name": name, "browser_download_url": url}]}
        with (
            patch("store.pack.latest_release", return_value=(repository, release, tag)),
            patch("store.pack.open_url", return_value=io.BytesIO(self.repo_zip.read_bytes())) as download,
        ):
            result = download_release_pack(self.store, Settings())
        download.assert_called_once_with(url)
        self.assertEqual(result["version"], "1.4.0")
        mod = load_mod_catalog(self.store.catalog_path)[0]
        game = self.root / "game"
        game.mkdir()
        with patch("scanner.mod_installer.urlopen", side_effect=AssertionError("network used")):
            self.assertEqual([], apply_mod_files(mod, str(game)))
            self.assertEqual("# DS3", fetch_mod_readme(mod))

    def test_missing_or_untrusted_release_asset_keeps_existing_pack(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        repository = "TLin-Y/SteamAnyAspectRatioMods"
        name = "AnyAspectRatio-Offline-Mods-1.4.0.zip"
        release = {"assets": [{"name": name, "browser_download_url": "https://example.com/pack.zip"}]}
        with (
            patch("store.pack.latest_release", return_value=(repository, release, "v1.4.0")),
            patch("store.pack.open_url", side_effect=AssertionError("download used")),
        ):
            with self.assertRaisesRegex(ValueError, "does not contain"):
                download_release_pack(self.store, Settings())
        self.assertEqual(3, self.store.pack_stats()["file_count"])

    def test_invalid_download_keeps_existing_pack(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        repository = "TLin-Y/SteamAnyAspectRatioMods"
        name = "AnyAspectRatio-Offline-Mods-1.4.0.zip"
        url = f"https://github.com/{repository}/releases/download/v1.4.0/{name}"
        release = {"assets": [{"name": name, "browser_download_url": url}]}
        with (
            patch("store.pack.latest_release", return_value=(repository, release, "v1.4.0")),
            patch("store.pack.open_url", return_value=io.BytesIO(b"invalid")),
        ):
            with self.assertRaises(ValueError):
                download_release_pack(self.store, Settings())
        self.assertEqual(3, self.store.pack_stats()["file_count"])

    def test_offline_mode_prevents_release_lookup(self) -> None:
        with patch("store.pack.latest_release", side_effect=AssertionError("network used")):
            with self.assertRaises(OfflineModeError):
                download_release_pack(self.store, Settings(offline_mode=True))

    def test_changed_settings_during_download_keep_existing_pack(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        repository = "TLin-Y/SteamAnyAspectRatioMods"
        name = "AnyAspectRatio-Offline-Mods-1.4.0.zip"
        url = f"https://github.com/{repository}/releases/download/v1.4.0/{name}"
        release = {"assets": [{"name": name, "browser_download_url": url}]}
        with (
            patch("store.pack.latest_release", return_value=(repository, release, "v1.4.0")),
            patch("store.pack.open_url", return_value=io.BytesIO(self.repo_zip.read_bytes())),
            patch.object(self.store, "load_settings", return_value=Settings(offline_mode=True)),
        ):
            with self.assertRaisesRegex(ValueError, "changed during the download"):
                download_release_pack(self.store, Settings())
        self.assertEqual(3, self.store.pack_stats()["file_count"])

    def test_overlapping_import_is_rejected_without_damaging_download(self) -> None:
        repository = "TLin-Y/SteamAnyAspectRatioMods"
        name = "AnyAspectRatio-Offline-Mods-1.4.0.zip"
        url = f"https://github.com/{repository}/releases/download/v1.4.0/{name}"
        release = {"assets": [{"name": name, "browser_download_url": url}]}
        started = threading.Event()
        proceed = threading.Event()

        class PausedDownload(io.BytesIO):
            def read(self, size=-1):
                started.set()
                if not proceed.wait(5):
                    raise TimeoutError("Test download did not resume")
                return super().read(size)

        with (
            patch("store.pack.latest_release", return_value=(repository, release, "v1.4.0")),
            patch("store.pack.open_url", return_value=PausedDownload(self.repo_zip.read_bytes())),
            ThreadPoolExecutor(max_workers=1) as executor,
        ):
            future = executor.submit(download_release_pack, self.store, Settings())
            try:
                self.assertTrue(started.wait(5))
                with self.assertRaisesRegex(ValueError, "operation is in progress"):
                    install_pack(self.store, Settings(), self.repo_zip)
            finally:
                proceed.set()
            self.assertEqual("1.4.0", future.result(timeout=5)["version"])
        self.assertEqual(3, self.store.pack_stats()["file_count"])
