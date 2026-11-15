import json
from dataclasses import replace

from support.builders import catalog_mod
from support.fakes import failing, fetched, no_network, offline, serving
from support.packs import StoreTest
from support.sandbox import PLUGIN_ROOT

from store.dependencies import (
    _with_bundled_resolution_metadata,
    load_active_catalog,
    refresh_catalog_on_start,
    update_dependencies,
)
from store.plugin_store import Settings

CATALOG = (PLUGIN_ROOT / "catalog.json").read_bytes()


class CatalogUpdateTests(StoreTest):
    def origin(self, settings: Settings = Settings()) -> str:
        return load_active_catalog(self.store, settings).origin

    def stale_archive(self) -> None:
        self.write({"old.zip": "x"}, self.store.archive_cache_dir)

    def has_stale_archive(self) -> bool:
        return (self.store.archive_cache_dir / "old.zip").exists()

    def test_update_caches_the_catalog_for_its_source_and_drops_old_archives(self) -> None:
        self.assertEqual("bundled", self.origin())
        document = json.loads(CATALOG)
        document["mods"] = document["mods"][:2]
        self.stale_archive()
        dev = Settings(catalog_ref="dev")
        with serving(json.dumps(document).encode()) as network:
            result = update_dependencies(self.store, dev)
            self.assertFalse(update_dependencies(self.store, dev)["catalog_changed"])
        self.assertIn("/dev/AnyAspectRatioDecky/catalog.json", fetched(network)[0])
        self.assertEqual((True, 2, False), (result["catalog_changed"], result["mod_count"], self.has_stale_archive()))
        source = load_active_catalog(self.store, dev)
        self.assertEqual(("cache", 2, "dev"), (source.origin, len(source.mods), source.mods[0].source.repository_ref))
        self.assertEqual("bundled", self.origin())  # a cache fetched for another source is never used

    def test_a_bad_download_or_cache_falls_back_to_a_working_catalog(self) -> None:
        with serving(CATALOG):
            update_dependencies(self.store, Settings())
        with serving(b"{not json"), self.assertRaises(ValueError):
            update_dependencies(self.store, Settings())
        self.assertEqual("cache", self.origin())
        self.assertEqual([], list(self.store.cache_dir.glob("*.tmp")))

        self.store.catalog_path.write_text("{}", encoding="utf-8")
        source = load_active_catalog(self.store, Settings())
        self.assertEqual("bundled", source.origin)
        self.assertIsNotNone(source.warning)

    def test_startup_refresh_drops_archives_only_when_the_catalog_changed(self) -> None:
        with failing(OSError("offline")), self.assertRaises(OSError):
            refresh_catalog_on_start(self.store, Settings())
        self.assertEqual("bundled", self.origin())
        for changed in (True, False):
            with self.subTest(changed=changed), serving(CATALOG):
                self.stale_archive()
                self.assertEqual(changed, refresh_catalog_on_start(self.store, Settings()))
                self.assertEqual(not changed, self.has_stale_archive())
                self.assertEqual("cache", self.origin())

    def test_startup_refresh_replaces_an_imported_offline_pack_catalog(self) -> None:
        with serving(CATALOG):
            update_dependencies(self.store, Settings())
        meta = json.loads(self.store.catalog_meta_path.read_text(encoding="utf-8"))
        self.store.catalog_meta_path.write_text(json.dumps({**meta, "source_url": "offline pack", "sha256": "0" * 64}),
                                                encoding="utf-8")
        with serving(CATALOG):
            self.assertTrue(refresh_catalog_on_start(self.store, Settings()))
        self.assertNotEqual("offline pack", json.loads(self.store.catalog_meta_path.read_text(encoding="utf-8"))["source_url"])

    def test_offline_mode_never_touches_the_network(self) -> None:
        with offline(), no_network():
            with self.assertRaises(OSError):
                update_dependencies(self.store, Settings(offline_mode=True))
            self.assertIsNone(refresh_catalog_on_start(self.store, Settings(offline_mode=True)))

    def test_old_official_catalogs_get_resolution_fields_only_for_unchanged_archives(self) -> None:
        ori = catalog_mod("ori-and-the-will-of-the-wisps")
        legacy = replace(ori, ini_resolution=())
        changed = replace(legacy, source=replace(legacy.source, sha256="0" * 64))
        other_repository = Settings(catalog_repository_url="https://github.com/example/other")
        for entry, settings, expected in ((legacy, Settings(), ori.ini_resolution), (changed, Settings(), ()),
                                          (legacy, other_repository, ())):
            with self.subTest(sha256=entry.source.sha256[:8], repository=settings.catalog_repository_url):
                self.assertEqual(expected, _with_bundled_resolution_metadata((entry,), settings)[0].ini_resolution)
