import threading
from dataclasses import replace
from unittest.mock import patch

from support.builders import app_for, catalog_mod
from support.plugin import LOGGER_NAME, PluginTest

from store.dependencies import CatalogSource
from store.plugin_store import Settings

ORI = catalog_mod("ori-and-the-will-of-the-wisps")
ORI_INI = b"[AspectRatio]\nTargetWidth = 16.0\nTargetHeight = 10.0\nOther = keep\n"


class ResolutionTests(PluginTest):
    def installed(self, entry=ORI, ini: bytes = ORI_INI):
        """The mod's files are in the game folder; returns the scan report that finds them."""
        self.write({**dict.fromkeys(entry.files, "mod"), entry.ini_file: ini})
        return self.report(entry, apps=[app_for(entry, self.game, "Game")])

    def remind(self, width: int = 1920, height: int = 1080) -> None:
        self.store.save_settings(Settings(ini_resolution_width=width, ini_resolution_height=height,
                                          ini_resolution_update_pending=True))

    def reminded(self) -> bool:
        return self.store.load_settings().ini_resolution_update_pending

    def test_saving_a_new_resolution_sets_the_update_reminder(self) -> None:
        self.store.save_settings(Settings(offline_mode=True))
        self.assertTrue(self.call("save_settings", "", "", "", "", "1600", "900")["saved"])
        saved = self.store.load_settings()
        self.assertEqual((True, 1600, 900, True), (saved.offline_mode, saved.ini_resolution_width,
                                                   saved.ini_resolution_height, saved.ini_resolution_update_pending))
        self.store.save_settings(Settings(ini_resolution_update_pending=True))
        self.call("save_settings", "", "", "", "", "1280", "800")
        self.assertTrue(self.reminded())

        with (
            patch.object(self.main, "load_active_catalog", return_value=CatalogSource("bundled", ())),
            patch.object(self.main.Plugin, "_scan_local_archives", return_value=([], [])),
        ):
            defaults = self.call("get_settings")["defaults"]
        self.assertEqual((Settings().ini_resolution_width, Settings().ini_resolution_height),
                         (defaults["ini_resolution_width"], defaults["ini_resolution_height"]))

    def test_update_writes_the_resolution_into_complete_unambiguous_mods_only(self) -> None:
        report = self.installed()
        self.remind()
        with self.scanning(report):
            result = self.call("update_current_resolution")
            self.assertEqual((["Game"], [], False, False), (result["updated"], result["failures"],
                                                            result["resolution_update_pending"], self.reminded()))
            self.assert_files({ORI.ini_file: ORI_INI.replace(b"TargetHeight = 10.0", b"TargetHeight = 9.0")})
            self.assertEqual(["Game"], self.call("update_current_resolution")["unchanged"])
            report.supported_games = [replace(report.supported_games[0], ambiguity_group="ambiguous", match_locked=False)]
            skipped = self.call("update_current_resolution")
        self.assertEqual([], skipped["updated"])
        self.assertIn("select the correct mod", skipped["skipped"][0])

    def test_update_reports_only_mods_with_resolution_fields(self) -> None:
        report = self.installed(catalog_mod("sonic-x-shadow-generations"), b"[Display]\nMode = 1\n")
        report.warnings.append("Steam library is unavailable: /run/media/deck/SD")
        with self.scanning(report):
            result = self.call("update_current_resolution")
        self.assertEqual(([], [], [], []), (result["updated"], result["unchanged"], result["skipped"], result["failures"]))
        self.assertNotIn("warnings", result)

    def test_update_is_refused_while_a_pack_operation_runs(self) -> None:
        report = self.installed()
        self.remind()
        held, release = threading.Event(), threading.Event()

        def hold_pack_lock() -> None:
            with self.main.pack_operation():
                held.set()
                release.wait(5)

        holder = threading.Thread(target=hold_pack_lock)
        holder.start()
        self.addCleanup(holder.join)
        self.addCleanup(release.set)
        held.wait(5)
        with self.scanning(report), self.assertRaisesRegex(ValueError, "Another offline pack operation"):
            self.call("update_current_resolution")
        self.assert_files({ORI.ini_file: ORI_INI})
        self.assertTrue(self.reminded())

    def test_a_failed_update_keeps_the_ini_and_the_reminder(self) -> None:
        report = self.installed()
        self.remind()
        with self.scanning(report), patch("utils.files.os.replace", side_effect=PermissionError("denied")):
            result = self.call("update_current_resolution")
        self.assertEqual([], result["updated"])
        self.assertIn("denied", result["failures"][0])
        self.assertTrue(result["resolution_update_pending"] and self.reminded())
        self.assert_files({ORI.ini_file: ORI_INI})

    def test_permissions_that_cannot_be_kept_are_only_logged(self) -> None:
        orochi = catalog_mod("warriors-orochi-3-ultimate")
        self.touch(orochi.executable)
        report = self.installed(orochi, b"[AspectRatio]\r\nAspectWidth = 4\r\nAspectHeight = 3\r\n")
        self.store.save_settings(Settings(ini_resolution_update_pending=True))
        with (
            self.scanning(report),
            patch("utils.files.os.chmod", side_effect=PermissionError("denied")),
            self.assertLogs(LOGGER_NAME, level="WARNING") as logs,
        ):
            result = self.call("update_current_resolution")
        self.assertEqual((["Game"], [], False), (result["updated"], result["failures"], result["resolution_update_pending"]))
        self.assertIn("Could not keep INI file permissions", "\n".join(logs.output))
        self.assert_files({orochi.ini_file: "[AspectRatio]\r\nAspectWidth = 8\r\nAspectHeight = 5\r\n"})

    def test_reminder_stays_when_the_resolution_changes_during_the_update(self) -> None:
        report = self.installed(ini=b"[AspectRatio]\nTargetWidth = 4\nTargetHeight = 3\n")
        self.store.save_settings(Settings(ini_resolution_update_pending=True))

        def update_while_resolution_changes(*_):
            self.remind()
            return True, True

        with self.scanning(report), patch.object(self.main, "update_existing_ini_resolution",
                                                 side_effect=update_while_resolution_changes):
            self.assertTrue(self.call("update_current_resolution")["resolution_update_pending"])
        self.assertTrue(self.reminded())
