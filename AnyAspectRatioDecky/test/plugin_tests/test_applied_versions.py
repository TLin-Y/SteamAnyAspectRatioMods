import hashlib
from dataclasses import replace
from email.message import Message
from unittest.mock import patch
from urllib.error import HTTPError

from support.builders import app_for, catalog_mod, write_zip
from support.plugin import PluginTest

from installer import backups
from scanner.mod_catalog import ModDefinition, load_mod_catalog
from store.mod_sources import set_pack_dir
from utils.network import NetworkError

HASHED = next(mod for mod in load_mod_catalog() if mod.source.sha256 and not mod.file_backups)


class AppliedVersionTests(PluginTest):
    """The plugin remembers which archive version was applied, so it can offer updates."""

    def scan(self, entry: ModDefinition):
        report = self.report(entry, apps=[app_for(entry, self.game, "Game")])
        with self.catalog(lambda: (entry,), lambda: report):
            return self.main.Plugin._scan().supported_games[0]

    def set_applied(self, entry: ModDefinition, applied: bool, conflicts: list[str], **options: object):
        with (
            patch.object(self.main.Plugin, "_resolve_mod_game", return_value=(entry, str(self.game))),
            patch.object(self.main, "apply_mod_files", return_value=conflicts),
            patch.object(self.main, "remove_mod_files", return_value=conflicts),
        ):
            return self.call("set_mod_applied", entry.mod_id, str(self.game), applied, **options)

    def applied_hashes(self) -> dict:
        return self.store.load_applied_hashes()

    def test_version_is_recorded_only_after_a_full_apply_and_survives_a_cache_clear(self) -> None:
        self.touch(HASHED.files[0])
        updated = replace(HASHED, source=replace(HASHED.source, sha256="a" * 64))
        self.assertFalse(self.scan(updated).update_available)
        self.set_applied(HASHED, True, [])
        self.store.clear_local_data()
        for conflicts, options in (([updated.files[0]], {}), ([updated.files[0]], {"replace_existing_files": False})):
            with self.subTest(options=options):
                self.assertTrue(self.scan(updated).update_available)
                self.set_applied(updated, True, conflicts, **options)
        self.assertTrue(self.scan(updated).update_available)
        self.set_applied(updated, True, [], replace_existing_files=True)
        self.assertFalse(self.scan(updated).update_available)

        self.set_applied(updated, False, [updated.ini_file])
        self.assertIn(updated.mod_id, self.applied_hashes()[str(self.game)])
        self.set_applied(updated, False, [])
        self.assertEqual({}, self.applied_hashes())

    def test_missing_archive_refreshes_the_catalog_once_and_applies_the_new_entry(self) -> None:
        refreshed = replace(HASHED, source=replace(HASHED.source, sha256="b" * 64))
        game = (str(self.game),)
        for code, applies in ((404, [HASHED, refreshed]), (500, [HASHED])):
            failure = NetworkError(f"HTTP {code}")
            failure.__cause__ = HTTPError("https://example.com", code, "", Message(), None)
            with (
                self.subTest(code=code),
                patch.object(self.main.Plugin, "_resolve_mod_game", side_effect=[(HASHED, *game), (refreshed, *game)]),
                patch.object(self.main, "apply_mod_files", side_effect=[failure, []]) as apply,
                patch.object(self.main, "refresh_catalog_on_start") as refresh,
            ):
                if code == 404:
                    self.call("set_mod_applied", HASHED.mod_id, str(self.game), True)
                    self.assertEqual("b" * 64, self.applied_hashes()[str(self.game)][HASHED.mod_id])
                else:
                    with self.assertRaisesRegex(NetworkError, "HTTP 500"):
                        self.call("set_mod_applied", HASHED.mod_id, str(self.game), True)
                self.assertEqual(applies, [call.args[0] for call in apply.call_args_list])
                self.assertEqual(code == 404, refresh.called)

    def test_lost_backup_is_reported_and_the_mod_is_no_longer_counted_as_applied(self) -> None:
        undertale = catalog_mod("undertale")
        self.write({undertale.files[0]: "patched"})
        self.store.set_applied_hash(str(self.game), undertale.mod_id, undertale.source.sha256)
        applied = self.scan(undertale)
        self.assertEqual("complete", applied.file_status)
        self.assertIn(undertale.files[0], applied.mod_files)
        with (
            patch.object(self.main.Plugin, "_resolve_mod_game", return_value=(undertale, str(self.game))),
            patch.object(self.main, "remove_mod_files",
                         side_effect=backups.MissingModBackupError("Verify integrity of game files")),
        ):
            result = self.call("set_mod_applied", undertale.mod_id, str(self.game), False)
        self.assertIn("Verify integrity of game files", result["warning"])
        self.assertNotIn(undertale.mod_id, self.applied_hashes().get(str(self.game), {}))
        restored = self.scan(undertale)
        self.assertEqual("not_detected", restored.file_status)
        self.assertNotIn(undertale.files[0], restored.mod_files)

    def test_local_archives_never_offer_updates(self) -> None:
        local = replace(HASHED, source=replace(HASHED.source, kind="local_archive"))
        self.set_applied(local, True, [])
        self.assertEqual({}, self.applied_hashes())
        self.assertFalse(self.scan(local).update_available)

    def test_unreadable_version_file_is_reported(self) -> None:
        self.store.applied_hashes_path.write_text("{", encoding="utf-8")
        report = self.report(HASHED, apps=[app_for(HASHED, self.game, "Game")])
        with self.catalog(lambda: (HASHED,), lambda: report):
            warnings = self.main.Plugin._scan().warnings
        self.assertTrue(any("Unable to read applied mod hashes" in warning for warning in warnings))
        with self.assertRaisesRegex(ValueError, "Mod files were applied"):
            self.set_applied(HASHED, True, [])

    def test_confirmed_update_replaces_the_files_and_clears_the_badge_everywhere(self) -> None:
        re2 = catalog_mod("resident-evil-2")
        pack = self.folder("pack")
        set_pack_dir(pack)

        def archive(content: bytes) -> str:
            write_zip(pack / "mod.zip", dict.fromkeys(re2.files, content))
            return hashlib.sha256((pack / "mod.zip").read_bytes()).hexdigest()

        old = replace(re2, source=replace(re2.source, path="mod.zip", sha256=archive(b"old")))
        active = [old]

        def badge(result) -> bool:
            return result["supported_games"][0]["update_available"]

        def cached_badge() -> bool:
            return badge(self.call("get_cached_scan")["report"])

        with self.catalog(lambda: active, lambda: self.report(active[0], apps=[app_for(re2, self.game, "Game")])):
            self.assertEqual([], self.call("set_mod_applied", old.mod_id, str(self.game), True)["conflicting_files"])
            self.call("scan_supported_games")
            active[0] = replace(old, source=replace(old.source, sha256=archive(b"new")))
            self.assertTrue(cached_badge())
            self.assertTrue(badge(self.call("scan_supported_games")))
            for choice in (None, False):
                with self.subTest(choice=choice):
                    result = self.call("set_mod_applied", old.mod_id, str(self.game), True, choice)
                    self.assertEqual(set(old.files), set(result["conflicting_files"]))
                    stale = self.call("scan_supported_games")
                    self.assertTrue(badge(stale))
            self.assertEqual(old.source.sha256, self.applied_hashes()[str(self.game)][old.mod_id])

            self.assertEqual([], self.call("set_mod_applied", old.mod_id, str(self.game), True, True)["conflicting_files"])
            self.assert_files(dict.fromkeys(old.files, "new"))
            self.assertEqual(active[0].source.sha256, self.applied_hashes()[str(self.game)][old.mod_id])
            self.assertIsNone(self.call("get_cached_scan"))
            self.store.save_last_scan(stale)
            self.assertFalse(cached_badge())
            self.assertFalse(badge(self.call("scan_supported_games")))
            self.assertFalse(cached_badge())
