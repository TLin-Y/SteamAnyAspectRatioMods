import io
import json
from dataclasses import replace
from http.client import IncompleteRead
from unittest.mock import patch

from support.builders import write_zip
from support.catalog import catalog_document, catalog_size
from support.fakes import failing, serving
from support.packs import (
    CATALOG_MEMBER,
    PACK_FILES,
    README_MEMBER,
    PackTest,
    split_pack,
)

from installer.apply import apply_mod_files
from store.dependencies import load_active_catalog, update_dependencies
from store.mod_sources import fetch_mod_readme
from store.offline_pack import archive_names, parse_part_name
from store.plugin_store import Settings

SINGLE = "AnyAspectRatio-Offline-Mods-1.4.0.zip"


class OfflinePackTests(PackTest):
    def test_pack_file_names(self) -> None:
        self.assertEqual(["AnyAspectRatio-Offline-Mods-1.9.0.zip"], archive_names("1.9.0", 1))
        parts = archive_names("1.9.0-rc-1", 3)
        self.assertEqual("AnyAspectRatio-Offline-Mods-1.9.0-rc-1-part2-of-3.zip", parts[1])
        self.assertEqual([("1.9.0-rc-1", index, 3) for index in (1, 2, 3)], [parse_part_name(name) for name in parts])
        for name in ("AnyAspectRatio-Offline-Mods-1.9.0.zip", "AnyAspectRatio-Offline-Mods--part1-of-2.zip",
                     "AnyAspectRatio-Offline-Mods-1.9.0-partX-of-2.zip", "Other-1.9.0-part1-of-2.zip",
                     "AnyAspectRatio-Offline-Mods-1.9.0-part1-of-2.zip.tmp"):
            with self.subTest(name=name):
                self.assertIsNone(parse_part_name(name))

    def test_import_keeps_only_what_the_catalog_needs_and_works_offline(self) -> None:
        self.assertEqual((1, PACK_FILES), (self.install()["mod_count"], self.pack_files()))
        self.assertFalse((self.store.pack_dir / "compiler").exists())
        self.assert_works_offline()

    def test_selecting_one_part_imports_the_whole_set(self) -> None:
        first, _ = split_pack(self.pack, self.folder("parts"))
        self.assertEqual(1, self.install(first)["mod_count"])
        self.assertTrue((self.store.pack_dir / self.pack.mod_path).is_file())
        with failing(OSError("offline")):
            self.assertEqual("# DS3", fetch_mod_readme(self.packed_mod()))

    def test_a_broken_import_keeps_the_existing_pack(self) -> None:
        self.install()
        first, second = split_pack(self.pack, self.folder("parts"))
        without_archive = write_zip(self.root / "broken.zip", {
            name: content for name, content in self.pack.members.items() if not name.endswith(".zip")})
        cases = (
            ("a missing part", lambda: second.unlink(), first, "Keep all 2 offline pack parts"),
            ("parts with different catalogs", lambda: write_zip(second, {
                CATALOG_MEMBER: b'{"schema_version":3,"mods":[]}', README_MEMBER: b"# DS3"}), (first, second),
             "different catalogs"),
            ("a missing mod archive", lambda: None, without_archive, ""),
        )
        for what, damage, selection, error in cases:
            with self.subTest(what):
                split_pack(self.pack, self.root / "parts")
                damage()
                with self.assertRaisesRegex(ValueError, error):
                    self.install(selection)
                self.assertEqual(PACK_FILES, self.pack_files())
                self.assertTrue((self.store.pack_dir / self.pack.mod_path).is_file())

    def test_readme_prefers_online_and_falls_back_to_a_complete_pack_file(self) -> None:
        self.install()

        class InterruptedStream(io.BytesIO):
            def read(self, size: int = -1) -> bytes:
                if self.tell() == 0:
                    return super().read(3)
                raise IncompleteRead(b"partial")

        with serving(b"# DS3 newer"):
            self.assertEqual("# DS3 newer", fetch_mod_readme(self.packed_mod()))
        for what, network in (("network down", failing(OSError("down"))),
                              ("interrupted download", patch("utils.network.urlopen",
                                                             return_value=InterruptedStream(b"partial")))):
            with self.subTest(what), network:
                self.assertEqual("# DS3", fetch_mod_readme(self.packed_mod()))

    def test_outdated_pack_archive_is_never_applied(self) -> None:
        self.install()
        newer = replace(self.packed_mod(), source=replace(self.packed_mod().source, sha256="a" * 64))
        game = self.folder("game")
        with failing(OSError("down")), self.assertRaises(OSError):
            apply_mod_files(newer, str(game))
        self.assertFalse((game / "Game").exists())

    def test_import_keeps_the_online_catalog_while_offline_uses_the_pack_one(self) -> None:
        online = catalog_document()
        with serving(json.dumps(online).encode()):
            update_dependencies(self.store, Settings())
        self.install()
        self.assertEqual(catalog_size(online), len(load_active_catalog(self.store, Settings()).mods))
        (packed,) = load_active_catalog(self.store, Settings(offline_mode=True)).mods
        self.assert_works_offline(packed)

    def test_local_data_can_be_cleared_and_measured(self) -> None:
        self.install()
        self.write({"test.log": "log"}, self.store.log_dir)
        self.write({"test.cache": "cache"}, self.store.cache_dir)
        self.assertEqual(sum(path.stat().st_size for path in self.store.data_dir.rglob("*") if path.is_file()),
                         self.store.disk_usage())
        self.store.save_settings(Settings(local_archive_dir=""))
        self.store.clear_local_data()
        self.assertEqual(0, self.pack_files())
        self.assertFalse(self.store.catalog_path.exists())
        self.assertTrue(self.store.archive_cache_dir.is_dir())
        self.assertTrue(self.store.settings_path.is_file())
