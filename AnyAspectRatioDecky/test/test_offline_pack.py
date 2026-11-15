import asyncio
import hashlib
import importlib
import io
import json
import logging
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from store.dependencies import load_active_catalog
from store.mod_sources import set_pack_dir
from store.offline_pack import install_pack
from store.plugin_store import PluginStore, Settings

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

    def test_online_import_refreshes_pack_catalog(self) -> None:
        decky = types.ModuleType("decky")
        decky.logger = logging.getLogger("test_offline_pack")
        with patch.dict(sys.modules, {"decky": decky}):
            main = importlib.import_module("main")
        online = (PLUGIN_ROOT / "catalog.json").read_bytes()
        with (
            patch.object(main, "get_store", return_value=self.store),
            patch("utils.network.urlopen", return_value=io.BytesIO(online)),
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
            patch("utils.network.urlopen", side_effect=OSError("network down")),
        ):
            result = asyncio.run(main.Plugin().import_offline_pack(str(self.repo_zip)))
        self.assertIn("Could not refresh", result["warning"])
        self.assertEqual(1, len(load_active_catalog(self.store, Settings(offline_mode=True)).mods))

    def test_uninstall_deletes_pack_and_caches_but_keeps_settings(self) -> None:
        decky = types.ModuleType("decky")
        decky.logger = logging.getLogger("test_offline_pack")
        with patch.dict(sys.modules, {"decky": decky}):
            main = importlib.import_module("main")
        install_pack(self.store, Settings(), self.repo_zip)
        (self.store.cache_dir / "test.cache").write_bytes(b"cache")
        self.store.save_settings(Settings(offline_mode=True))
        plugin = main.Plugin()
        with patch.object(main, "get_store", return_value=self.store):
            asyncio.run(plugin._unload())
            asyncio.run(plugin._uninstall())
        self.assertFalse(self.store.pack_dir.exists())
        self.assertEqual([], [path for path in self.store.cache_dir.rglob("*") if path.is_file()])
        self.assertTrue(self.store.load_settings().offline_mode)
