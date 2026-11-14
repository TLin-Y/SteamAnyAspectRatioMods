import json
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch
from zipfile import ZipFile

from scanner.mod_catalog import load_mod_catalog, summarize_credits
from scanner.mod_installer import mod_readme_path
from scanner.steam_library_scanner import InstalledApp, SteamScanReport
from scanner.supported_game_scanner import SupportedGame, match_supported_games


class ModCatalogTests(unittest.TestCase):
    def test_ff13_combined_package_and_installation(self) -> None:
        import configparser

        from scanner.mod_installer import apply_mod_files, remove_mod_files

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "final-fantasy-xiii")
        root = Path(__file__).resolve().parents[2]
        external = root / "external/rebtd7/FF13Fix"
        bundle = root / mod.source.path
        destination = "white_data/prog/win/bin"
        self.assertEqual(destination, mod.source.destination)
        self.assertEqual(("TLin-Y", "rebtd7"), tuple(credit.name for credit in mod.credits))
        self.assertEqual(("d3d9,dinput8=n,b",), mod.wine_dll_overrides)
        self.assertEqual(f"{destination}/FF13_ARFix.ini", mod.ini_file)
        self.assertEqual(
            {f"{destination}/{name}" for name in (
                "FF13_ARFix.asi", "FF13_ARFix.ini", "dinput8.dll",
                "d3d9.dll", "FF13Fix.ini",
            )},
            set(mod.files),
        )
        with ZipFile(bundle) as archive, ZipFile(external / "FF13Fix_164_.zip") as upstream:
            self.assertIsNone(archive.testzip())
            self.assertEqual(
                {"FF13_ARFix.asi", "FF13_ARFix.ini", "dinput8.dll", "d3d9.dll",
                 "FF13Fix.ini", "LICENSE-Ultimate-ASI-Loader.txt",
                 "FF13Fix_Licences/FF13Fix_LICENSE.txt", "FF13Fix_Licences/MinHook_LICENSE.txt"},
                set(archive.namelist()),
            )
            for name in (
                "d3d9.dll", "FF13Fix.ini",
                "FF13Fix_Licences/FF13Fix_LICENSE.txt", "FF13Fix_Licences/MinHook_LICENSE.txt",
            ):
                self.assertEqual(upstream.read(f"FF13Fix_164/{name}"), archive.read(name))
            upstream_ini = archive.read("FF13Fix.ini")
        with ZipFile(root / "Final Fantasy XIII/Final-Fantasy-XIII-v1.0.0.zip") as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(
                (root / "Final Fantasy XIII/README.md").read_bytes(), archive.read("README.md")
            )
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            target = game / destination
            target.mkdir(parents=True)
            (target / "d3d9.dll").write_bytes(b"existing proxy")
            with patch("scanner.mod_installer._pack_file", return_value=bundle):
                conflicts = apply_mod_files(mod, str(game), ini_resolution=(1920, 1080))
                self.assertEqual([f"{destination}/d3d9.dll"], conflicts)
                self.assertEqual(b"existing proxy", (target / "d3d9.dll").read_bytes())
                self.assertFalse((target / "FF13_ARFix.asi").exists())
                apply_mod_files(mod, str(game), replace_existing_files=True,
                                ini_resolution=(1920, 1080))
                self.assertEqual(upstream_ini, (target / "FF13Fix.ini").read_bytes())
                config = configparser.ConfigParser()
                config.read(target / "FF13_ARFix.ini", encoding="utf-8")
                self.assertEqual(
                    (1280, 720, 1920, 1080),
                    tuple(config.getint("Resolution", name) for name in
                          ("ReplaceWidth", "ReplaceHeight", "TargetWidth", "TargetHeight")),
                )
                remove_mod_files(mod, str(game), preserve_modified_ini=False)
            self.assertEqual([], list(target.iterdir()) if target.exists() else [])

    def test_dragontweak_verified_paths_apply_and_remove(self) -> None:
        from scanner.mod_installer import apply_mod_files, read_mod_ini, remove_mod_files

        expected = (
            ("3937550", "YakuzaKiwami3", "runtime/media/startup.exe"),
            ("3061810", "LikeADragonPirates", "runtime/media/startup.exe"),
            ("2072450", "LikeADragon8", "runtime/media/LikeADragon8.exe"),
            ("2375550", "LikeADragonGaiden", "runtime/media/startup.exe"),
            ("2058190", "Lost Judgment", "runtime/media/startup.exe"),
            ("1235140", "Yakuza Like a Dragon", "runtime/media/startup.exe"),
            ("2058180", "Judgment", "runtime/media/startup.exe"),
            ("3717340", "Yakuza Kiwami 2 R", "runtime/media/startup.exe"),
            ("927380", "Yakuza Kiwami 2", "YakuzaKiwami2.exe"),
            ("1388590", "Yakuza 6 - The Song of Life", "Yakuza6.exe"),
            ("2988580", "Yakuza 0 Directors Cut", "runtime/media/startup.exe"),
            ("638970", "Yakuza 0", "media/Yakuza0.exe"),
            ("3717330", "Yakuza Kiwami R", "runtime/media/startup.exe"),
            ("834530", "Yakuza Kiwami", "media/YakuzaKiwami.exe"),
            ("1105510", "Yakuza 5", "main/Yakuza5.exe"),
            ("1105500", "Yakuza 4", "Yakuza4.exe"),
            ("1088710", "Yakuza 3", "Yakuza3.exe"),
        )
        mods = {
            mod.steam_app_id: mod for mod in load_mod_catalog()
            if mod.mod_id.startswith("dragontweak-")
        }
        root = Path(__file__).resolve().parents[2]
        archive_path = root / "external/Lyall/DragonTweak/DragonTweak_0.0.6.zip"
        with ZipFile(archive_path) as archive:
            self.assertIsNone(archive.testzip())
            contents = {
                name: archive.read(name)
                for name in ("dinput8.dll", "DragonTweak.asi", "DragonTweak.ini")
            }
        for app_id, install_dir, executable in expected:
            with self.subTest(app_id=app_id), tempfile.TemporaryDirectory() as directory:
                mod = mods[app_id]
                self.assertFalse(mod.manual)
                self.assertEqual((install_dir, executable), (mod.install_dir, mod.executable))
                destination = executable.rpartition("/")[0]
                prefix = f"{destination}/" if destination else ""
                self.assertEqual(destination, mod.source.destination)
                self.assertEqual(tuple(f"{prefix}{name}" for name in contents), mod.files)
                self.assertEqual(f"{prefix}DragonTweak.ini", mod.ini_file)
                self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
                game = Path(directory) / install_dir
                exe = game / executable
                exe.parent.mkdir(parents=True)
                exe.write_bytes(b"game")
                app = InstalledApp(app_id, install_dir, install_dir, directory, str(game))
                report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
                (matched,) = report.supported_games
                self.assertEqual(str(exe), matched.executable_path)
                self.assertTrue(matched.executable_found)
                self.assertFalse(matched.to_dict()["manual"])
                with patch("scanner.mod_installer._pack_file", return_value=archive_path):
                    apply_mod_files(mod, str(game))
                for name, content in contents.items():
                    self.assertEqual(content, (exe.parent / name).read_bytes())
                ini, exists = read_mod_ini(mod, str(game))
                self.assertEqual(contents["DragonTweak.ini"].decode("utf-8-sig"), ini)
                self.assertTrue(exists)
                remove_mod_files(mod, str(game))
                self.assertEqual(b"game", exe.read_bytes())
                self.assertEqual([exe], [path for path in game.rglob("*") if path.is_file()])
        self.assertFalse(mods["3937550"].manual)

    def test_backups_array_validation_and_legacy_compatibility(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        entry = next(mod for mod in document["mods"] if mod["id"] == "ai-the-somnium-files")
        document["mods"] = [entry]
        original = entry["backups"]
        target = original[0]["target"]
        invalid = (
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
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            for value in invalid:
                with self.subTest(value=value):
                    entry["backups"] = value
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_mod_catalog(path)
            entry["backups"] = original
            path.write_text(json.dumps(document), encoding="utf-8")
            mod = load_mod_catalog(path)[0]
            self.assertEqual((target, original[0]["backup"]),
                             (mod.file_backups[0].target, mod.file_backups[0].backup))
            entry["backup_file"] = original[0]["backup"]
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)
        legacy = next(mod for mod in load_mod_catalog() if mod.mod_id == "undertale")
        self.assertEqual((), legacy.backups)
        self.assertEqual(("data.win", "data.win.bak"),
                         (legacy.file_backups[0].target, legacy.file_backups[0].backup))

    def test_ai1_release_paths_and_defaults(self) -> None:
        import configparser

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "ai-the-somnium-files")
        self.assertEqual("948740", mod.steam_app_id)
        self.assertEqual("AI The Somnium Files", mod.install_dir)
        self.assertEqual("AI_TheSomniumFiles.exe", mod.executable)
        self.assertEqual((), mod.wine_dll_overrides)
        self.assertIsNone(mod.backup_file)
        self.assertEqual(1, len(mod.backups))
        self.assertEqual(mod.files[0], mod.backups[0].target)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(mod.files) | {"README.md", "43.jpg", "ultrawide.jpg"},
                             set(archive.namelist()))
            for name in ("README.md", "43.jpg", "ultrawide.jpg"):
                self.assertEqual((root / "AI The Somnium Files" / name).read_bytes(), archive.read(name))
            config = configparser.ConfigParser()
            config.read_string(archive.read(mod.ini_file).decode("utf-8-sig"))
            self.assertEqual(1280, config.getint("Resolution", "Width"))
            self.assertEqual(800, config.getint("Resolution", "Height"))
            self.assertTrue(config.getboolean("Resolution", "PreserveHUDAspect"))

    def test_apollo_justice_release_paths_and_defaults(self) -> None:
        import configparser

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "apollo-justice-ace-attorney-trilogy")
        self.assertEqual("2187220", mod.steam_app_id)
        self.assertEqual("Apollo Justice Ace Attorney Trilogy", mod.install_dir)
        self.assertEqual("GS456.exe", mod.executable)
        self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(mod.files) | {"README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                             set(archive.namelist()))
            self.assertEqual((root / "Apollo Justice Ace Attorney Trilogy/README.md").read_bytes(),
                             archive.read("README.md"))
            config = configparser.ConfigParser()
            config.read_string(archive.read(mod.ini_file).decode("utf-8-sig"))
            self.assertEqual(1280, config.getint("Window", "Width"))
            self.assertEqual(800, config.getint("Window", "Height"))
            self.assertEqual(1280, config.getint("Scene", "BackgroundHeight"))

    def test_dead_cells_release_paths_and_defaults(self) -> None:
        import configparser

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "dead-cells")
        self.assertEqual("588650", mod.steam_app_id)
        self.assertEqual("Dead Cells", mod.install_dir)
        self.assertEqual("deadcells.exe", mod.executable)
        self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
        self.assertEqual((), mod.display_tags)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(mod.files) | {"README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                             set(archive.namelist()))
            self.assertEqual((root / "Dead Cells/README.md").read_bytes(), archive.read("README.md"))
            config = configparser.ConfigParser()
            config.read_string(archive.read(mod.ini_file).decode("utf-8-sig"))
            self.assertEqual(1280, config.getint("Window", "Width"))
            self.assertEqual(800, config.getint("Window", "Height"))
            self.assertTrue(config.getboolean("AspectRatio", "Enabled"))

    def test_devil_may_cry_5_release_paths_and_defaults(self) -> None:
        import configparser

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "devil-may-cry-5")
        self.assertEqual("601150", mod.steam_app_id)
        self.assertEqual("Devil May Cry 5", mod.install_dir)
        self.assertEqual("DevilMayCry5.exe", mod.executable)
        self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
        self.assertEqual(("stretch",), mod.display_tags)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(mod.files) | {"README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                             set(archive.namelist()))
            self.assertEqual((root / "Devil May Cry 5/README.md").read_bytes(), archive.read("README.md"))
            config = configparser.ConfigParser()
            config.read_string(archive.read(mod.ini_file).decode("utf-8-sig"))
            self.assertTrue(config.getboolean("AspectRatio", "EnableARFix"))
            self.assertEqual(16, config.getint("AspectRatio", "AspectWidth"))
            self.assertEqual(10, config.getint("AspectRatio", "AspectHeight"))

    def test_pwaat_release_paths_and_defaults(self) -> None:
        import configparser

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "phoenix-wright-ace-attorney-trilogy")
        self.assertEqual("787480", mod.steam_app_id)
        self.assertEqual("Phoenix Wright Ace Attorney Trilogy", mod.install_dir)
        self.assertEqual("PWAAT.exe", mod.executable)
        self.assertEqual(("must-read-readme", "new-res-in-game-menu"), mod.display_tags)
        self.assertEqual((), mod.wine_dll_overrides)
        self.assertEqual(1, len(mod.backups))
        self.assertEqual(mod.files[0], mod.backups[0].target)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(mod.files) | {"README.md"}, set(archive.namelist()))
            self.assertEqual((root / "Phoenix Wright Ace Attorney Trilogy/README.md").read_bytes(),
                             archive.read("README.md"))
            config = configparser.ConfigParser()
            config.read_string(archive.read(mod.ini_file).decode("utf-8-sig"))
            self.assertEqual(1280, config.getint("Resolution", "Width"))
            self.assertEqual(800, config.getint("Resolution", "Height"))

    def test_invalid_ini_resolution_metadata_is_rejected(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        entry = next(mod for mod in document["mods"] if mod["id"] == "ori-and-the-will-of-the-wisps")
        document["mods"] = [entry]
        original = entry["ini_resolution"][0]
        invalid = (
            "width",
            [{"section": "AspectRatio", "width": "TargetWidth", "height": "TargetHeight", "kind": "guess"}],
            [{**original, "width": "TargetWidth\nOther"}],
            [{**original, "height": "targetwidth"}],
            [original, original],
            [{"section": "UserSettings", "key": "Resolution", "kind": "pixels"}],
        )
        invalid_strings = (
            "key",
            [{"section": "UserSettings", "key": "Resolution", "kind": "pixels"}],
            [{"section": "AspectRatio", "key": "TargetWidth"}],
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            for field, values in (("ini_resolution", invalid), ("ini_resolution_strings", invalid_strings)):
                for value in values:
                    with self.subTest(field=field, value=value):
                        entry.pop("ini_resolution_strings", None)
                        entry["ini_resolution"] = [original]
                        entry[field] = value
                        path.write_text(json.dumps(document), encoding="utf-8")
                        with self.assertRaisesRegex(ValueError, "ini_resolution"):
                            load_mod_catalog(path)

    def test_ini_resolution_stays_readable_by_released_plugins(self) -> None:
        # Plugins up to 1.8.0 refuse the whole catalog on any other ini_resolution shape or kind.
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        for entry in document["mods"]:
            for pair in entry.get("ini_resolution", []):
                with self.subTest(mod=entry["id"]):
                    self.assertEqual({"section", "width", "height", "kind"}, set(pair))
                    self.assertIn(pair["kind"], ("pixels", "ratio"))

    def test_ini_resolution_metadata_matches_catalogued_ini_contents(self) -> None:
        root = Path(__file__).resolve().parents[2]
        mods = load_mod_catalog()
        configured = [mod for mod in mods if mod.ini_resolution]
        self.assertEqual(67, len(configured))
        for mod in configured:
            with self.subTest(mod=mod.mod_id):
                if mod.source.kind == "archive":
                    prefix = f"{mod.source.destination}/" if mod.source.destination else ""
                    name = mod.ini_file.removeprefix(prefix)
                    with ZipFile(root / mod.source.path) as archive:
                        content = archive.read(name).decode("utf-8-sig")
                elif mod.source.kind == "files":
                    source = next(item for item in mod.source.files if item.destination == mod.ini_file)
                    content = (root / source.path).read_text(encoding="utf-8-sig")
                elif mod.source.kind == "directory":
                    content = (root / mod.source.path / mod.ini_file).read_text(encoding="utf-8-sig")
                else:
                    self.fail(f"Unsupported source for {mod.mod_id}")
                sections = {}
                current = ""
                for line in content.splitlines():
                    if line.strip().startswith("[") and "]" in line:
                        current = line.strip()[1:].split("]", 1)[0].strip().lower()
                    elif "=" in line and not line.lstrip().startswith((";", "#")):
                        sections.setdefault(current, []).append(line.split("=", 1)[0].strip().lower())
                for pair in mod.ini_resolution:
                    keys = sections.get(pair.section.lower(), [])
                    self.assertEqual(1, keys.count(pair.width.lower()))
                    self.assertEqual(1, keys.count(pair.height.lower()))
        origins = next(mod for mod in mods if mod.mod_id == "dynasty-warriors-origins")
        self.assertEqual({"Width", "Height"}, {
            key for pair in origins.ini_resolution for key in (pair.width, pair.height)
        })

    def test_display_tags_are_catalogued_for_game_title(self) -> None:
        mods = {mod.mod_id: mod for mod in load_mod_catalog()}
        expected = {
            "devil-may-cry-5",
            "dynasty-warriors-7-xtreme-legends-definitive-edition",
            "dynasty-warriors-8-xtreme-legends-complete-edition",
            "monster-hunter-rise",
            "rayman-legends",
            "persona-5-royal",
            "resident-evil-2",
            "resident-evil-3",
            "resident-evil-4",
            "resident-evil-7",
            "resident-evil-village",
            "samurai-warriors-spirit-of-sanada",
            "warriors-orochi-3-ultimate",
            "warriors-orochi-4-ultimate",
            "ultrawidefix-lrff13",
        }

        self.assertEqual(
            expected,
            {mod_id for mod_id, mod in mods.items() if "stretch" in mod.display_tags},
        )
        self.assertTrue(all("(Stretch)" not in mods[mod_id].name for mod_id in expected))

    def test_display_tags_must_be_unique_safe_identifiers(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        entry = next(mod for mod in document["mods"] if mod["id"] == "monster-hunter-rise")
        document["mods"] = [entry]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            for value in (["stretch", "stretch"], ["bad tag"], [1], "stretch"):
                with self.subTest(display_tags=value):
                    entry["display_tags"] = value
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, "display_tags"):
                        load_mod_catalog(path)

    def test_readme_sits_beside_archive_zips(self) -> None:
        from scanner.mod_installer import mod_readme_path

        mods = {mod.mod_id: mod for mod in load_mod_catalog()}
        self.assertEqual("Dark Souls III/README.md", mod_readme_path(mods["dark-souls-iii"]))

        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        document["mods"] = [document["mods"][0]]
        document["mods"][0]["source"]["path"] = "external/Lyall/SekiroFix/SekiroFix_0.0.1.zip"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            self.assertEqual(
                "external/Lyall/SekiroFix/README.md",
                mod_readme_path(load_mod_catalog(path)[0]),
            )

    def test_credits_default_override_and_ranking(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        document["mods"] = [mod for mod in document["mods"] if "credits" not in mod]
        document["mods"][0]["credits"] = [{"name": "Other", "url": "https://example.com/o"}]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            authors = summarize_credits(load_mod_catalog(path))
            self.assertEqual(["TLin-Y", "Other"], [author["name"] for author in authors])
            self.assertEqual([40, 1], [author["mod_count"] for author in authors])

            document["mods"][0]["credits"] = [{"name": "Bad", "url": "http://x"}]
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)

            document["mods"][0]["credits"] = [
                {"name": "Bad", "support": [{"label": "Pay", "url": "http://x"}]}
            ]
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)

        everyone = {a["name"]: a for a in summarize_credits(load_mod_catalog())}
        self.assertEqual(("", 42), (everyone["TLin-Y"]["url"], everyone["TLin-Y"]["mod_count"]))
        self.assertEqual(["Mod list", "Ko-fi"], [link["label"] for link in everyone["TLin-Y"]["support"]])
        self.assertEqual(
            ["Patreon", "Ko-fi"], [link["label"] for link in everyone["Lyall"]["support"]]
        )

    def test_loads_audited_repository_catalog(self) -> None:
        catalog = [mod for mod in load_mod_catalog() if not mod.credits or mod.credits[0].name == "TLin-Y"]
        mod = next(mod for mod in catalog if mod.steam_app_id == "3841510")
        repository_root = Path(__file__).resolve().parents[2]

        self.assertEqual(42, len(catalog))
        self.assertEqual("DW3CE_RE", mod.install_dir)
        self.assertEqual(3, len(mod.files))
        self.assertEqual(
            39,
            sum(candidate.source.kind == "archive" for candidate in catalog),
        )
        self.assertEqual(
            1,
            sum(candidate.source.kind == "directory" for candidate in catalog),
        )
        self.assertEqual(
            2,
            sum(candidate.source.kind == "files" for candidate in catalog),
        )
        for candidate in catalog:
            self.assertTrue(
                (repository_root / mod_readme_path(candidate)).is_file(),
                candidate.mod_id,
            )
            if candidate.source.kind == "archive":
                self.assertIsNotNone(candidate.source.path)
                archive_path = repository_root / candidate.source.path
                self.assertTrue(archive_path.is_file(), candidate.mod_id)
                with ZipFile(archive_path) as archive:
                    members = set(archive.namelist())
                for installed_file in candidate.files:
                    relative_path = Path(installed_file)
                    if candidate.source.destination:
                        relative_path = relative_path.relative_to(
                            candidate.source.destination
                        )
                    self.assertIn(relative_path.as_posix(), members, candidate.mod_id)
            elif candidate.source.kind == "directory":
                source_directory = repository_root / candidate.source.path
                self.assertTrue(source_directory.is_dir(), candidate.mod_id)
                for installed_file in candidate.files:
                    self.assertTrue(
                        (source_directory / installed_file).is_file(),
                        candidate.mod_id,
                    )
            else:
                for source_file in candidate.source.files:
                    self.assertTrue(
                        (repository_root / source_file.path).is_file(),
                        candidate.mod_id,
                    )

    def test_ffxii_package_and_install_paths(self) -> None:
        import configparser

        mod = next(
            mod for mod in load_mod_catalog()
            if mod.mod_id == "final-fantasy-xii-the-zodiac-age"
        )
        self.assertEqual("595520", mod.steam_app_id)
        self.assertEqual("FINAL FANTASY XII THE ZODIAC AGE", mod.install_dir)
        self.assertEqual("x64/FFXII_TZA.exe", mod.executable)
        self.assertEqual("x64", mod.source.destination)
        self.assertEqual(
            "Final Fantasy XII The Zodiac Age/Final-Fantasy-XII-The-Zodiac-Age-v1.0.0.zip",
            mod.source.path,
        )
        self.assertEqual(
            ("x64/dinput8.dll", "x64/FFXII_TZA_ARFix.asi", "x64/FFXII_TZA_ARFix.ini"),
            mod.files,
        )
        self.assertEqual("x64/FFXII_TZA_ARFix.ini", mod.ini_file)
        self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(
                {"dinput8.dll", "FFXII_TZA_ARFix.asi", "FFXII_TZA_ARFix.ini",
                 "README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                set(archive.namelist()),
            )
            readme = (root / mod_readme_path(mod)).read_bytes()
            self.assertEqual(readme, archive.read("README.md"))
            for image in (b"ff12.jpg", b"ff12demo.jpg", b"ff12demo2.jpg"):
                self.assertIn(image, readme)
            self.assertIn(b"at 4:3", readme)
            self.assertIn(b"SteamOS / Winlator", readme)
            self.assertIn(b"FOV fixes are enabled by default", readme)
            config = configparser.ConfigParser()
            config.read_string(archive.read("FFXII_TZA_ARFix.ini").decode("utf-8-sig"))
            self.assertEqual(1280, config.getint("Window", "Width"))
            self.assertEqual(800, config.getint("Window", "Height"))
            self.assertTrue(config.getboolean("Resolution", "Enabled"))
            self.assertTrue(config.getboolean("FOV", "Enabled"))

    def test_dw7xl_package_and_install_paths(self) -> None:
        import configparser

        mod = next(
            mod for mod in load_mod_catalog()
            if mod.mod_id == "dynasty-warriors-7-xtreme-legends-definitive-edition"
        )
        self.assertEqual("968790", mod.steam_app_id)
        self.assertEqual("Dynasty Warriors 7 DX", mod.install_dir)
        self.assertEqual("SM6EN.exe", mod.executable)
        self.assertEqual(("SM6JP.exe", "SM6TC.exe"), mod.executable_alternatives)
        self.assertFalse(mod.source.destination)
        self.assertEqual(("dinput8.dll", "DW7XL_ARFix.asi", "DW7XL_ARFix.ini"), mod.files)
        self.assertEqual("DW7XL_ARFix.ini", mod.ini_file)
        self.assertEqual(("stretch",), mod.display_tags)
        self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(
                {"dinput8.dll", "DW7XL_ARFix.asi", "DW7XL_ARFix.ini",
                 "README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                set(archive.namelist()),
            )
            readme = archive.read("README.md")
            self.assertEqual((root / mod_readme_path(mod)).read_bytes(), readme)
            self.assertIn(b'1280x720', readme)
            self.assertIn(b'SteamOS display scaling to "Stretch"', readme)
            self.assertIn(b'WINEDLLOVERRIDES="dinput8=n,b" %command%', readme)
            config = configparser.ConfigParser()
            config.read_string(archive.read("DW7XL_ARFix.ini").decode("utf-8-sig"))
            self.assertEqual(1280, config.getint("Resolution", "Width"))
            self.assertEqual(800, config.getint("Resolution", "Height"))

    def test_dw8xl_package_and_install_paths(self) -> None:
        import configparser

        mod = next(
            mod for mod in load_mod_catalog()
            if mod.mod_id == "dynasty-warriors-8-xtreme-legends-complete-edition"
        )
        self.assertEqual("278080", mod.steam_app_id)
        self.assertEqual("Dynasty Warriors 8", mod.install_dir)
        self.assertEqual("Launch.exe", mod.executable)
        self.assertFalse(mod.source.destination)
        self.assertEqual(("dinput8.dll", "DW8XL_ARFix.asi", "DW8XL_ARFix.ini"), mod.files)
        self.assertEqual("DW8XL_ARFix.ini", mod.ini_file)
        self.assertEqual(("stretch",), mod.display_tags)
        self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(
                {"dinput8.dll", "DW8XL_ARFix.asi", "DW8XL_ARFix.ini",
                 "README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                set(archive.namelist()),
            )
            readme = archive.read("README.md")
            self.assertEqual((root / mod_readme_path(mod)).read_bytes(), readme)
            self.assertIn(b"1280x720", readme)
            self.assertIn(b'SteamOS display scaling to "Stretch"', readme)
            self.assertIn(b'WINEDLLOVERRIDES="dinput8=n,b" %command%', readme)
            config = configparser.ConfigParser()
            config.read_string(archive.read("DW8XL_ARFix.ini").decode("utf-8-sig"))
            self.assertEqual(1280, config.getint("Resolution", "Width"))
            self.assertEqual(800, config.getint("Resolution", "Height"))
            self.assertAlmostEqual(1.1, config.getfloat("Camera", "FovMultiplier"))
            self.assertIn(b"FovMultiplier", readme)

    def test_bloodstained_package_and_install_paths(self) -> None:
        import configparser

        from scanner.mod_installer import existing_mod_logs, read_mod_log

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "bloodstained-ritual-of-the-night")
        destination = "BloodstainedRotN/Binaries/Win64"
        self.assertEqual("692850", mod.steam_app_id)
        self.assertEqual("Bloodstained Ritual of the Night", mod.install_dir)
        self.assertEqual(f"{destination}/BloodstainedRotN-Win64-Shipping.exe", mod.executable)
        self.assertEqual(("BloodstainedRotN.exe",), mod.executable_alternatives)
        self.assertEqual(destination, mod.source.destination)
        self.assertEqual(
            tuple(f"{destination}/{name}" for name in
                  ("winhttp.dll", "BloodstainedARFix.asi", "BloodstainedARFix.ini")),
            mod.files,
        )
        self.assertEqual(f"{destination}/BloodstainedARFix.ini", mod.ini_file)
        self.assertEqual(
            ("winhttp=n,b", "msvcp140,vcruntime140,vcruntime140_1=b"),
            mod.wine_dll_overrides,
        )
        self.assertEqual(
            "Bloodstained Ritual of the Night/Bloodstained-Ritual-of-the-Night-v1.0.0.zip",
            mod.source.path,
        )
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertEqual(
                {"winhttp.dll", "BloodstainedARFix.asi", "BloodstainedARFix.ini",
                 "README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                set(archive.namelist()),
            )
            self.assertEqual(
                (root / mod_readme_path(mod)).read_bytes(), archive.read("README.md")
            )
            self.assertIn(
                b'WINEDLLOVERRIDES="' + ";".join(mod.wine_dll_overrides).encode("ascii") + b'" %command%',
                archive.read("README.md"),
            )
            config = configparser.ConfigParser()
            config.read_string(archive.read("BloodstainedARFix.ini").decode("utf-8"))
            self.assertEqual(1280, config.getint("Resolution", "Width"))
            self.assertEqual(800, config.getint("Resolution", "Height"))
            self.assertEqual(100, config.getint("Camera", "FOVPercent"))
        with tempfile.TemporaryDirectory() as directory:
            name = f"{destination}/BloodstainedARFix.log"
            log = Path(directory) / name
            log.parent.mkdir(parents=True)
            log.write_text("Bloodstained log", encoding="utf-8")
            self.assertEqual([name], existing_mod_logs(mod.files, directory))
            self.assertEqual(("Bloodstained log", False), read_mod_log(mod, directory, name))

    def test_sonic_x_shadow_generations_package(self) -> None:
        import configparser

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "sonic-x-shadow-generations")
        self.assertEqual("2513280", mod.steam_app_id)
        self.assertEqual("SONIC_X_SHADOW_GENERATIONS", mod.install_dir)
        self.assertEqual("SONIC_X_SHADOW_GENERATIONS.exe", mod.executable)
        self.assertEqual(("SONIC_GENERATIONS.exe",), mod.executable_alternatives)
        self.assertEqual(
            ("winmm.dll", "SonicShadow_ARFix.asi", "GenerationsARFix.asi", "SonicShadow_ARFix.ini"),
            mod.files,
        )
        self.assertEqual("SonicShadow_ARFix.ini", mod.ini_file)
        self.assertEqual(("winmm=n,b",), mod.wine_dll_overrides)
        from scanner.mod_installer import existing_mod_logs, read_mod_log

        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "SonicShadow_ARFix.log"
            log.write_text("shared Sonic and Shadow log", encoding="utf-8")
            self.assertEqual(["SonicShadow_ARFix.log"], existing_mod_logs(mod.files, directory))
            self.assertEqual(
                ("shared Sonic and Shadow log", False),
                read_mod_log(mod, directory, "SonicShadow_ARFix.log"),
            )
        self.assertEqual(
            "SONIC X SHADOW GENERATIONS/SONIC-X-SHADOW-GENERATIONS-v1.0.0.zip",
            mod.source.path,
        )
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertEqual(
                set(mod.files) | {"README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                set(archive.namelist()),
            )
            self.assertEqual(
                (root / mod_readme_path(mod)).read_bytes(), archive.read("README.md")
            )
            config = configparser.ConfigParser()
            config.read_string(archive.read(mod.ini_file).decode("utf-8"))
            for section in ("Shadow", "Generations"):
                self.assertTrue(config.getboolean(section, "CameraCorrection"))
                self.assertTrue(config.getboolean(section, "RemoveLetterbox"))
            self.assertFalse(config.getboolean("Diagnostics", "VerboseDiagnostics"))

    def test_visions_of_mana_install_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "visions-of-mana")
        destination = "VisionsofMana/Binaries/Win64"
        self.assertEqual("VisionsofMana", mod.install_dir)
        self.assertEqual(f"{destination}/VisionsofMana-Win64-Shipping.exe", mod.executable)
        self.assertEqual(destination, mod.source.destination)
        self.assertEqual(
            {f"{destination}/{name}" for name in ("dxgi.dll", "VoM_ARFix.asi", "VoM_ARFix.ini")},
            set(mod.files),
        )
        self.assertEqual(f"{destination}/VoM_ARFix.ini", mod.ini_file)

    def test_palworld_catalog_paths(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}
        palworld = catalog["palworld"]
        self.assertEqual("Palworld", palworld.install_dir)
        self.assertEqual(
            "Pal/Binaries/Win64/Palworld-Win64-Shipping.exe",
            palworld.executable,
        )
        self.assertEqual(
            {
                "Pal/Binaries/Win64/winmm.dll",
                "Pal/Binaries/Win64/Palworld_ARFix.asi",
                "Pal/Binaries/Win64/Palworld_ARFix.ini",
            },
            set(palworld.files),
        )

    def test_absolum_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "absolumfix")
        self.assertEqual("1904480", mod.steam_app_id)
        self.assertEqual("Absolum", mod.install_dir)
        self.assertEqual("x64.steam/Absolum.exe", mod.executable)
        self.assertEqual("x64.steam", mod.source.destination)
        self.assertEqual(
            ("x64.steam/winhttp.dll", "x64.steam/Absolum_ARFix.asi", "x64.steam/Absolum_ARFix.ini"),
            mod.files,
        )
        self.assertEqual("x64.steam/Absolum_ARFix.ini", mod.ini_file)
        self.assertEqual(("winhttp=n,b",), mod.wine_dll_overrides)
        self.assertEqual("archive", mod.source.kind)
        self.assertEqual("Reimpl-Absolum/Absolum-v0.1.0.zip", mod.source.path)
        self.assertEqual(("TLin-Y",), tuple(credit.name for credit in mod.credits))

    def test_dave_the_diver_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "dave-the-diver")
        self.assertEqual("1868140", mod.steam_app_id)
        self.assertEqual("Dave the Diver", mod.install_dir)
        self.assertEqual("DaveTheDiver.exe", mod.executable)
        self.assertEqual(("winmm.dll", "Dave_ARFix.asi", "Dave_ARFix.ini"), mod.files)
        self.assertEqual("Dave_ARFix.ini", mod.ini_file)
        self.assertEqual(("winmm=n,b",), mod.wine_dll_overrides)
        self.assertEqual("archive", mod.source.kind)
        self.assertEqual("DAVE THE DIVER/DAVE-THE-DIVER-v1.1.0.zip", mod.source.path)

    def test_pentiment_release_paths_and_defaults(self) -> None:
        import configparser

        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "pentiment")
        self.assertEqual("1205520", mod.steam_app_id)
        self.assertEqual("Pentiment", mod.install_dir)
        self.assertEqual("Pentiment.exe", mod.executable)
        self.assertEqual(("winhttp.dll", "Pentiment_ARFix.asi", "Pentiment_ARFix.ini"), mod.files)
        self.assertEqual("Pentiment_ARFix.ini", mod.ini_file)
        self.assertEqual(("winhttp=n,b",), mod.wine_dll_overrides)
        self.assertEqual("archive", mod.source.kind)
        self.assertEqual("Pentiment/Pentiment-v0.2.0.zip", mod.source.path)
        self.assertEqual(1, len(mod.ini_resolution))
        pair = mod.ini_resolution[0]
        self.assertEqual(("Resolution", "Width", "Height", "pixels"),
                         (pair.section, pair.width, pair.height, pair.kind))
        root = Path(__file__).resolve().parents[2]
        with ZipFile(root / mod.source.path) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(
                set(mod.files) | {"README.md", "LICENSE-Ultimate-ASI-Loader.txt"},
                set(archive.namelist()),
            )
            self.assertEqual((root / mod_readme_path(mod)).read_bytes(), archive.read("README.md"))
            config = configparser.ConfigParser()
            config.read_string(archive.read(mod.ini_file).decode("utf-8-sig"))
            self.assertTrue(config.getboolean("AspectRatio", "Enabled"))
            self.assertTrue(config.getboolean("Resolution", "Borderless"))
            self.assertEqual(1280, config.getint("Resolution", "Width"))
            self.assertEqual(800, config.getint("Resolution", "Height"))

    def test_sanabi_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "sanabi")
        self.assertEqual("1562700", mod.steam_app_id)
        self.assertEqual("SANABI", mod.install_dir)
        self.assertEqual("SNB.exe", mod.executable)
        self.assertEqual(("winmm.dll", "SNB_ARFix.asi", "SNB_ARFix.ini"), mod.files)
        self.assertEqual("SNB_ARFix.ini", mod.ini_file)
        self.assertEqual(("winmm=n,b",), mod.wine_dll_overrides)
        self.assertEqual("archive", mod.source.kind)
        self.assertEqual("SANABI/SANABI-v1.1.0.zip", mod.source.path)

    def test_undertale_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "undertale")
        self.assertEqual("Undertale", mod.install_dir)
        self.assertEqual("UNDERTALE.exe", mod.executable)
        self.assertEqual("archive", mod.source.kind)
        self.assertEqual(
            "external/Twig/Undertale/Undertale-Ultrawide-Fix.zip", mod.source.path
        )
        self.assertEqual(("data.win",), mod.files)
        self.assertEqual("data.win.bak", mod.backup_file)
        self.assertEqual("external/Twig/Undertale/README.md", mod_readme_path(mod))
        self.assertEqual(("Twig",), tuple(credit.name for credit in mod.credits))

    def test_elden_ring_offline_launcher_and_archive_paths(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}
        mod = catalog["elden-ring-ultrawide-fix"]
        self.assertTrue(mod.offline_only)
        self.assertTrue(catalog["dark-souls-iii"].offline_only)
        self.assertIsNone(catalog["dark-souls-iii"].launcher_replacement)
        self.assertEqual("ELDEN RING", mod.install_dir)
        self.assertEqual("Game/eldenring.exe", mod.executable)
        self.assertEqual((
            "Game/modengine2_launcher.exe",
            "Game/start_protected_game.exe",
            "Game/start_game_in_offline_mode.exe",
        ), mod.executable_alternatives)
        self.assertEqual("Game", mod.source.destination)
        self.assertEqual("Game/start_game_in_offline_mode.exe", mod.launcher_replacement.source)
        self.assertEqual("Game/start_protected_game.exe", mod.launcher_replacement.target)
        self.assertEqual("Game/start_protected_game-old.exe", mod.launcher_replacement.backup)
        self.assertEqual(("TechieW",), tuple(credit.name for credit in mod.credits))
        self.assertEqual("external/TechieW/ELDEN RING/README.md", mod_readme_path(mod))
        root = Path(__file__).resolve().parents[2]
        self.assertTrue((root / mod_readme_path(mod)).is_file())
        with ZipFile(root / mod.source.path) as archive:
            self.assertEqual(
                {f"Game/{info.filename}" for info in archive.infolist() if not info.is_dir()},
                set(mod.files),
            )

    def test_rejects_invalid_offline_launcher_metadata(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        entry = next(mod for mod in document["mods"] if mod["id"] == "elden-ring-ultrawide-fix")
        document["mods"] = [entry]
        original = dict(entry["launcher_replacement"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            for change in (
                {"source": "../outside.exe"},
                {"source": "Game/missing.exe"},
                {"target": "Game/eldenring.exe"},
                {"target": original["source"]},
                {"backup": "Game/dinput8.dll"},
                {"backup": "Game/modengine2_launcher.exe"},
                {"backup": original["target"]},
                {"backup": "other/backup.exe"},
                {"source": 123},
                {"extra": "value"},
            ):
                with self.subTest(change=change):
                    entry["launcher_replacement"] = {**original, **change}
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_mod_catalog(path)
            entry["launcher_replacement"] = original
            for value in (False, "true", 1):
                with self.subTest(offline_only=value):
                    entry["offline_only"] = value
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_mod_catalog(path)

    def test_beast_of_reincarnation_install_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "beastofreincarnationfix")
        destination = "BeastOfReincarnation/Binaries/Win64"
        self.assertEqual("BeastOfReincarnation", mod.install_dir)
        self.assertEqual(destination, mod.source.destination)
        self.assertEqual(
            {f"{destination}/{name}" for name in (
                "BeastOfReincarnationFix.asi", "BeastOfReincarnationFix.ini", "dsound.dll"
            )},
            set(mod.files),
        )
        self.assertEqual(f"{destination}/BeastOfReincarnationFix.ini", mod.ini_file)

    def test_guacamelee_2_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "guacamelee-2")
        self.assertEqual("534550", mod.steam_app_id)
        self.assertEqual("Guacamelee2", mod.install_dir)
        self.assertEqual("Guac2_x64.exe", mod.executable)
        self.assertEqual(("Guac2_64.exe",), mod.executable_alternatives)
        self.assertEqual(
            ("dinput8.dll", "Guac2_ARFix.asi", "Guac2_ARFix.ini"),
            mod.files,
        )
        self.assertEqual("Guac2_ARFix.ini", mod.ini_file)
        self.assertEqual("Guacamelee 2/Guacamelee-2-v2.0.1.zip", mod.source.path)

    def test_guacamelee_stce_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "guacamelee-stce")
        self.assertEqual("275390", mod.steam_app_id)
        self.assertEqual("DrinkBox_Game4", mod.install_dir)
        self.assertEqual("GuacSTCE.exe", mod.executable)
        self.assertEqual(("Game.exe",), mod.executable_alternatives)
        self.assertEqual(
            ("dinput8.dll", "Guac1_ARFix.asi", "Guac1_ARFix.ini"),
            mod.files,
        )
        self.assertEqual("Guac1_ARFix.ini", mod.ini_file)
        self.assertEqual(
            "Guacamelee Super Turbo Championship Edition/Guacamelee-STCE-v2.0.0.zip",
            mod.source.path,
        )

    def test_rayman_legends_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "rayman-legends")
        self.assertEqual("242550", mod.steam_app_id)
        self.assertEqual("Rayman Legends", mod.install_dir)
        self.assertEqual("Rayman Legends.exe", mod.executable)
        self.assertEqual(
            ("dinput8.dll", "RaymanLegends_ARFix.asi", "RaymanLegends_ARFix.ini"),
            mod.files,
        )
        self.assertEqual("RaymanLegends_ARFix.ini", mod.ini_file)

    def test_ys_viii_catalog_paths(self) -> None:
        mod = next(
            mod for mod in load_mod_catalog()
            if mod.mod_id == "ys-viii-lacrimosa-of-dana"
        )
        self.assertEqual("579180", mod.steam_app_id)
        self.assertEqual("Ys VIII Lacrimosa of Dana", mod.install_dir)
        self.assertEqual("ys8.exe", mod.executable)
        self.assertEqual(
            ("dinput8.dll", "Ys8_AspectPatch.asi", "Ys8_AspectPatch.ini"),
            mod.files,
        )
        self.assertEqual("Ys8_AspectPatch.ini", mod.ini_file)

    def test_verified_external_mod_install_paths(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}
        for mod_id, app_id, install_dir, executable, asi_path in (
            ("wofffix", "552700", "WOFF", "WOFF.exe", "WOFFFix.asi"),
            ("doa6fix", "838380", "Dead or Alive 6", "doa6.exe", "DOA6Fix.asi"),
            (
                "so4fix", "609150", "STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster",
                "StarOceanTheLastHope.exe", "SO4Fix.asi",
            ),
            (
                "mdaraincodeplusfix", "2903950", "Master Detective Archives RAIN CODE",
                "RainCodePlus.exe", "RainCodePlus/Binaries/Win64/MDARainCodePlusFix.asi",
            ),
        ):
            with self.subTest(mod_id=mod_id):
                mod = catalog[mod_id]
                self.assertEqual(app_id, mod.steam_app_id)
                self.assertEqual(install_dir, mod.install_dir)
                self.assertEqual(executable, mod.executable)
                self.assertIn(asi_path, mod.files)

    def test_supports_folder_and_direct_file_sources(self) -> None:
        catalog = {mod.mod_id: mod for mod in load_mod_catalog()}

        ninja_gaiden = catalog["ninja-gaiden-sigma-2"]
        self.assertEqual("directory", ninja_gaiden.source.kind)
        self.assertTrue(
            (Path(__file__).resolve().parents[2] / ninja_gaiden.source.path).is_dir()
        )

        ori = catalog["ori-and-the-blind-forest-definitive-edition"]
        self.assertIsNone(ori.ini_file)
        self.assertEqual("files", ori.source.kind)
        self.assertEqual(1, len(ori.source.files))

        trials = catalog["trials-of-mana"]
        self.assertEqual("files", trials.source.kind)
        self.assertEqual(3, len(trials.source.files))

        # Every loose file has a checksum, so nothing is installed unverified.
        for mod in (ninja_gaiden, ori, trials):
            self.assertEqual(set(mod.files), {path for path, _ in mod.source.file_hashes}, mod.mod_id)

    def test_rejects_invalid_file_hashes(self) -> None:
        catalog_path = Path(__file__).resolve().parents[1] / "catalog.json"
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
        index = next(i for i, mod in enumerate(document["mods"]) if mod["id"] == "trials-of-mana")
        archive_index = next(i for i, mod in enumerate(document["mods"]) if mod["source"]["kind"] == "archive")
        bad_values = [
            (index, {"Trials of Mana/Binaries/Win64/dxgi.dll": "not-a-hash"}),
            (index, {"unlisted.dll": "0" * 64}),
            (index, ["0" * 64]),
            (archive_index, {"any.dll": "0" * 64}),
        ]
        for mod_index, value in bad_values:
            broken = json.loads(json.dumps(document))
            broken["mods"][mod_index]["source"]["file_hashes"] = value
            with self.subTest(value=value), tempfile.TemporaryDirectory() as temporary_directory:
                path = Path(temporary_directory) / "catalog.json"
                path.write_text(json.dumps(broken), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "invalid source metadata"):
                    load_mod_catalog(path)

    def test_rejects_unsafe_repository_source_path(self) -> None:
        catalog_path = Path(__file__).resolve().parents[1] / "catalog.json"
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
        document["mods"][0]["source"]["path"] = (
            "../outside.zip"
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "unsafe relative path"):
                load_mod_catalog(path)

    def test_rejects_unsafe_mod_file_path(self) -> None:
        catalog_path = Path(__file__).resolve().parents[1] / "catalog.json"
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
        document["mods"][0]["files"][0] = "../outside.dll"

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "catalog.json"
            path.write_text(json.dumps(document), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "unsafe relative path"):
                load_mod_catalog(path)

    def test_executable_alternatives_are_validated(self) -> None:
        catalog_path = Path(__file__).resolve().parents[1] / "catalog.json"
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
        entry = document["mods"][0]
        document["mods"] = [entry]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            for value in ("other.exe", [""], [123], ["../outside.exe"], [entry["executable"]]):
                with self.subTest(value=value):
                    entry["executable_alternatives"] = value
                    path.write_text(json.dumps(document), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_mod_catalog(path)

    def test_expedition_33_catalog_paths(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "clairobscurfix")
        self.assertEqual("Expedition 33", mod.install_dir)
        self.assertEqual("Sandfall/Binaries/Win64/SandFall-Win64-Shipping.exe", mod.executable)
        self.assertEqual(("Expedition33_Steam.exe",), mod.executable_alternatives)
        self.assertEqual("Sandfall/Binaries/Win64", mod.source.destination)

    def test_archive_checksums_match_the_zips_in_the_repository(self) -> None:
        # Fails after a zip changes until scripts/update_catalog_hashes.py is run.
        scripts = str(Path(__file__).resolve().parents[1] / "scripts")
        sys.path.insert(0, scripts)
        try:
            from update_catalog_hashes import update
        finally:
            sys.path.remove(scripts)
        self.assertFalse(update(check=True), "run scripts/update_catalog_hashes.py")
        for mod in load_mod_catalog():
            if mod.source.kind == "archive":
                self.assertEqual(64, len(mod.source.sha256), mod.mod_id)



class ManualModTests(unittest.TestCase):
    def test_manual_mods_are_listed_but_never_installed(self) -> None:
        from scanner.mod_installer import apply_mod_files, remove_mod_files

        mods = {mod.mod_id: mod for mod in load_mod_catalog()}
        wild_hearts, dbxv2 = mods["wildheartsfix"], mods["dbxv2fix"]
        self.assertTrue(wild_hearts.manual and dbxv2.manual)
        self.assertEqual("", wild_hearts.executable)
        self.assertFalse(mods["dark-souls-iii"].manual)

        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / "WILD HEARTS"
            game.mkdir()
            app = InstalledApp("1938010", "WILD HEARTS", "WILD HEARTS", directory, str(game))
            report = match_supported_games(SteamScanReport(1, 1, [app], []), (wild_hearts,))
            self.assertEqual(1, len(report.supported_games))
            game_data = report.supported_games[0].to_dict()
            self.assertTrue(game_data["manual"])
            self.assertEqual([], game_data["display_tags"])

            with self.assertRaisesRegex(ValueError, "manually"):
                apply_mod_files(wild_hearts, str(game))
            with self.assertRaisesRegex(ValueError, "manually"):
                remove_mod_files(wild_hearts, str(game))
            self.assertEqual([], list(game.iterdir()))

    def test_executable_is_required_unless_manual(self) -> None:
        document = json.loads(
            (Path(__file__).resolve().parents[1] / "catalog.json").read_text(encoding="utf-8")
        )
        entry = next(mod for mod in document["mods"] if mod["id"] == "wildheartsfix")
        document["mods"] = [entry]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.json"
            entry["manual"] = False
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)
            entry["manual"] = "yes"
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaises(ValueError):
                load_mod_catalog(path)


class SupportedGameScannerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.game_path = Path(self.temporary_directory.name) / "game"
        self.game_path.mkdir()
        self.mod = next(mod for mod in load_mod_catalog() if not mod.file_backups)
        self.app = InstalledApp(
            app_id=self.mod.steam_app_id,
            name="Test Game",
            install_dir=self.mod.install_dir,
            library_path=self.temporary_directory.name,
            game_path=str(self.game_path),
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _scan(self) -> SupportedGame:
        return match_supported_games(
            SteamScanReport(1, 1, [self.app], []),
            (self.mod,),
        ).supported_games[0]

    def _touch(self, relative_path: str) -> None:
        path = self.game_path / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()

    def test_matches_game_and_detects_missing_mod_files(self) -> None:
        self._touch(self.mod.executable)

        report = match_supported_games(
            SteamScanReport(1, 1, [self.app], []),
            (self.mod,),
        )
        game = report.supported_games[0]

        self.assertTrue(game.executable_found)
        self.assertEqual("not_detected", game.file_status)
        self.assertEqual(list(self.mod.files), game.files_missing)
        self.assertEqual(list(self.mod.files), game.to_dict()["mod_files"])
        self.assertEqual(self.mod.ini_file, game.to_dict()["ini_file"])
        self.assertEqual(
            list(self.mod.wine_dll_overrides),
            game.to_dict()["wine_dll_overrides"],
        )
        self.assertEqual(1, len(report.to_dict()["supported_games"]))

    def test_reports_every_mod_for_the_same_game(self) -> None:
        other = replace(self.mod, mod_id="second-mod", name="Second mod")
        report = match_supported_games(
            SteamScanReport(1, 1, [self.app], []),
            (self.mod, other),
        )

        self.assertEqual(
            [self.mod.mod_id, "second-mod"],
            [game.mod_id for game in report.supported_games],
        )
        self.assertEqual({self.mod.mod_id, "second-mod"}, set(report.mods))

    def test_reports_partial_and_complete_mod_file_presence(self) -> None:
        self._touch(self.mod.executable)
        self._touch(self.mod.files[0])

        partial = self._scan()
        self.assertEqual("partial", partial.file_status)

        for relative_path in self.mod.files:
            self._touch(relative_path)
        complete = self._scan()
        self.assertEqual("complete", complete.file_status)
        self.assertEqual(str(self.game_path / self.mod.ini_file), complete.ini_path)

    def test_restored_backup_target_is_not_reported_as_an_installed_mod_file(self) -> None:
        mod = next(
            item for item in load_mod_catalog()
            if item.mod_id == "ai-the-somnium-files"
        )
        backup = mod.file_backups[0]
        self._touch(backup.target)
        app = replace(self.app, app_id=mod.steam_app_id, install_dir=mod.install_dir)

        def scan() -> SupportedGame:
            return match_supported_games(
                SteamScanReport(1, 1, [app], []),
                (mod,),
            ).supported_games[0]

        restored = scan()
        self.assertEqual("not_detected", restored.file_status)
        self.assertEqual([], restored.files_present)
        self.assertNotIn(backup.target, restored.mod_files)

        self._touch(backup.backup)
        applied = scan()
        self.assertEqual("partial", applied.file_status)
        self.assertIn(backup.target, applied.files_present)
        self.assertIn(backup.target, applied.mod_files)

    def test_ignores_app_with_mismatched_install_directory(self) -> None:
        mismatched_app = InstalledApp(
            app_id=self.app.app_id,
            name=self.app.name,
            install_dir="different-directory",
            library_path=self.app.library_path,
            game_path=self.app.game_path,
        )
        report = match_supported_games(
            SteamScanReport(1, 1, [mismatched_app], []),
            (self.mod,),
        )

        self.assertEqual([], report.supported_games)
        self.assertEqual(1, len(report.warnings))
        self.assertIn("does not match catalog directory", report.warnings[0])

    def test_mismatched_steam_directory_uses_unique_executable(self) -> None:
        self._touch(self.mod.executable)
        app = replace(self.app, install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod,))
        self.assertEqual([self.mod.mod_id], [game.mod_id for game in report.supported_games])
        self.assertTrue(report.supported_games[0].executable_found)
        self.assertTrue(report.supported_games[0].matched_by_executable)
        self.assertTrue(report.supported_games[0].to_dict()["matched_by_executable"])
        self.assertEqual(1, len(report.warnings))

    def test_unknown_steam_app_uses_unique_executable(self) -> None:
        self._touch(self.mod.executable)
        app = replace(self.app, app_id="999999", install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod,))
        self.assertEqual([self.mod.mod_id], [game.mod_id for game in report.supported_games])
        self.assertEqual("999999", report.supported_games[0].steam_app_id)

    def test_known_steam_id_does_not_fallback_to_another_game(self) -> None:
        other = replace(self.mod, mod_id="other", steam_app_id="123456", executable="other.exe")
        self._touch(other.executable)
        app = replace(self.app, install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod, other))
        self.assertEqual([], report.supported_games)

    def test_executable_alternatives_match_both_steam_and_shortcuts(self) -> None:
        mod = replace(self.mod, executable="game-primary.exe", executable_alternatives=("other.exe",))
        self._touch("other.exe")
        for is_shortcut in (False, True):
            with self.subTest(is_shortcut=is_shortcut):
                app = replace(
                    self.app, app_id="999999", install_dir="different",
                    shortcut_executable=str(self.game_path / "other.exe"),
                )
                scan = SteamScanReport(1, 1, [] if is_shortcut else [app], [], [app] if is_shortcut else [])
                (game,) = match_supported_games(scan, (mod,)).supported_games
                self.assertTrue(game.executable_found)
                self.assertEqual(str(self.game_path / "other.exe"), game.executable_path)

    def test_absolum_steam_and_executable_fallback(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "absolumfix")
        self._touch("x64.steam/Absolum.exe")
        for install_dir, matched_by_executable in (("Absolum", False), ("Other", True)):
            with self.subTest(install_dir=install_dir):
                app = replace(
                    self.app,
                    app_id=mod.steam_app_id,
                    install_dir=install_dir,
                )
                report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
                (game,) = report.supported_games
                self.assertEqual("1904480", game.steam_app_id)
                self.assertEqual(str(self.game_path / mod.executable), game.executable_path)
                self.assertTrue(game.executable_found)
                self.assertEqual(matched_by_executable, game.matched_by_executable)

    def test_guacamelee_2_executables_match_steam_and_shortcuts(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "guacamelee-2")
        for executable in ("Guac2_64.exe", "Guac2_x64.exe"):
            self._touch(executable)
            for is_shortcut in (False, True):
                with self.subTest(executable=executable, is_shortcut=is_shortcut):
                    app = InstalledApp(
                        app_id=mod.steam_app_id,
                        name="Guacamelee! 2",
                        install_dir="Guacamelee2",
                        library_path=self.app.library_path,
                        game_path=self.app.game_path,
                        shortcut_executable=str(self.game_path / executable),
                    )
                    scan = SteamScanReport(
                        1, 1, [] if is_shortcut else [app], [],
                        [app] if is_shortcut else [],
                    )
                    report = match_supported_games(scan, (mod,))
                    (game,) = report.supported_games
                    self.assertEqual("534550", game.steam_app_id)
                    self.assertEqual(str(self.game_path / executable), game.executable_path)
                    self.assertTrue(game.executable_found)
                    self.assertEqual(is_shortcut, game.matched_by_executable)
            (self.game_path / executable).unlink()

    def test_guacamelee_stce_executable_names_match_steam_and_other_folders(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "guacamelee-stce")
        for executable, install_dir, matched_by_executable in (
            ("Game.exe", "DrinkBox_Game4", False),
            ("GuacSTCE.exe", "Guacamelee.STCE", True),
        ):
            with self.subTest(executable=executable, install_dir=install_dir):
                self._touch(executable)
                app = InstalledApp(
                    app_id=mod.steam_app_id,
                    name="Guacamelee! Super Turbo Championship Edition",
                    install_dir=install_dir,
                    library_path=self.app.library_path,
                    game_path=self.app.game_path,
                )
                report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))

                (game,) = report.supported_games
                self.assertEqual("275390", game.steam_app_id)
                self.assertEqual(str(self.game_path / executable), game.executable_path)
                self.assertEqual(matched_by_executable, game.matched_by_executable)

    def test_rayman_executable_matches_steam_and_custom_install_folders(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "rayman-legends")
        self._touch("Rayman Legends.exe")
        for install_dir, matched_by_executable in (
            ("Rayman Legends", False),
            ("Rayman.Legends", True),
        ):
            with self.subTest(install_dir=install_dir):
                app = InstalledApp(
                    app_id=mod.steam_app_id,
                    name="Rayman Legends",
                    install_dir=install_dir,
                    library_path=self.app.library_path,
                    game_path=self.app.game_path,
                )
                report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))

                (game,) = report.supported_games
                self.assertEqual("242550", game.steam_app_id)
                self.assertEqual(
                    str(self.game_path / "Rayman Legends.exe"),
                    game.executable_path,
                )
                self.assertEqual(matched_by_executable, game.matched_by_executable)

    def test_ambiguous_executable_names_require_selection(self) -> None:
        mod = replace(self.mod, executable="dir/game.exe")
        other = replace(mod, mod_id="other", steam_app_id="123456", executable="other/game.exe")
        self._touch(mod.executable)
        for is_shortcut in (False, True):
            with self.subTest(is_shortcut=is_shortcut):
                app = replace(
                    self.app, app_id="999999", install_dir="different",
                    shortcut_executable=str(self.game_path / mod.executable),
                )
                scan = SteamScanReport(1, 1, [] if is_shortcut else [app], [], [app] if is_shortcut else [])
                report = match_supported_games(scan, (mod, other))
                (game,) = report.supported_games
                self.assertEqual(mod.mod_id, game.mod_id)
                self.assertIsNotNone(game.ambiguity_group)
                self.assertEqual(game.ambiguity_group, game.to_dict()["ambiguity_group"])

    def test_ambiguous_executable_for_mismatched_known_steam_id_requires_selection(self) -> None:
        mod = replace(self.mod, executable="game.exe")
        other = replace(mod, mod_id="other", steam_app_id="123456")
        self._touch(mod.executable)
        app = replace(self.app, install_dir="different-directory")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod, other))
        (game,) = report.supported_games
        self.assertEqual(mod.mod_id, game.mod_id)
        self.assertIsNotNone(game.ambiguity_group)

    def test_known_steam_id_keeps_all_mods_when_executable_is_shared(self) -> None:
        mod = replace(self.mod, executable="game.exe")
        other = replace(mod, mod_id="other")
        self._touch(mod.executable)
        report = match_supported_games(SteamScanReport(1, 1, [self.app], []), (mod, other))
        self.assertEqual([mod.mod_id, "other"], [game.mod_id for game in report.supported_games])
        self.assertTrue(all(not game.matched_by_executable for game in report.supported_games))

    def test_file_probes_are_cached_for_one_scan_without_missing_alternatives(self) -> None:
        mod = replace(self.mod, executable="missing.exe", executable_alternatives=("found.exe",))
        other = replace(mod, mod_id="other", steam_app_id="123456", executable="missing.exe",
                        executable_alternatives=("other.exe",))
        self._touch("found.exe")
        shortcut = replace(self.app, app_id="999999", install_dir="different")
        original_is_file = Path.is_file
        probes: dict[Path, int] = {}

        def count_probe(path: Path) -> bool:
            probes[path] = probes.get(path, 0) + 1
            return original_is_file(path)

        with patch.object(Path, "is_file", count_probe):
            report = match_supported_games(
                SteamScanReport(1, 1, [shortcut], []), (mod, other)
            )
        self.assertEqual([mod.mod_id], [game.mod_id for game in report.supported_games])
        self.assertEqual(str(self.game_path / "found.exe"), report.supported_games[0].executable_path)
        self.assertTrue(all(count == 1 for count in probes.values()), probes)

    def test_fallback_does_not_probe_paths_with_absent_root_components(self) -> None:
        mod = replace(self.mod, executable="missing/path/game.exe")
        app = replace(self.app, app_id="999999", install_dir="different")
        with patch.object(Path, "is_file", side_effect=AssertionError("unexpected file probe")):
            report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.assertEqual([], report.supported_games)

    def test_fallback_reports_unreadable_game_directory(self) -> None:
        app = replace(self.app, app_id="999999", install_dir="different")
        with patch("scanner.supported_game_scanner.os.scandir", side_effect=PermissionError("denied")):
            report = match_supported_games(SteamScanReport(1, 1, [app], []), (self.mod,))
        self.assertEqual([], report.supported_games)
        self.assertTrue(any("denied" in warning for warning in report.warnings))

    def test_warns_when_known_game_executable_or_destination_is_missing(self) -> None:
        mod = replace(
            self.mod, executable="new/game.exe",
            source=replace(self.mod.source, destination="old/Binaries"),
        )
        report = match_supported_games(SteamScanReport(1, 1, [self.app], []), (mod,))
        self.assertEqual(1, len(report.supported_games))
        self.assertFalse(report.supported_games[0].executable_found)
        self.assertTrue(any("no catalogued executable found" in warning for warning in report.warnings))
        self.assertTrue(any("catalog destination" in warning for warning in report.warnings))
        self.assertTrue(any("old" in warning and "Binaries" in warning for warning in report.warnings))

    def test_launcher_match_warns_when_nested_destination_is_missing(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "clairobscurfix")
        self._touch("Expedition33_Steam.exe")
        app = replace(self.app, app_id=mod.steam_app_id, install_dir=mod.install_dir)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.assertTrue(report.supported_games[0].executable_found)
        self.assertTrue(any("catalog destination" in warning for warning in report.warnings))
        destination = self.game_path / mod.source.destination
        destination.mkdir(parents=True)
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod,))
        self.assertFalse(any("catalog destination" in warning for warning in report.warnings))

    def test_multiple_unique_executables_in_one_directory_require_selection(self) -> None:
        mod = replace(self.mod, executable="first.exe")
        other = replace(mod, mod_id="other", steam_app_id="123456", executable="second.exe")
        self._touch(mod.executable)
        self._touch(other.executable)
        app = replace(self.app, app_id="999999")
        report = match_supported_games(SteamScanReport(1, 1, [app], []), (mod, other))
        self.assertEqual({mod.mod_id, other.mod_id}, {game.mod_id for game in report.supported_games})
        self.assertEqual(1, len({game.ambiguity_group for game in report.supported_games}))
        self.assertIsNotNone(report.supported_games[0].ambiguity_group)

    def test_expedition_33_alternatives_at_game_root(self) -> None:
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "clairobscurfix")
        for executable in (mod.executable, *mod.executable_alternatives):
            with self.subTest(executable=executable):
                self._touch(executable)
                shortcut = replace(
                    self.app, app_id="999999", install_dir="different",
                    shortcut_executable=str(self.game_path / executable),
                )
                report = match_supported_games(SteamScanReport(0, 0, [], [], [shortcut]), (mod,))
                (game,) = report.supported_games
                self.assertEqual(str(self.game_path / executable), game.executable_path)
                self.assertEqual(str(self.game_path), game.game_path)
                (self.game_path / executable).unlink()


if __name__ == "__main__":
    unittest.main()
