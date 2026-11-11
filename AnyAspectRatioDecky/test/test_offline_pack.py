import asyncio
import hashlib
import importlib
import io
import json
import logging
import sys
import tempfile
import threading
import types
import unittest
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from http.client import IncompleteRead
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from scanner.mod_catalog import load_mod_catalog
from scanner.mod_installer import apply_mod_files, fetch_mod_readme, set_offline_mode, set_pack_dir
from store import PluginStore, Settings, download_release_pack, install_pack, load_active_catalog, update_dependencies
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
        set_offline_mode(True)
        self.addCleanup(set_offline_mode, False)
        with patch("scanner.mod_installer.urlopen", side_effect=AssertionError("network used")):
            self.assertEqual([], apply_mod_files(mod, str(game)))
            self.assertEqual("# DS3", fetch_mod_readme(mod))
        self.assertTrue((game / "Game" / "dinput8.dll").is_file())

    def _create_pack_parts(self, catalog_bytes: bytes | None = None) -> tuple[Path, Path]:
        parts_dir = self.root / "parts"
        parts_dir.mkdir(exist_ok=True)
        with ZipFile(self.repo_zip) as source:
            members = {
                item.filename: source.read(item)
                for item in source.infolist()
                if not item.is_dir()
            }
        catalog_name = "repo-main/AnyAspectRatioDecky/catalog.json"
        if catalog_bytes is not None:
            members[catalog_name] = catalog_bytes
        part_names = (
            "AnyAspectRatio-Offline-Mods-1.8.0-part1-of-2.zip",
            "AnyAspectRatio-Offline-Mods-1.8.0-part2-of-2.zip",
        )
        groups = (
            {catalog_name, f"repo-main/{self.mod_path}"},
            {catalog_name, "repo-main/Dark Souls III/README.md"},
        )
        paths = tuple(parts_dir / name for name in part_names)
        for path, group in zip(paths, groups):
            with ZipFile(path, "w") as archive:
                for member in sorted(group):
                    archive.writestr(member, members[member])
        return paths

    def test_importing_one_part_imports_the_complete_matching_set(self) -> None:
        first, _ = self._create_pack_parts()

        result = install_pack(self.store, Settings(), first)

        self.assertEqual(1, result["mod_count"])
        self.assertTrue((self.store.pack_dir / self.mod_path).is_file())
        with patch("scanner.mod_installer.urlopen", side_effect=OSError("offline")):
            self.assertEqual(
                "# DS3", fetch_mod_readme(load_mod_catalog(self.store.catalog_path)[0])
            )

    def test_missing_part_keeps_existing_pack(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        first, second = self._create_pack_parts()
        second.unlink()

        with self.assertRaisesRegex(ValueError, "Keep all 2 offline pack parts"):
            install_pack(self.store, Settings(), first)

        self.assertTrue((self.store.pack_dir / self.mod_path).is_file())

    def test_parts_with_different_catalogs_are_rejected(self) -> None:
        first, second = self._create_pack_parts()
        with ZipFile(second, "w") as archive:
            archive.writestr(
                "repo-main/AnyAspectRatioDecky/catalog.json",
                b'{"schema_version":3,"mods":[]}',
            )
            archive.writestr("repo-main/Dark Souls III/README.md", "# DS3")

        with self.assertRaisesRegex(ValueError, "different catalogs"):
            install_pack(self.store, Settings(), (first, second))

    def test_online_readme_prefers_the_newest_file_over_the_pack(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        mod = load_mod_catalog(self.store.catalog_path)[0]
        with patch("scanner.mod_installer.urlopen", return_value=io.BytesIO(b"# DS3 newer")):
            self.assertEqual("# DS3 newer", fetch_mod_readme(mod))

    def test_pack_is_the_fallback_when_the_network_fails(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        mod = load_mod_catalog(self.store.catalog_path)[0]
        with patch("scanner.mod_installer.urlopen", side_effect=OSError("down")):
            self.assertEqual("# DS3", fetch_mod_readme(mod))

    def test_interrupted_read_falls_back_to_complete_pack_file(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        mod = load_mod_catalog(self.store.catalog_path)[0]

        class InterruptedStream(io.BytesIO):
            def read(self, size=-1):
                if self.tell() == 0:
                    return super().read(3)
                raise IncompleteRead(b"partial")

        with patch("scanner.mod_installer.urlopen", return_value=InterruptedStream(b"partial")):
            self.assertEqual("# DS3", fetch_mod_readme(mod))

    def test_import_preserves_online_catalog_and_offline_uses_pack_catalog(self) -> None:
        document = json.loads((PLUGIN_ROOT / "catalog.json").read_text(encoding="utf-8"))
        online = json.dumps(document).encode()
        with patch("scanner.mod_installer.urlopen", return_value=io.BytesIO(online)):
            update_dependencies(self.store, Settings())
        install_pack(self.store, Settings(), self.repo_zip)

        self.assertEqual(len(document["mods"]), len(load_active_catalog(self.store, Settings()).mods))
        offline = load_active_catalog(self.store, Settings(offline_mode=True))
        self.assertEqual(1, len(offline.mods))
        game = self.root / "game"
        game.mkdir()
        set_offline_mode(True)
        self.addCleanup(set_offline_mode, False)
        with patch("scanner.mod_installer.urlopen", side_effect=AssertionError("network used")):
            self.assertEqual([], apply_mod_files(offline.mods[0], str(game)))

    def test_online_import_refreshes_pack_catalog(self) -> None:
        decky = types.ModuleType("decky")
        decky.logger = logging.getLogger("test_offline_pack")
        with patch.dict(sys.modules, {"decky": decky}):
            main = importlib.import_module("main")
        online = (PLUGIN_ROOT / "catalog.json").read_bytes()
        with (
            patch.object(main, "get_store", return_value=self.store),
            patch("scanner.mod_installer.urlopen", return_value=io.BytesIO(online)),
        ):
            result = asyncio.run(main.Plugin().import_offline_pack(str(self.repo_zip)))
        self.assertIsNone(result["warning"])
        self.assertEqual(len(json.loads(online)["mods"]), len(load_active_catalog(self.store, Settings()).mods))
        self.assertEqual(1, len(load_active_catalog(self.store, Settings(offline_mode=True)).mods))

    def test_online_import_reports_failed_catalog_refresh(self) -> None:
        decky = types.ModuleType("decky")
        decky.logger = logging.getLogger("test_offline_pack")
        with patch.dict(sys.modules, {"decky": decky}):
            main = importlib.import_module("main")
        with (
            patch.object(main, "get_store", return_value=self.store),
            patch("scanner.mod_installer.urlopen", side_effect=OSError("network down")),
        ):
            result = asyncio.run(main.Plugin().import_offline_pack(str(self.repo_zip)))
        self.assertIn("Could not refresh", result["warning"])
        self.assertEqual(1, len(load_active_catalog(self.store, Settings(offline_mode=True)).mods))

    def test_disk_usage_counts_data_cache_pack_and_logs(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        (self.store.log_dir / "test.log").write_bytes(b"log")
        (self.store.cache_dir / "test.cache").write_bytes(b"cache")
        expected = sum(path.stat().st_size for path in self.store.data_dir.rglob("*") if path.is_file())
        self.assertEqual(expected, self.store.disk_usage())

    def test_stale_pack_archive_is_not_applied_when_the_catalog_checksum_changed(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        mod = load_mod_catalog(self.store.catalog_path)[0]
        newer = replace(mod, source=replace(mod.source, sha256="a" * 64))
        game = self.root / "game"
        game.mkdir()
        with patch("scanner.mod_installer.urlopen", side_effect=OSError("down")):
            with self.assertRaises(OSError):
                apply_mod_files(newer, str(game))
        self.assertFalse((game / "Game").exists())

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
        set_offline_mode(True)
        self.addCleanup(set_offline_mode, False)
        with patch("scanner.mod_installer.urlopen", side_effect=AssertionError("network used")):
            self.assertEqual([], apply_mod_files(mod, str(game)))
            self.assertEqual("# DS3", fetch_mod_readme(mod))

    def test_downloads_and_installs_all_release_pack_parts(self) -> None:
        paths = self._create_pack_parts()
        repository = "TLin-Y/SteamAnyAspectRatioMods"
        tag = "v1.8.0"
        assets = []
        part_contents = {}
        for path in paths:
            url = f"https://github.com/{repository}/releases/download/{tag}/{path.name}"
            assets.append({"name": path.name, "browser_download_url": url})
            part_contents[url] = path.read_bytes()

        with (
            patch("store.pack.latest_release", return_value=(repository, {"assets": assets}, tag)),
            patch(
                "store.pack.open_url",
                side_effect=lambda url: io.BytesIO(part_contents[url]),
            ) as download,
        ):
            result = download_release_pack(self.store, Settings())

        self.assertEqual(2, download.call_count)
        self.assertEqual("1.8.0", result["version"])
        self.assertTrue((self.store.pack_dir / self.mod_path).is_file())

    def test_incomplete_release_pack_parts_are_rejected_before_download(self) -> None:
        repository = "TLin-Y/SteamAnyAspectRatioMods"
        tag = "v1.8.0"
        name = "AnyAspectRatio-Offline-Mods-1.8.0-part1-of-2.zip"
        url = f"https://github.com/{repository}/releases/download/{tag}/{name}"
        release = {"assets": [{"name": name, "browser_download_url": url}]}
        with (
            patch("store.pack.latest_release", return_value=(repository, release, tag)),
            patch("store.pack.open_url", side_effect=AssertionError("download used")),
        ):
            with self.assertRaisesRegex(ValueError, "incomplete offline pack part set"):
                download_release_pack(self.store, Settings())

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
