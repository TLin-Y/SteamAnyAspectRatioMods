import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from scanner.mod_catalog import load_mod_catalog
from scanner.mod_installer import apply_mod_files, fetch_mod_readme, set_pack_dir
from store import PluginStore, Settings, export_pack, install_pack

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

    def test_export_then_import_round_trips(self) -> None:
        install_pack(self.store, Settings(), self.repo_zip)
        exported = Path(export_pack(self.store, str(self.root)))

        other = PluginStore(self.root / "other")
        other.ensure_directories()
        result = install_pack(other, Settings(), exported)

        self.assertEqual(3, result["file_count"])
        self.assertEqual(result["size_bytes"], self.store.pack_stats()["size_bytes"])

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

    def test_export_requires_a_pack(self) -> None:
        with self.assertRaises(ValueError):
            export_pack(self.store, str(self.root))
