from support.builders import catalog_mod
from support.catalog import CatalogEditTest

from scanner.mod_catalog import CATALOG_SCHEMA_VERSION, summarize_credits
from store.mod_sources import mod_readme_path


class CatalogRuleTests(CatalogEditTest):
    """What a catalog must look like before the plugin accepts any of it."""

    def test_only_the_supported_schema_version_loads(self) -> None:
        self.document["mods"] = self.document["mods"][:1]
        versions = (CATALOG_SCHEMA_VERSION - 1, str(CATALOG_SCHEMA_VERSION), float(CATALOG_SCHEMA_VERSION), True)
        self.assert_rejected(self.document, "schema_version", versions, "Unsupported mod catalog schema version")
        self.assertEqual(1, len(self.load()))

    def test_paths_must_stay_inside_the_repository_and_the_game(self) -> None:
        entry = self.entry(self.document["mods"][0]["id"])
        self.assert_rejected(entry["source"], "path", ["../outside.zip"], "must be a safe relative path")
        self.assert_rejected(entry, "files", [["../outside.dll", *entry["files"][1:]]], "must be a safe relative path")

    def test_executable_is_required_unless_manual_and_alternatives_are_checked(self) -> None:
        first = self.document["mods"][0]
        manual = self.only("dbxv2fix")
        del manual["executable"]
        self.assert_rejected(manual, "manual", [False, "yes"])
        self.document["mods"] = [first]
        self.assert_rejected(first, "executable_alternatives",
                             ["other.exe", [""], [123], ["../outside.exe"], [first["executable"]]])

    def test_backups_must_name_distinct_catalogued_files_beside_them(self) -> None:
        entry = self.only("ai-the-somnium-files")
        original = entry["backups"]
        target = original[0]["target"]
        self.assert_rejected(entry, "backups", (
            None, {}, "backup", [None], [{"target": target}],
            [{"target": target, "backup": 123}],
            [{"target": target, "backup": target, "extra": True}],
            [{"target": "missing.dll", "backup": "missing.dll.bak"}],
            [{"target": target, "backup": "../outside.bak"}],
            [{"target": target, "backup": "other/original.bak"}],
            [{"target": target, "backup": entry["files"][1]}],
            [{"target": target, "backup": target.upper()}],
            [{"target": entry["ini_file"], "backup": "AI1_ARFix.ini.bak"}],
            original * 2,
            original + [{"target": entry["files"][1], "backup": original[0]["backup"].upper()}],
        ))
        (backup,) = self.load()[0].file_backups
        self.assertEqual((target, original[0]["backup"]), (backup.target, backup.backup))
        self.assert_rejected(entry, "backup_file", [original[0]["backup"]])

        undertale = catalog_mod("undertale")
        self.assertEqual(((), ("data.win", "data.win.bak")),
                         (undertale.backups, (undertale.file_backups[0].target, undertale.file_backups[0].backup)))

    def test_ini_resolution_fields_must_be_well_formed_and_unique(self) -> None:
        entry = self.only("ori-and-the-will-of-the-wisps")
        pair = entry["ini_resolution"][0]
        self.assert_rejected(entry, "ini_resolution", (
            "width",
            [{**pair, "kind": "guess"}],
            [{**pair, "width": "TargetWidth\nOther"}],
            [{**pair, "height": "targetwidth"}],
            [pair, pair],
            [{"section": "UserSettings", "key": "Resolution", "kind": "pixels"}],
        ), "ini_resolution")
        self.assert_rejected(entry, "ini_resolution_strings", (
            "key",
            [{"section": "UserSettings", "key": "Resolution", "kind": "pixels"}],
            [{"section": "AspectRatio", "key": "TargetWidth"}],
        ), "ini_resolution")

    def test_display_tags_must_be_unique_safe_identifiers(self) -> None:
        entry = self.only("monster-hunter-rise")
        self.assert_rejected(entry, "display_tags", (["stretch", "stretch"], ["bad tag"], [1], "stretch"), "display_tags")

    def test_offline_launcher_must_swap_two_distinct_files_beside_each_other(self) -> None:
        entry = self.only("elden-ring-ultrawide-fix")
        launcher = dict(entry["launcher_replacement"])
        changes = (
            {"source": "../outside.exe"}, {"source": "Game/missing.exe"}, {"target": "Game/eldenring.exe"},
            {"target": launcher["source"]}, {"backup": "Game/dinput8.dll"}, {"backup": "Game/modengine2_launcher.exe"},
            {"backup": launcher["target"]}, {"backup": "other/backup.exe"}, {"source": 123}, {"extra": "value"},
        )
        self.assert_rejected(entry, "launcher_replacement", [{**launcher, **change} for change in changes])
        self.assert_rejected(entry, "offline_only", [False, "true", 1])

    def test_file_hashes_only_cover_listed_loose_files(self) -> None:
        trials = self.entry("trials-of-mana")["source"]
        archive = next(entry for entry in self.document["mods"] if entry["source"]["kind"] == "archive")["source"]
        self.assert_rejected(trials, "file_hashes", (
            {"Trials of Mana/Binaries/Win64/dxgi.dll": "not-a-hash"}, {"unlisted.dll": "0" * 64}, ["0" * 64],
        ), "source.file_hashes")
        self.assert_rejected(archive, "file_hashes", [{"any.dll": "0" * 64}], "source.file_hashes")

    def test_credits_default_to_the_owner_and_need_https_links(self) -> None:
        self.document["mods"] = [entry for entry in self.document["mods"] if "credits" not in entry]
        first = self.document["mods"][0]
        first["credits"] = [{"name": "Other", "url": "https://example.com/o"}]
        authors = summarize_credits(self.load())
        owned = len(self.document["mods"]) - 1
        self.assertEqual([("TLin-Y", owned), ("Other", 1)], [(author["name"], author["mod_count"]) for author in authors])
        self.assert_rejected(first, "credits", (
            [{"name": "Bad", "url": "http://x"}],
            [{"name": "Bad", "support": [{"label": "Pay", "url": "http://x"}]}],
        ))

    def test_readme_sits_beside_an_archive(self) -> None:
        self.assertEqual("Dark Souls III/README.md", mod_readme_path(catalog_mod("dark-souls-iii")))
        self.document["mods"] = self.document["mods"][:1]
        self.document["mods"][0]["source"]["path"] = "external/Lyall/SekiroFix/SekiroFix_0.0.1.zip"
        self.assertEqual("external/Lyall/SekiroFix/README.md", mod_readme_path(self.load()[0]))
