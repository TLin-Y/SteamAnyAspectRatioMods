import configparser
from unittest.mock import patch
from zipfile import ZipFile

from support.builders import app_for, catalog_mod
from support.sandbox import PLUGIN_ROOT, GameTest

from installer.mod_ini import read_mod_ini
from installer.mod_logs import existing_mod_logs, read_mod_log
from scanner.mod_catalog import load_mod_catalog
from scanner.steam_library_scanner import SteamScanReport
from scanner.supported_game_scanner import match_supported_games

REPOSITORY_ROOT = PLUGIN_ROOT.parent
DRAGONTWEAK_ZIP = REPOSITORY_ROOT / "external/Lyall/DragonTweak/DragonTweak_0.0.6.zip"
DRAGONTWEAK_GAMES = (  # app id, install folder, executable
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


def _from_pack(archive):
    """The mod archive is read from the repository instead of being downloaded."""
    return patch("store.mod_sources._pack_file", return_value=archive)


class CatalogInstallTests(GameTest):
    """Real catalog entries installed from the real archives in this repository."""

    def test_ff13_asks_about_its_proxy_dll_then_installs_with_the_resolution(self) -> None:
        ff13 = catalog_mod("final-fantasy-xiii")
        folder = ff13.source.destination
        self.write({f"{folder}/d3d9.dll": "existing proxy"})
        with _from_pack(REPOSITORY_ROOT / ff13.source.path):
            self.assertEqual([f"{folder}/d3d9.dll"], self.apply(ff13, ini_resolution=(1920, 1080)))
            self.assert_files({f"{folder}/d3d9.dll": "existing proxy", f"{folder}/FF13_ARFix.asi": None})
            self.apply(ff13, replace_existing_files=True, ini_resolution=(1920, 1080))
            with ZipFile(REPOSITORY_ROOT / ff13.source.path) as archive:
                self.assert_files({f"{folder}/FF13Fix.ini": archive.read("FF13Fix.ini")})
            config = configparser.ConfigParser()
            config.read(self.game / ff13.ini_file, encoding="utf-8")
            self.assertEqual((1280, 720, 1920, 1080), tuple(
                config.getint("Resolution", key) for key in ("ReplaceWidth", "ReplaceHeight", "TargetWidth", "TargetHeight")))
            self.remove(ff13, preserve_modified_ini=False)
        self.assertEqual([], [path for path in self.game.rglob("*") if path.is_file()])

    def test_dragontweak_matches_installs_and_removes_for_every_yakuza_game(self) -> None:
        mods = {mod.steam_app_id: mod for mod in load_mod_catalog() if mod.mod_id.startswith("dragontweak-")}
        with ZipFile(DRAGONTWEAK_ZIP) as archive:
            self.assertIsNone(archive.testzip())
            contents = {name: archive.read(name) for name in ("dinput8.dll", "DragonTweak.asi", "DragonTweak.ini")}
        for app_id, install_dir, executable in DRAGONTWEAK_GAMES:
            with self.subTest(app_id=app_id):
                mod = mods[app_id]
                folder = executable.rpartition("/")[0]
                prefix = f"{folder}/" if folder else ""
                self.assertEqual((False, install_dir, executable, folder, ("dinput8=n,b",)),
                                 (mod.manual, mod.install_dir, mod.executable, mod.source.destination,
                                  mod.wine_dll_overrides))
                self.assertEqual((tuple(f"{prefix}{name}" for name in contents), f"{prefix}DragonTweak.ini"),
                                 (mod.files, mod.ini_file))

                self.write({executable: "game"})
                (matched,) = match_supported_games(SteamScanReport(1, 1, [app_for(mod, self.game)], []), (mod,)
                                                   ).supported_games
                self.assertEqual((str(self.game / executable), True, False),
                                 (matched.executable_path, matched.executable_found, matched.to_dict()["manual"]))
                with _from_pack(DRAGONTWEAK_ZIP):
                    self.apply(mod)
                self.assert_files({f"{prefix}{name}": content for name, content in contents.items()})
                self.assertEqual((contents["DragonTweak.ini"].decode("utf-8-sig"), True), read_mod_ini(mod, str(self.game)))
                self.remove(mod)
                self.assertEqual([self.game / executable], [path for path in self.game.rglob("*") if path.is_file()])
                (self.game / executable).unlink()

    def test_manual_mods_are_listed_but_never_installed(self) -> None:
        manual = catalog_mod("dbxv2fix")
        (game,) = match_supported_games(SteamScanReport(1, 1, [app_for(manual, self.game)], []), (manual,)
                                        ).supported_games
        self.assertEqual((True, []), (game.to_dict()["manual"], game.to_dict()["display_tags"]))
        for operation in (self.apply, self.remove):
            with self.subTest(operation=operation.__name__), self.assertRaisesRegex(ValueError, "manually"):
                operation(manual)
        self.assertEqual([], list(self.game.iterdir()))

    def test_asi_logs_use_the_real_mod_file_names(self) -> None:
        for mod_id, log in (("bloodstained-ritual-of-the-night", "BloodstainedRotN/Binaries/Win64/BloodstainedARFix.log"),
                            ("sonic-x-shadow-generations", "SonicShadow_ARFix.log")):
            with self.subTest(mod=mod_id):
                mod = catalog_mod(mod_id)
                self.write({log: f"{mod_id} log"})
                self.assertEqual([log], existing_mod_logs(mod.files, str(self.game)))
                self.assertEqual((f"{mod_id} log", False), read_mod_log(mod, str(self.game), log))
