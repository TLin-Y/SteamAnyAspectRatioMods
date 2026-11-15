from contextlib import ExitStack
from unittest.mock import patch

from support.catalog import catalog_document
from support.fakes import failing, serving
from support.plugin import PluginTest, file_logger
from support.sandbox import PLUGIN_ROOT

from store.dependencies import load_active_catalog
from store.plugin_store import Settings


class LocalDataTests(PluginTest):
    def mod_count(self, settings: Settings = Settings()) -> int:
        return len(load_active_catalog(self.store, settings).mods)

    def test_importing_a_pack_online_also_refreshes_the_catalog(self) -> None:
        with serving((PLUGIN_ROOT / "catalog.json").read_bytes()):
            self.assertIsNone(self.call("import_offline_pack", str(self.pack.path))["warning"])
        self.assertEqual((len(catalog_document()["mods"]), 1), (self.mod_count(), self.mod_count(Settings(offline_mode=True))))

        with failing(OSError("network down")):
            self.assertIn("Could not refresh", self.call("import_offline_pack", str(self.pack.path))["warning"])
        self.assertEqual(1, self.mod_count(Settings(offline_mode=True)))

    def test_uninstall_and_clear_delete_pack_caches_and_logs_but_keep_settings(self) -> None:
        self.install()
        self.write({"test.cache": "cache"}, self.store.cache_dir)
        self.store.save_settings(Settings(offline_mode=True))
        self.call("_unload")
        self.call("_uninstall")
        self.assertFalse(self.store.pack_dir.exists())
        self.assertEqual([], [path for path in self.store.cache_dir.rglob("*") if path.is_file()])
        self.assertTrue(self.store.load_settings().offline_mode)

        with ExitStack() as stack:
            plugin_log = file_logger(stack, "test.clear.rpc", self.store.log_path)
            plugin_log.info("old log")
            with patch.object(self.main, "log", plugin_log):
                self.assertEqual({}, self.call("clear_local_data"))
            self.assertFalse(self.store.log_path.exists())
            self.assertTrue(self.store.settings_path.exists())
            plugin_log.info("new log")
            self.assertIn("new log", self.store.log_path.read_text(encoding="utf-8"))
