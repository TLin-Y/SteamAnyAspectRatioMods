import json
from pathlib import Path
from unittest import mock

from support.builders import write_zip
from support.fakes import serving
from support.sandbox import PLUGIN_ROOT, SandboxTest

from helper import PLUGIN_NAME
from installer import plugin_updater

REPO = "https://github.com/TLin-Y/SteamAnyAspectRatioMods"
DOWNLOAD = f"{REPO}/releases/download/v1.5.0/AnyAspectRatio-1.5.0.zip"
PLUGIN_ZIP = {"name": "AnyAspectRatio-1.5.0.zip", "browser_download_url": DOWNLOAD}
OFFLINE_PACK = {"name": "AnyAspectRatio-Offline-1.5.0.zip", "browser_download_url": DOWNLOAD + "x"}


def release(tag: str = "v1.5.0", assets: list[dict] | None = None) -> bytes:
    assets = [OFFLINE_PACK, PLUGIN_ZIP] if assets is None else assets
    return json.dumps({"tag_name": tag, "assets": assets, "html_url": REPO}).encode()


def manifest(name: str = PLUGIN_NAME, **fields: str) -> str:
    return json.dumps({"name": name, **fields})


class PluginUpdaterTests(SandboxTest):
    def test_plugin_name_matches_manifest_and_frontend(self) -> None:
        self.assertEqual(PLUGIN_NAME, json.loads((PLUGIN_ROOT / "plugin.json").read_text(encoding="utf-8"))["name"])
        callables = (PLUGIN_ROOT / "src/services/callables.ts").read_text(encoding="utf-8")
        self.assertIn(f'export const PLUGIN_NAME = "{PLUGIN_NAME}";', callables)

    def test_versions(self) -> None:
        self.assertEqual("", plugin_updater.read_installed_version(self.root))
        self.write({"plugin.json": json.dumps({"version": "1.9.0"})})
        self.assertEqual("1.9.0", plugin_updater.read_installed_version(self.root))
        self.assertEqual((1, 4), plugin_updater.parse_version("v1.4.0"))
        self.assertGreater(plugin_updater.parse_version("1.10"), plugin_updater.parse_version("1.9.9"))
        self.assertIsNone(plugin_updater.parse_version("nightly"))

    def test_only_a_newer_plugin_zip_from_the_same_repository_is_offered(self) -> None:
        other_repository = {**PLUGIN_ZIP, "browser_download_url": "https://github.com/evil/repo/releases/download/v1/x.zip"}
        cases = (
            (release(), True, DOWNLOAD),
            (release("v1.4.0"), False, DOWNLOAD),
            (release(assets=[other_repository]), True, ""),
        )
        for response, available, download_url in cases:
            with self.subTest(response=response), mock.patch.object(plugin_updater, "read_url_limited",
                                                                    return_value=response):
                info = plugin_updater.check_for_update(REPO, "1.4.0")
                self.assertEqual((available, download_url), (info["update_available"], info["download_url"]))

    def test_update_overlays_the_plugin_folder_and_keeps_other_files(self) -> None:
        plugin_dir = self.folder("plugin")
        self.write({"keep.txt": "keep", "main.py": "old"}, plugin_dir)
        archive = write_zip(self.root / "update.zip", {
            "AnyAspectRatio/plugin.json": manifest(version="1.5.0"),
            "AnyAspectRatio/main.py": "new",
            "AnyAspectRatio/dist/index.js": "js",
        })
        self.assertEqual("1.5.0", plugin_updater._extract_plugin_zip(archive, plugin_dir))
        self.assert_files({"main.py": "new", "keep.txt": "keep", "dist/index.js": "js"}, plugin_dir)

    def test_zip_for_another_plugin_or_with_unsafe_paths_is_rejected(self) -> None:
        plugin_dir = self.folder("plugin")
        for entries in ({"plugin.json": manifest("Other")}, {"plugin.json": manifest(), "../escape.txt": "x"}):
            with self.subTest(entries=list(entries)), self.assertRaises(ValueError):
                plugin_updater._extract_plugin_zip(write_zip(self.root / "update.zip", entries), plugin_dir)
        self.assertFalse((self.root / "escape.txt").exists())

    def test_read_only_plugin_folder_saves_the_zip_for_manual_install(self) -> None:
        source = write_zip(self.root / "source.zip", {"plugin.json": manifest()})
        with (
            serving(source.read_bytes()),
            mock.patch.object(plugin_updater, "_extract_plugin_zip", side_effect=PermissionError),
        ):
            result = plugin_updater.install_update(REPO, DOWNLOAD, self.root / "plugin", self.root / "Downloads")
        self.assertFalse(result["installed"])
        self.assertTrue(Path(result["saved_zip"]).is_file())

    def test_download_must_come_from_the_configured_repository(self) -> None:
        with self.assertRaises(ValueError):
            plugin_updater.install_update(REPO, "https://github.com/evil/repo/releases/download/v1/x.zip",
                                          Path("."), Path("."))
