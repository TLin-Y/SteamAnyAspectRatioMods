import configparser
import sys
import unittest
from operator import attrgetter
from zipfile import ZipFile

from support.builders import catalog_mod
from support.catalog import catalog_document
from support.sandbox import PLUGIN_ROOT

from scanner.mod_catalog import (
    IniResolutionPair,
    LauncherReplacement,
    ModBackup,
    load_mod_catalog,
    summarize_credits,
)
from store.mod_sources import mod_readme_path
from utils import ini

REPOSITORY_ROOT = PLUGIN_ROOT.parent
LOADER_LICENSE = "LICENSE-Ultimate-ASI-Loader.txt"
WIN64 = "Binaries/Win64"

# Counts to bump when adding one of our own mods (see AGENTS.md, catalog step 5).
OWN_MODS, OWN_ARCHIVES, OWN_DIRECTORIES, OWN_LOOSE_FILES, RESOLUTION_MODS = 66, 65, 1, 0, 81

# Files our own zips may carry besides the mod files, README.md, the loader license and .jpg screenshots.
ZIP_EXTRAS = {
    "dark-souls-iii": {"README.txt"},
    "brotato": {"LICENSE"},
    "final-fantasy-xiii": {"FF13Fix_Licences/FF13Fix_LICENSE.txt", "FF13Fix_Licences/MinHook_LICENSE.txt"},
    "warriors-orochi-3-ultimate": {"mods/fix/log.txt", "LICENSE-WO3UFix.txt"},
}
NO_LOADER_LICENSE = {"ai-the-somnium-files", "brotato", "emperor-rise-of-the-middle-kingdom", "phoenix-wright-ace-attorney-trilogy"}
NO_ZIP_README = {"final-fantasy-xiii"}  # its README ships in the separate release zip


def _lines(text: bytes) -> list[bytes]:
    """Git may change line endings on checkout, so texts are compared without them."""
    return text.splitlines()


def _credit_names(mod) -> tuple[str, ...]:
    return tuple(credit.name for credit in mod.credits)


_DERIVED = {"credits": _credit_names, "readme": mod_readme_path, "source.files": lambda mod: len(mod.source.files)}


def _field(mod, name: str):
    return _DERIVED[name](mod) if name in _DERIVED else attrgetter(name)(mod)


def _archive_name(mod, path: str) -> str:
    """Where an installed file sits inside the mod's zip (the destination folder is not in the zip)."""
    return path.removeprefix(f"{mod.source.destination}/") if mod.source.destination else path


def _in(folder: str, *names: str) -> tuple[str, ...]:
    return tuple(f"{folder}/{name}" for name in names)


# Catalog values verified by hand (paths from Steam app info or appmanifests); they must not drift silently.
PINNED = {
    "ai-the-somnium-files": {
        "steam_app_id": "948740", "install_dir": "AI The Somnium Files", "executable": "AI_TheSomniumFiles.exe",
        "wine_dll_overrides": (), "backup_file": None,
        "backups": (ModBackup("AI_TheSomniumFiles_Data/Managed/Assembly-CSharp.dll",
                              "AI_TheSomniumFiles_Data/Managed/Assembly-CSharp.dll.ai1-original.bak"),),
    },
    "apollo-justice-ace-attorney-trilogy": {
        "steam_app_id": "2187220", "install_dir": "Apollo Justice Ace Attorney Trilogy", "executable": "GS456.exe",
        "wine_dll_overrides": ("dinput8=n,b",),
    },
    "dead-cells": {
        "steam_app_id": "588650", "install_dir": "Dead Cells", "executable": "deadcells.exe",
        "wine_dll_overrides": ("dinput8=n,b",), "display_tags": (),
    },
    "devil-may-cry-5": {
        "steam_app_id": "601150", "install_dir": "Devil May Cry 5", "executable": "DevilMayCry5.exe",
        "wine_dll_overrides": ("dinput8=n,b",), "display_tags": (),
    },
    "phoenix-wright-ace-attorney-trilogy": {
        "steam_app_id": "787480", "install_dir": "Phoenix Wright Ace Attorney Trilogy", "executable": "PWAAT.exe",
        "display_tags": ("must-read-readme", "new-res-in-game-menu"), "wine_dll_overrides": (),
        "backups": (ModBackup("PWAAT_Data/Managed/Assembly-CSharp.dll",
                              "PWAAT_Data/Managed/Assembly-CSharp.dll.pwaat-original.bak"),),
    },
    "final-fantasy-xiii": {
        "source.destination": "white_data/prog/win/bin", "credits": ("TLin-Y", "rebtd7"),
        "wine_dll_overrides": ("d3d9,dinput8=n,b",),
        "files": _in("white_data/prog/win/bin", "FF13_ARFix.asi", "FF13_ARFix.ini", "dinput8.dll", "d3d9.dll", "FF13Fix.ini"),
        "ini_file": "white_data/prog/win/bin/FF13_ARFix.ini",
    },
    "final-fantasy-xii-the-zodiac-age": {
        "steam_app_id": "595520", "install_dir": "FINAL FANTASY XII THE ZODIAC AGE", "executable": "x64/FFXII_TZA.exe",
        "source.destination": "x64",
        "source.path": "Final Fantasy XII The Zodiac Age/Final-Fantasy-XII-The-Zodiac-Age-v1.0.0.zip",
        "files": _in("x64", "dinput8.dll", "FFXII_TZA_ARFix.asi", "FFXII_TZA_ARFix.ini"),
        "ini_file": "x64/FFXII_TZA_ARFix.ini", "wine_dll_overrides": ("dinput8=n,b",),
    },
    "dynasty-warriors-7-xtreme-legends-definitive-edition": {
        "steam_app_id": "968790", "install_dir": "Dynasty Warriors 7 DX", "executable": "SM6EN.exe",
        "executable_alternatives": ("SM6JP.exe", "SM6TC.exe"), "source.destination": "",
        "files": ("dinput8.dll", "DW7XL_ARFix.asi", "DW7XL_ARFix.ini"), "ini_file": "DW7XL_ARFix.ini",
        "display_tags": (), "wine_dll_overrides": ("dinput8=n,b",),
    },
    "dynasty-warriors-8-xtreme-legends-complete-edition": {
        "steam_app_id": "278080", "install_dir": "Dynasty Warriors 8", "executable": "Launch.exe",
        "source.destination": "", "files": ("dinput8.dll", "DW8XL_ARFix.asi", "DW8XL_ARFix.ini"),
        "ini_file": "DW8XL_ARFix.ini", "display_tags": (), "wine_dll_overrides": ("dinput8=n,b",),
    },
    "samurai-warriors-4-dx": {
        "steam_app_id": "2719200", "install_dir": "SAMURAI WARRIORS 4 DX", "executable": "SamuraiWarriors4DX.exe",
        "source.destination": "", "files": ("dinput8.dll", "SW4DX_ARFix.asi", "SW4DX_ARFix.ini"),
        "ini_file": "SW4DX_ARFix.ini", "display_tags": (), "wine_dll_overrides": ("dinput8=n,b",),
    },
    "one-piece-pirate-warriors-3": {
        "steam_app_id": "331600", "install_dir": "OPPW3", "executable": "oppw3.exe",
        "source.destination": "", "files": ("dinput8.dll", "OPPW3_ARFix.asi", "OPPW3_ARFix.ini"),
        "ini_file": "OPPW3_ARFix.ini", "display_tags": (), "wine_dll_overrides": ("dinput8=n,b",),
    },
    "samurai-warriors-4-ii": {
        "steam_app_id": "348470", "install_dir": "SAMURAI WARRIORS 4-II", "executable": "SW4II.exe",
        "source.destination": "", "files": ("dinput8.dll", "SW4II_ARFix.asi", "SW4II_ARFix.ini"),
        "ini_file": "SW4II_ARFix.ini", "display_tags": (), "wine_dll_overrides": ("dinput8=n,b",),
    },
    "dynasty-warriors-9": {
        "steam_app_id": "730310", "install_dir": "Dynasty Warriors 9", "executable": "DW9.exe",
        "source.destination": "", "files": ("dinput8.dll", "DW9_ARFix.asi", "DW9_ARFix.ini"),
        "ini_file": "DW9_ARFix.ini", "display_tags": (), "wine_dll_overrides": ("dinput8=n,b",),
    },
    "dragonsword-awakening": {
        "steam_app_id": "4570720", "install_dir": "DragonSword  Awakening",
        "executable": f"DS/{WIN64}/DSClient-Win64-Shipping.exe", "executable_alternatives": ("DSClient.exe",),
        "source.destination": f"DS/{WIN64}",
        "files": _in(f"DS/{WIN64}", "winmm.dll", "DSClient_ARFix.asi", "DSClient_ARFix.ini"),
        "ini_file": f"DS/{WIN64}/DSClient_ARFix.ini", "wine_dll_overrides": ("winmm=n,b",),
        "source.path": "DragonSword Awakening/DragonSword-Awakening-v1.0.0.zip",
    },
    "sonic-x-shadow-generations": {
        "steam_app_id": "2513280", "install_dir": "SONIC_X_SHADOW_GENERATIONS",
        "executable": "SONIC_X_SHADOW_GENERATIONS.exe", "executable_alternatives": ("SONIC_GENERATIONS.exe",),
        "files": ("winmm.dll", "SonicShadow_ARFix.asi", "GenerationsARFix.asi", "SonicShadow_ARFix.ini"),
        "ini_file": "SonicShadow_ARFix.ini", "wine_dll_overrides": ("winmm=n,b",),
        "source.path": "SONIC X SHADOW GENERATIONS/SONIC-X-SHADOW-GENERATIONS-v1.0.0.zip",
    },
    "visions-of-mana": {
        "install_dir": "VisionsofMana", "executable": f"VisionsofMana/{WIN64}/VisionsofMana-Win64-Shipping.exe",
        "source.destination": f"VisionsofMana/{WIN64}",
        "files": _in(f"VisionsofMana/{WIN64}", "dxgi.dll", "VoM_ARFix.asi", "VoM_ARFix.ini"),
        "ini_file": f"VisionsofMana/{WIN64}/VoM_ARFix.ini",
    },
    "trials-of-mana": {
        "install_dir": "Trials of Mana", "executable": f"Trials of Mana/{WIN64}/Trials of Mana-Win64-Shipping.exe",
        "source.destination": f"Trials of Mana/{WIN64}",
        "files": _in(f"Trials of Mana/{WIN64}", "dxgi.dll", "TrialsOfMana_ARFix.asi", "TrialsOfMana_ARFix.ini"),
        "ini_file": f"Trials of Mana/{WIN64}/TrialsOfMana_ARFix.ini", "wine_dll_overrides": ("dxgi=n,b",),
        "source.path": "Trials of Mana/Trials-of-Mana-v1.0.1.zip",
    },
    "palworld": {
        "install_dir": "Palworld", "executable": f"Pal/{WIN64}/Palworld-Win64-Shipping.exe",
        "files": _in(f"Pal/{WIN64}", "winmm.dll", "Palworld_ARFix.asi", "Palworld_ARFix.ini"),
    },
    "absolumfix": {
        "steam_app_id": "1904480", "install_dir": "Absolum", "executable": "x64.steam/Absolum.exe",
        "source.destination": "x64.steam",
        "files": _in("x64.steam", "winhttp.dll", "Absolum_ARFix.asi", "Absolum_ARFix.ini"),
        "ini_file": "x64.steam/Absolum_ARFix.ini", "wine_dll_overrides": ("winhttp=n,b",),
        "source.kind": "archive", "source.path": "Reimpl-Absolum/Absolum-v0.1.0.zip", "credits": ("TLin-Y",),
    },
    "dave-the-diver": {
        "steam_app_id": "1868140", "install_dir": "Dave the Diver", "executable": "DaveTheDiver.exe",
        "files": ("winmm.dll", "Dave_ARFix.asi", "Dave_ARFix.ini"), "ini_file": "Dave_ARFix.ini",
        "wine_dll_overrides": ("winmm=n,b",), "source.kind": "archive",
        "source.path": "DAVE THE DIVER/DAVE-THE-DIVER-v1.2.1.zip",
    },
    "brotato": {
        "steam_app_id": "1942280", "install_dir": "Brotato", "executable": "Brotato.exe",
        "files": ("override.cfg", "AnyAspectRatio.gd"), "ini_file": None, "wine_dll_overrides": (),
        "display_tags": ("fullscreen",),
        "source.kind": "archive", "source.path": "Brotato/Brotato-v1.0.0.zip",
    },
    "pentiment": {
        "steam_app_id": "1205520", "install_dir": "Pentiment", "executable": "Pentiment.exe",
        "files": ("winhttp.dll", "Pentiment_ARFix.asi", "Pentiment_ARFix.ini"), "ini_file": "Pentiment_ARFix.ini",
        "wine_dll_overrides": ("winhttp=n,b",), "source.kind": "archive", "source.path": "Pentiment/Pentiment-v0.2.1.zip",
        "ini_resolution": (IniResolutionPair("Resolution", "Width", "Height", "pixels"),),
    },
    "sanabi": {
        "steam_app_id": "1562700", "install_dir": "SANABI", "executable": "SNB.exe",
        "files": ("winmm.dll", "SNB_ARFix.asi", "SNB_ARFix.ini"), "ini_file": "SNB_ARFix.ini",
        "wine_dll_overrides": ("winmm=n,b",), "source.kind": "archive", "source.path": "SANABI/SANABI-v1.1.0.zip",
    },
    "undertale": {
        "install_dir": "Undertale", "executable": "UNDERTALE.exe", "source.kind": "archive",
        "source.path": "external/Twig/Undertale/Undertale-Ultrawide-Fix.zip", "files": ("data.win",),
        "backup_file": "data.win.bak", "readme": "external/Twig/Undertale/README.md", "credits": ("Twig",),
    },
    "plants-vs-zombies-widescreen": {
        "steam_app_id": "3590", "install_dir": "Plants Vs Zombies", "executable": "PlantsVsZombies.exe",
        "source.path": "external/Twig/PvZWidescreenSteam/PvZWidescreenForSteam.zip",
        "files": ("PlantsVsZombies.exe", "main.pak", "pvzgame.exe"),
        "backups": (ModBackup("PlantsVsZombies.exe", "PlantsVsZombies.exe.pvz-original.bak"),
                    ModBackup("main.pak", "main.pak.pvz-original.bak")),
        "readme": "external/Twig/PvZWidescreenSteam/README.md", "credits": ("Twig", "HenryJk"),
    },
    "elden-ring-ultrawide-fix": {
        "offline_only": True, "install_dir": "ELDEN RING", "executable": "Game/eldenring.exe",
        "executable_alternatives": _in("Game", "modengine2_launcher.exe", "start_protected_game.exe",
                                       "start_game_in_offline_mode.exe"),
        "source.destination": "Game",
        "launcher_replacement": LauncherReplacement(*_in("Game", "start_game_in_offline_mode.exe",
                                                         "start_protected_game.exe", "start_protected_game-old.exe")),
        "credits": ("TechieW",), "readme": "external/TechieW/ELDEN RING/README.md",
    },
    "dark-souls-iii": {"offline_only": True, "launcher_replacement": None, "manual": False},
    "beastofreincarnationfix": {
        "install_dir": "BeastOfReincarnation", "source.destination": f"BeastOfReincarnation/{WIN64}",
        "files": _in(f"BeastOfReincarnation/{WIN64}", "BeastOfReincarnationFix.asi", "BeastOfReincarnationFix.ini",
                     "dsound.dll"),
        "ini_file": f"BeastOfReincarnation/{WIN64}/BeastOfReincarnationFix.ini",
    },
    "guacamelee-2": {
        "steam_app_id": "534550", "install_dir": "Guacamelee2", "executable": "Guac2_x64.exe",
        "executable_alternatives": ("Guac2_64.exe",), "files": ("dinput8.dll", "Guac2_ARFix.asi", "Guac2_ARFix.ini"),
        "ini_file": "Guac2_ARFix.ini", "source.path": "Guacamelee 2/Guacamelee-2-v2.0.2.zip",
    },
    "guacamelee-stce": {
        "steam_app_id": "275390", "install_dir": "DrinkBox_Game4", "executable": "GuacSTCE.exe",
        "executable_alternatives": ("Game.exe",), "files": ("dinput8.dll", "Guac1_ARFix.asi", "Guac1_ARFix.ini"),
        "ini_file": "Guac1_ARFix.ini",
        "source.path": "Guacamelee Super Turbo Championship Edition/Guacamelee-STCE-v2.0.1.zip",
    },
    "rayman-legends": {
        "steam_app_id": "242550", "install_dir": "Rayman Legends", "executable": "Rayman Legends.exe",
        "files": ("dinput8.dll", "RaymanLegends_ARFix.asi", "RaymanLegends_ARFix.ini"),
        "ini_file": "RaymanLegends_ARFix.ini",
    },
    "ys-viii-lacrimosa-of-dana": {
        "steam_app_id": "579180", "install_dir": "Ys VIII Lacrimosa of Dana", "executable": "ys8.exe",
        "files": ("dinput8.dll", "Ys8_AspectPatch.asi", "Ys8_AspectPatch.ini"), "ini_file": "Ys8_AspectPatch.ini",
    },
    "star-ocean-the-second-story-r": {
        "steam_app_id": "2238900", "install_dir": "STAR OCEAN THE SECOND STORY R", "executable": "SO2R.exe",
        "files": ("winmm.dll", "SO2R_ARFix.asi", "SO2R_ARFix.ini"), "ini_file": "SO2R_ARFix.ini",
    },
    "dynasty-warriors-6": {  # GOG only; folder from the user's install, not on Steam
        "steam_app_id": "0", "install_dir": "Dynasty Warriors 6", "executable": "DW6_WIN.exe",
    },
    "emperor-rise-of-the-middle-kingdom": {  # GOG only; GOG install folder, not on Steam
        "steam_app_id": "0", "install_dir": "Emperor - Rise of the Middle Kingdom", "executable": "Emperor.exe",
    },
    "wofffix": {"steam_app_id": "552700", "install_dir": "WOFF", "executable": "WOFF.exe",
                "files": ("WOFFFix.asi", "WOFFFix.ini", "dinput8.dll")},
    "doa6fix": {"steam_app_id": "838380", "install_dir": "Dead or Alive 6", "executable": "doa6.exe",
                "files": ("DOA6Fix.asi", "DOA6Fix.ini", "winmm.dll")},
    "so4fix": {"steam_app_id": "609150", "install_dir": "STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster",
               "executable": "StarOceanTheLastHope.exe", "files": ("SO4Fix.asi", "SO4Fix.ini", "winmm.dll")},
    "mdaraincodeplusfix": {
        "steam_app_id": "2903950", "install_dir": "Master Detective Archives RAIN CODE", "executable": "RainCodePlus.exe",
        "files": _in(f"RainCodePlus/{WIN64}", "dsound.dll", "MDARainCodePlusFix.asi", "MDARainCodePlusFix.ini"),
    },
    "clairobscurfix": {
        "install_dir": "Expedition 33", "executable": f"Sandfall/{WIN64}/SandFall-Win64-Shipping.exe",
        "executable_alternatives": ("Expedition33_Steam.exe",), "source.destination": f"Sandfall/{WIN64}",
    },
    "dynasty-warriors-origins": {
        "ini_resolution": (IniResolutionPair("Custom Resolution", "Width", "Height", "pixels"),),
    },
    "ninja-gaiden-sigma-2": {"source.kind": "directory"},
    "ori-and-the-blind-forest-definitive-edition": {
        "source.kind": "archive", "ini_file": "Ori1_ARFix.ini", "wine_dll_overrides": ("winmm=n,b",),
        "backups": (ModBackup("oriDE_Data/Managed/Assembly-CSharp.dll",
                              "oriDE_Data/Managed/Assembly-CSharp.dll.oride-original.bak"),),
    },
    "wildheartsfix": {"manual": False, "executable": "00_game/target_origin/ex/WILD HEARTS.exe"},
    "dbxv2fix": {"manual": True},
}

# Shipped INI defaults: section -> key -> value (bool, int or float).
INI_DEFAULTS = {
    "ai-the-somnium-files": {"Resolution": {"Width": 1280, "Height": 800, "PreserveHUDAspect": True}},
    "apollo-justice-ace-attorney-trilogy": {"Window": {"Width": 1280, "Height": 800}, "Scene": {"BackgroundHeight": 1280}},
    "dead-cells": {"Window": {"Width": 1280, "Height": 800}, "AspectRatio": {"Enabled": True}},
    "devil-may-cry-4-special-edition": {
        "Resolution": {"Width": 1280, "Height": 800, "Borderless": True},
        "AspectRatio": {"Enabled": True, "EventBars": False}, "Camera": {"FOV": 1.0},
    },
    "devil-may-cry-5": {"AspectRatio": {"EnableARFix": True, "AspectWidth": 16, "AspectHeight": 10}},
    "phoenix-wright-ace-attorney-trilogy": {"Resolution": {"Width": 1280, "Height": 800}},
    "final-fantasy-xii-the-zodiac-age": {
        "Window": {"Width": 1280, "Height": 800}, "Resolution": {"Enabled": True}, "FOV": {"Enabled": True},
    },
    "dynasty-warriors-6": {"Resolution": {"Width": 0, "Height": 0}, "FOV": {"Multiplier": 1.0}},
    "dynasty-warriors-7-xtreme-legends-definitive-edition": {"Resolution": {"Width": 0, "Height": 0}, "FOV": {"Multiplier": 1.0}},
    "dynasty-warriors-origins": {
        "Custom Resolution": {"Width": 1280, "Height": 800, "TargetWidth": 3840, "TargetHeight": 2160}, "FOV": {"FOVScale": 0},
    },
    "dynasty-warriors-8-xtreme-legends-complete-edition": {
        "Resolution": {"Width": 0, "Height": 0}, "FOV": {"Multiplier": 1.0},
    },
    "dynasty-warriors-9": {"Resolution": {"Width": 0, "Height": 0}, "FOV": {"Multiplier": 1.0}},
    "samurai-warriors-4-ii": {"Resolution": {"Width": 0, "Height": 0}, "FOV": {"Multiplier": 1.0}},
    "one-piece-pirate-warriors-3": {"Resolution": {"Width": 0, "Height": 0}, "FOV": {"Multiplier": 1.0}},
    "samurai-warriors-4-dx": {"Resolution": {"Width": 0, "Height": 0}, "FOV": {"Multiplier": 1.0}},
    "dragonsword-awakening": {"Resolution": {"Width": 1280, "Height": 800}, "FOV": {"KeepHorizontalFOV": True}},
    "sonic-x-shadow-generations": {
        "Shadow": {"CameraCorrection": True, "RemoveLetterbox": True},
        "Generations": {"CameraCorrection": True, "RemoveLetterbox": True},
        "Diagnostics": {"VerboseDiagnostics": False},
    },
    "emperor-rise-of-the-middle-kingdom": {"Window": {"Width": 1280, "Height": 800, "Borderless": True}},
    "pentiment": {"AspectRatio": {"Enabled": True}, "Resolution": {"Borderless": True, "Width": 1280, "Height": 800}},
    "star-ocean-the-second-story-r": {
        "AspectRatio": {"Enabled": True}, "Resolution": {"Width": 1280, "Height": 800}, "FOV": {"Multiplier": 1.0},
    },
}

README_MENTIONS = {
    "final-fantasy-xii-the-zodiac-age": (b"ff12.jpg", b"ff12demo.jpg", b"ff12demo2.jpg", b"at 4:3",
                                         b"SteamOS / Winlator", b"FOV fixes are enabled by default"),
}

STRETCHED = {
    "rayman-legends",
    "ultrawidefix-lrff13",
}


def _ini_value(config: configparser.ConfigParser, section: str, key: str, expected: object) -> object:
    match expected:
        case bool():
            return config.getboolean(section, key)
        case int():
            return config.getint(section, key)
        case _:
            return config.getfloat(section, key)


class CatalogContentTests(unittest.TestCase):
    """The shipped catalog.json and the mod files it points at in this repository."""

    def setUp(self) -> None:
        self.mods = load_mod_catalog()
        self.own = [mod for mod in self.mods if not mod.credits or mod.credits[0].name == "TLin-Y"]

    def test_pinned_values(self) -> None:
        for mod_id, fields in PINNED.items():
            mod = catalog_mod(mod_id)
            for name, expected in fields.items():
                with self.subTest(mod=mod_id, field=name):
                    self.assertEqual(expected, _field(mod, name))

    def test_own_mod_counts(self) -> None:
        kinds = [mod.source.kind for mod in self.own]
        self.assertEqual((OWN_MODS, OWN_ARCHIVES, OWN_DIRECTORIES, OWN_LOOSE_FILES),
                         (len(self.own), kinds.count("archive"), kinds.count("directory"), kinds.count("files")))
        self.assertEqual(RESOLUTION_MODS, sum(bool(mod.ini_resolution) for mod in self.mods))
        dw3 = next(mod for mod in self.own if mod.steam_app_id == "3841510")
        self.assertEqual(("DW3CE_RE", 3), (dw3.install_dir, len(dw3.files)))

    def test_own_zips_hold_the_mod_files_and_the_folder_readme(self) -> None:
        for mod in self.own:
            if mod.source.kind != "archive":
                continue
            with self.subTest(mod=mod.mod_id), ZipFile(REPOSITORY_ROOT / mod.source.path) as archive:
                self.assertIsNone(archive.testzip())
                names = {name for name in archive.namelist() if not name.endswith("/")}
                required = {_archive_name(mod, path) for path in mod.files}
                if mod.mod_id not in NO_ZIP_README:
                    required.add("README.md")
                if mod.mod_id not in NO_LOADER_LICENSE:
                    required.add(LOADER_LICENSE)
                self.assertLessEqual(required, names)
                extras = {name for name in names - required if not name.endswith(".jpg")}
                self.assertEqual(ZIP_EXTRAS.get(mod.mod_id, set()), extras)
                if "README.md" in required:
                    readme = archive.read("README.md")
                    self.assertEqual(_lines((REPOSITORY_ROOT / mod_readme_path(mod)).read_bytes()), _lines(readme))
                    for mention in README_MENTIONS.get(mod.mod_id, ()):
                        self.assertIn(mention, readme)

    def test_own_readmes_give_the_exact_launch_option(self) -> None:
        for mod in self.own:
            if mod.wine_dll_overrides:
                with self.subTest(mod=mod.mod_id):
                    option = f'WINEDLLOVERRIDES="{";".join(mod.wine_dll_overrides)}" %command%'
                    self.assertIn(option.encode(), (REPOSITORY_ROOT / mod_readme_path(mod)).read_bytes())

    def test_shipped_ini_defaults(self) -> None:
        for mod_id, sections in INI_DEFAULTS.items():
            mod = catalog_mod(mod_id)
            with ZipFile(REPOSITORY_ROOT / mod.source.path) as archive:
                config = configparser.ConfigParser()
                config.read_string(archive.read(_archive_name(mod, mod.ini_file)).decode("utf-8-sig"))
            for section, values in sections.items():
                for key, expected in values.items():
                    with self.subTest(mod=mod_id, key=f"{section}.{key}"):
                        self.assertAlmostEqual(expected, _ini_value(config, section, key, expected))

    def test_every_source_file_is_in_the_repository_and_loose_files_are_hashed(self) -> None:
        for mod in self.own:
            with self.subTest(mod=mod.mod_id):
                self.assertTrue((REPOSITORY_ROOT / mod_readme_path(mod)).is_file())
                match mod.source.kind:
                    case "directory":
                        self.assertTrue(all((REPOSITORY_ROOT / mod.source.path / path).is_file() for path in mod.files))
                    case "files":
                        self.assertTrue(all((REPOSITORY_ROOT / item.path).is_file() for item in mod.source.files))
                if mod.source.kind != "archive":
                    self.assertEqual(set(mod.files), {path for path, _ in mod.source.file_hashes})
        elden_ring = catalog_mod("elden-ring-ultrawide-fix")
        self.assertTrue((REPOSITORY_ROOT / mod_readme_path(elden_ring)).is_file())
        with ZipFile(REPOSITORY_ROOT / elden_ring.source.path) as archive:
            self.assertEqual({f"Game/{info.filename}" for info in archive.infolist() if not info.is_dir()},
                             set(elden_ring.files))

    def test_archive_checksums_are_up_to_date(self) -> None:
        # Fails after a zip changes until scripts/update_catalog_hashes.py is run.
        sys.path.insert(0, str(PLUGIN_ROOT / "scripts"))
        try:
            from update_catalog_hashes import update
        finally:
            sys.path.pop(0)
        self.assertFalse(update(check=True), "run scripts/update_catalog_hashes.py")
        self.assertTrue(all(len(mod.source.sha256) == 64 for mod in self.mods if mod.source.kind == "archive"))

    def test_resolution_fields_exist_once_in_each_shipped_ini(self) -> None:
        for mod in self.mods:
            if not mod.ini_resolution:
                continue
            with self.subTest(mod=mod.mod_id):
                match mod.source.kind:
                    case "archive":
                        with ZipFile(REPOSITORY_ROOT / mod.source.path) as archive:
                            content = archive.read(_archive_name(mod, mod.ini_file))
                    case "files":
                        source = next(item for item in mod.source.files if item.destination == mod.ini_file)
                        content = (REPOSITORY_ROOT / source.path).read_bytes()
                    case _:
                        content = (REPOSITORY_ROOT / mod.source.path / mod.ini_file).read_bytes()
                sections = ini.values_by_section(content.splitlines(keepends=True))
                for pair in mod.ini_resolution:
                    keys = sections.get(pair.section.lower().encode(), {})
                    self.assertEqual((1, 1), (len(keys.get(pair.width.lower().encode(), [])),
                                              len(keys.get(pair.height.lower().encode(), []))))

    def test_resolution_fields_stay_readable_by_released_plugins(self) -> None:
        # Plugins up to 1.8.0 refuse the whole catalog on any other ini_resolution shape or kind.
        for entry in catalog_document()["mods"]:
            for pair in entry.get("ini_resolution", []):
                with self.subTest(mod=entry["id"]):
                    self.assertEqual({"section", "width", "height", "kind"}, set(pair))
                    self.assertIn(pair["kind"], ("pixels", "ratio"))

    def test_stretch_tag_marks_exactly_the_stretched_games(self) -> None:
        tagged = {mod.mod_id for mod in self.mods if "stretch" in mod.display_tags}
        self.assertEqual(STRETCHED, tagged)
        self.assertTrue(all("(Stretch)" not in catalog_mod(mod_id).name for mod_id in tagged))

    def test_credits_rank_the_owner_first_with_support_links(self) -> None:
        authors = {author["name"]: author for author in summarize_credits(self.mods)}
        self.assertEqual(("", OWN_MODS), (authors["TLin-Y"]["url"], authors["TLin-Y"]["mod_count"]))
        self.assertEqual(["Mod list", "Ko-fi"], [link["label"] for link in authors["TLin-Y"]["support"]])
        self.assertEqual(["Patreon", "Ko-fi"], [link["label"] for link in authors["Lyall"]["support"]])

    def test_ff13_package_carries_the_upstream_files_unchanged(self) -> None:
        ff13 = catalog_mod("final-fantasy-xiii")
        upstream_files = ("d3d9.dll", "FF13Fix.ini", "FF13Fix_Licences/FF13Fix_LICENSE.txt",
                          "FF13Fix_Licences/MinHook_LICENSE.txt")
        with (ZipFile(REPOSITORY_ROOT / ff13.source.path) as archive,
              ZipFile(REPOSITORY_ROOT / "external/rebtd7/FF13Fix/FF13Fix_164_.zip") as upstream):
            for name in upstream_files:
                with self.subTest(file=name):
                    self.assertEqual(upstream.read(f"FF13Fix_164/{name}"), archive.read(name))
        folder = REPOSITORY_ROOT / "Final Fantasy XIII"
        with ZipFile(folder / "Final-Fantasy-XIII-v1.1.0.zip") as release:
            self.assertIsNone(release.testzip())
            self.assertEqual(_lines((folder / "README.md").read_bytes()), _lines(release.read("README.md")))
