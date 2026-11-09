import asyncio
import hashlib
import importlib
import logging
import sys
import tempfile
import types
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from scanner import InstalledApp, SteamScanReport, load_mod_catalog, match_supported_games
from scanner import mod_installer
from store import PluginStore, Settings
from store.dependencies import CatalogSource


class AppliedVersionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / "game"
        self.game.mkdir()
        self.store = PluginStore(self.root / "data")
        self.mod = next(mod for mod in load_mod_catalog() if mod.source.sha256)
        self.app = InstalledApp(
            self.mod.steam_app_id, "Game", self.mod.install_dir, "", str(self.game)
        )
        self.fake_decky = types.ModuleType("decky")
        self.fake_decky.logger = logging.getLogger("test_applied_versions")
        with patch.dict(sys.modules, {"decky": self.fake_decky}):
            self.main = importlib.import_module("main")

    def _report(self, mod):
        return match_supported_games(SteamScanReport(1, 1, [self.app], []), (mod,))

    def _scan(self, mod):
        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main, "load_active_catalog", return_value=CatalogSource("test", (mod,))),
            patch.object(self.main, "scan_supported_games_report", return_value=self._report(mod)),
        ):
            return self.main.Plugin._scan()

    def _operation(self, mod, applied, conflicts, **kwargs):
        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main.Plugin, "_resolve_mod_game", return_value=(mod, str(self.game))),
            patch.object(self.main, "apply_mod_files", return_value=conflicts),
            patch.object(self.main, "remove_mod_files", return_value=conflicts),
        ):
            return asyncio.run(
                self.main.Plugin().set_mod_applied(mod.mod_id, str(self.game), applied, **kwargs)
            )

    def test_settings_rpc_saves_global_ini_resolution(self) -> None:
        self.store.save_settings(Settings(offline_mode=True))
        with patch.object(self.main, "get_store", return_value=self.store):
            result = asyncio.run(
                self.main.Plugin().save_settings("", "", "", "", "1600", "900")
            )

        self.assertTrue(result["saved"])
        settings = self.store.load_settings()
        self.assertTrue(settings.offline_mode)
        self.assertEqual(
            (1600, 900),
            (settings.ini_resolution_width, settings.ini_resolution_height),
        )
        self.assertTrue(settings.ini_resolution_update_pending)

    def test_resolution_reminder_survives_other_settings_saves(self) -> None:
        self.store.save_settings(Settings(ini_resolution_update_pending=True))
        with patch.object(self.main, "get_store", return_value=self.store):
            asyncio.run(self.main.Plugin().save_settings("", "", "", "", "1280", "800"))
        self.assertTrue(self.store.load_settings().ini_resolution_update_pending)

    def test_settings_rpc_reports_resolution_defaults_from_settings(self) -> None:
        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main, "load_active_catalog", return_value=CatalogSource("bundled", ())),
            patch.object(self.main.Plugin, "_scan_local_archives", return_value=([], [])),
        ):
            result = asyncio.run(self.main.Plugin().get_settings())
        self.assertEqual(
            (Settings().ini_resolution_width, Settings().ini_resolution_height),
            (result["defaults"]["ini_resolution_width"], result["defaults"]["ini_resolution_height"]),
        )

    def test_update_current_uses_saved_resolution_only_for_complete_unambiguous_mods(self) -> None:
        mod = next(m for m in load_mod_catalog() if m.mod_id == "ori-and-the-will-of-the-wisps")
        for name in mod.files:
            path = self.game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"mod")
        ini = self.game / mod.ini_file
        ini.write_bytes(b"[AspectRatio]\nTargetWidth = 16.0\nTargetHeight = 10.0\nOther = keep\n")
        app = replace(self.app, app_id=mod.steam_app_id, install_dir=mod.install_dir)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.store.save_settings(Settings(
            ini_resolution_width=1920, ini_resolution_height=1080, ini_resolution_update_pending=True
        ))
        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main.Plugin, "_scan", return_value=report),
        ):
            result = asyncio.run(self.main.Plugin().update_current_resolution())
            self.assertEqual(["Game"], result["updated"])
            self.assertEqual([], result["failures"])
            self.assertFalse(result["resolution_update_pending"])
            self.assertFalse(self.store.load_settings().ini_resolution_update_pending)
            self.assertEqual(
                b"[AspectRatio]\nTargetWidth = 16.0\nTargetHeight = 9.0\nOther = keep\n",
                ini.read_bytes(),
            )
            again = asyncio.run(self.main.Plugin().update_current_resolution())
            self.assertEqual(["Game"], again["unchanged"])
            report.supported_games = [
                replace(report.supported_games[0], ambiguity_group="ambiguous", match_locked=False)
            ]
            skipped = asyncio.run(self.main.Plugin().update_current_resolution())
            self.assertEqual([], skipped["updated"])
            self.assertIn("select the correct mod", skipped["skipped"][0])

    def test_update_current_reports_failure_without_modifying_other_inis(self) -> None:
        mod = next(m for m in load_mod_catalog() if m.mod_id == "ori-and-the-will-of-the-wisps")
        for name in mod.files:
            path = self.game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"mod")
        ini = self.game / mod.ini_file
        original = b"[AspectRatio]\nTargetWidth = 16.0\nTargetHeight = 10.0\n"
        ini.write_bytes(original)
        self.store.save_settings(Settings(ini_resolution_update_pending=True))
        app = replace(self.app, app_id=mod.steam_app_id, install_dir=mod.install_dir)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main.Plugin, "_scan", return_value=report),
            patch("scanner.mod_installer.os.replace", side_effect=PermissionError("denied")),
        ):
            result = asyncio.run(self.main.Plugin().update_current_resolution())
        self.assertEqual([], result["updated"])
        self.assertIn("denied", result["failures"][0])
        self.assertEqual(original, ini.read_bytes())
        self.assertTrue(result["resolution_update_pending"])
        self.assertTrue(self.store.load_settings().ini_resolution_update_pending)

    def test_update_current_logs_permission_fallback_without_warning_per_game(self) -> None:
        mod = next(m for m in load_mod_catalog() if m.mod_id == "warriors-orochi-3-ultimate")
        (self.game / mod.executable).touch()
        for name in mod.files:
            path = self.game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"mod")
        ini = self.game / mod.ini_file
        ini.write_bytes(b"[AspectRatio]\r\nAspectWidth = 4\r\nAspectHeight = 3\r\n")
        app = replace(self.app, app_id=mod.steam_app_id, install_dir=mod.install_dir)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.store.save_settings(Settings(ini_resolution_update_pending=True))
        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main.Plugin, "_scan", return_value=report),
            patch("scanner.mod_installer.os.chmod", side_effect=PermissionError("denied")),
            self.assertLogs("test_applied_versions", level="WARNING") as logs,
        ):
            result = asyncio.run(self.main.Plugin().update_current_resolution())
        self.assertEqual(["Game"], result["updated"])
        self.assertEqual([], result["warnings"])
        self.assertFalse(result["resolution_update_pending"])
        self.assertIn("ini_permissions_fallback", "\n".join(logs.output))
        self.assertEqual(b"[AspectRatio]\r\nAspectWidth = 8\r\nAspectHeight = 5\r\n", ini.read_bytes())

    def test_bulk_update_keeps_reminder_when_saved_resolution_changes_during_scan(self) -> None:
        mod = next(m for m in load_mod_catalog() if m.mod_id == "ori-and-the-will-of-the-wisps")
        for name in mod.files:
            path = self.game / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"mod")
        (self.game / mod.ini_file).write_bytes(b"[AspectRatio]\nTargetWidth = 4\nTargetHeight = 3\n")
        app = replace(self.app, app_id=mod.steam_app_id, install_dir=mod.install_dir)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.store.save_settings(Settings(ini_resolution_update_pending=True))

        def update_and_change(*args):
            self.store.save_settings(Settings(
                ini_resolution_width=1920, ini_resolution_height=1080, ini_resolution_update_pending=True
            ))
            return True, True

        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main.Plugin, "_scan", return_value=report),
            patch.object(self.main, "update_existing_ini_resolution", side_effect=update_and_change),
        ):
            result = asyncio.run(self.main.Plugin().update_current_resolution())
        self.assertTrue(result["resolution_update_pending"])
        self.assertTrue(self.store.load_settings().ini_resolution_update_pending)

    def test_hash_tracking_survives_cache_clear_and_updates_only_on_full_apply(self) -> None:
        file_path = self.game / self.mod.files[0]
        file_path.parent.mkdir(parents=True, exist_ok=True)
        file_path.touch()
        updated = replace(self.mod, source=replace(self.mod.source, sha256="a" * 64))
        self.assertFalse(self._scan(updated).supported_games[0].update_available)

        self._operation(self.mod, True, [])
        self.store.clear_local_data()
        self.assertTrue(self._scan(updated).supported_games[0].update_available)
        self._operation(updated, True, [updated.files[0]])
        self.assertTrue(self._scan(updated).supported_games[0].update_available)
        self._operation(updated, True, [updated.files[0]], replace_existing_files=False)
        self.assertTrue(self._scan(updated).supported_games[0].update_available)
        self._operation(updated, True, [], replace_existing_files=True)
        self.assertFalse(self._scan(updated).supported_games[0].update_available)

        self._operation(updated, False, [updated.ini_file])
        self.assertIn(updated.mod_id, self.store.load_applied_hashes()[str(self.game)])
        self._operation(updated, False, [])
        self.assertEqual({}, self.store.load_applied_hashes())

    def test_local_archives_do_not_record_or_display_updates(self) -> None:
        local = replace(self.mod, source=replace(self.mod.source, kind="local_archive"))
        self._operation(local, True, [])
        self.assertEqual({}, self.store.load_applied_hashes())
        self.assertFalse(self._scan(local).supported_games[0].update_available)

    def test_confirmed_update_replaces_files_and_clears_badge_in_scan_and_cache(self) -> None:
        self.mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "resident-evil-2")
        self.app = replace(self.app, app_id=self.mod.steam_app_id, install_dir=self.mod.install_dir)
        pack = self.root / "pack"
        pack.mkdir()
        archive = pack / "mod.zip"

        def write_archive(contents: bytes) -> str:
            with ZipFile(archive, "w") as zip_file:
                for path in self.mod.files:
                    zip_file.writestr(path, contents)
            return hashlib.sha256(archive.read_bytes()).hexdigest()

        old = replace(self.mod, source=replace(self.mod.source, path="mod.zip", sha256=write_archive(b"old")))
        active = [old]
        with (
            patch.object(self.main, "get_store", return_value=self.store),
            patch.object(self.main, "load_active_catalog", side_effect=lambda *args: CatalogSource("test", tuple(active))),
            patch.object(self.main, "scan_supported_games_report", side_effect=lambda *args, **kwargs: self._report(active[0])),
            patch.object(mod_installer, "_pack_dir", pack),
        ):
            plugin = self.main.Plugin()
            result = asyncio.run(plugin.set_mod_applied(old.mod_id, str(self.game), True))
            self.assertEqual([], result["conflicting_files"])
            asyncio.run(plugin.scan_supported_games())
            active[0] = replace(old, source=replace(old.source, sha256=write_archive(b"new")))
            cached = asyncio.run(plugin.get_cached_scan())
            self.assertTrue(cached["report"]["supported_games"][0]["update_available"])
            report = asyncio.run(plugin.scan_supported_games())
            self.assertTrue(report["supported_games"][0]["update_available"])
            result = asyncio.run(plugin.set_mod_applied(old.mod_id, str(self.game), True))
            self.assertEqual(set(old.files), set(result["conflicting_files"]))
            report = asyncio.run(plugin.scan_supported_games())
            self.assertTrue(report["supported_games"][0]["update_available"])
            result = asyncio.run(plugin.set_mod_applied(old.mod_id, str(self.game), True, False))
            self.assertEqual(set(old.files), set(result["conflicting_files"]))
            self.assertEqual(old.source.sha256, self.store.load_applied_hashes()[str(self.game)][old.mod_id])
            stale_report = asyncio.run(plugin.scan_supported_games())
            self.assertTrue(stale_report["supported_games"][0]["update_available"])
            result = asyncio.run(plugin.set_mod_applied(old.mod_id, str(self.game), True, True))
            self.assertEqual([], result["conflicting_files"])
            self.assertTrue(all((self.game / path).read_bytes() == b"new" for path in old.files))
            self.assertEqual(active[0].source.sha256, self.store.load_applied_hashes()[str(self.game)][old.mod_id])
            self.assertIsNone(asyncio.run(plugin.get_cached_scan()))
            self.store.save_last_scan(stale_report)
            cached = asyncio.run(plugin.get_cached_scan())
            self.assertFalse(cached["report"]["supported_games"][0]["update_available"])
            report = asyncio.run(plugin.scan_supported_games())
            self.assertFalse(report["supported_games"][0]["update_available"])
            cached = asyncio.run(plugin.get_cached_scan())
            self.assertFalse(cached["report"]["supported_games"][0]["update_available"])

    def test_invalid_hash_store_reports_failure(self) -> None:
        self.store.data_dir.mkdir(parents=True)
        self.store.applied_hashes_path.write_text("{", encoding="utf-8")
        report = self._scan(self.mod)
        self.assertTrue(any("Unable to read applied mod hashes" in w for w in report.warnings))
        with self.assertRaisesRegex(ValueError, "Mod files were applied"):
            self._operation(self.mod, True, [])


if __name__ == "__main__":
    unittest.main()
