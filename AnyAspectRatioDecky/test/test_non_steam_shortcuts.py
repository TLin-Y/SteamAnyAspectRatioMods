import struct
import importlib
import logging
import sys
import tempfile
import types
import unittest
from dataclasses import replace
from pathlib import Path
from zipfile import ZipFile
from unittest.mock import patch
import asyncio

from scanner.mod_catalog import ModDefinition, ModSource, load_mod_catalog
from scanner.mod_installer import apply_mod_files
from scanner.non_steam_shortcuts import parse_shortcuts, scan_non_steam_shortcuts
from scanner.steam_library_scanner import InstalledApp, SteamScanReport
from scanner.supported_game_scanner import match_supported_games
from store import PluginStore
from store.dependencies import CatalogSource


def _string(key: str, value: str) -> bytes:
    return b"\x01" + key.encode() + b"\x00" + value.encode() + b"\x00"


def _shortcuts_vdf(app_id: int, name: str, exe: str, start_dir: str) -> bytes:
    entry = (
        b"\x02appid\x00" + struct.pack("<I", app_id)
        + _string("AppName", name)
        + _string("Exe", f'"{exe}"')
        + _string("StartDir", f'"{start_dir}"')
        + b"\x08"
    )
    return b"\x00shortcuts\x00\x000\x00" + entry + b"\x08\x08"


class NonSteamShortcutTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / "games" / "Wukong"
        (self.game / "b1" / "Binaries" / "Win64").mkdir(parents=True)
        (self.game / "b1" / "Binaries" / "Win64" / "b1.exe").write_bytes(b"x")
        self.mod = ModDefinition(
            mod_id="wukongtweak",
            name="Wukong - WukongTweak",
            steam_app_id="2358720",
            install_dir="BlackMythWukong",
            executable="b1/Binaries/Win64/b1.exe",
            source=ModSource("archive", "x.zip", "", (), "", ""),
            files=("b1/Binaries/Win64/WukongTweak.asi",),
            ini_file=None,
            wine_dll_overrides=("dsound=n,b",),
        )

    def _write_shortcuts(self, start_dir: Path) -> None:
        config = self.root / "steam" / "userdata" / "1" / "config"
        config.mkdir(parents=True)
        (config / "shortcuts.vdf").write_bytes(
            _shortcuts_vdf(
                3000000001, "Black Myth GOG",
                str(self.game / self.mod.executable), str(start_dir),
            )
        )

    def test_parses_shortcut_with_unsigned_app_id(self) -> None:
        executable = str(self.root / "x.sh")
        data = _shortcuts_vdf(3000000001, "Black Myth GOG", executable, str(self.game))
        (app,) = parse_shortcuts(data)
        self.assertEqual(app.app_id, "3000000001")
        self.assertEqual(app.name, "Black Myth GOG")
        self.assertEqual(app.game_path, str(self.game))
        self.assertEqual(app.shortcut_executable, executable)

    def test_shortcut_matches_mod_by_executable_path(self) -> None:
        self._write_shortcuts(self.game)
        shortcuts = scan_non_steam_shortcuts([self.root / "steam"])
        scan = SteamScanReport(0, 0, [], [], shortcuts)
        report = match_supported_games(scan, (self.mod,))
        (game,) = report.supported_games
        self.assertEqual(game.steam_app_id, "3000000001")
        self.assertEqual(game.mod_id, "wukongtweak")
        self.assertTrue(game.executable_found)

    def test_shortcut_without_matching_executable_is_ignored(self) -> None:
        other_exe = self.game / "other.exe"
        other_exe.touch()
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Other", str(other_exe), str(self.game))
        )
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (self.mod,))
        self.assertEqual(report.supported_games, [])

    def test_nested_shortcut_resolves_catalog_game_root(self) -> None:
        executable = self.game / self.mod.executable
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Black Myth GOG", str(executable), str(executable.parent))
        )
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (self.mod,))
        (game,) = report.supported_games
        self.assertEqual(game.game_path, str(self.game))
        self.assertEqual(game.executable_path, str(executable))

    def test_nested_shortcut_apply_uses_catalog_game_root(self) -> None:
        executable = self.game / self.mod.executable
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", str(executable), str(executable.parent))
        )
        archive_path = self.root / "mod.zip"
        with ZipFile(archive_path, "w") as archive:
            archive.writestr(self.mod.files[0], b"mod contents")
        mod = replace(self.mod, source=replace(self.mod.source, kind="local_archive", path=str(archive_path)))
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (mod,))
        (game,) = report.supported_games
        self.assertEqual([], apply_mod_files(mod, game.game_path))
        self.assertEqual((self.game / self.mod.files[0]).read_bytes(), b"mod contents")

    def test_wrapper_shortcut_is_not_matched_from_start_directory(self) -> None:
        wrapper = self.game / "run.sh"
        wrapper.touch()
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Other", str(wrapper), str(self.game))
        )
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (self.mod,))
        self.assertEqual([], report.supported_games)

    def test_shortcut_with_ambiguous_basename_requires_selection(self) -> None:
        executable = self.game / self.mod.executable
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", str(executable), str(self.game))
        )
        other = replace(self.mod, mod_id="other", executable="elsewhere/b1.exe")
        report = match_supported_games(
            SteamScanReport(0, 0, [], [], shortcuts), (self.mod, other)
        )
        (game,) = report.supported_games
        self.assertEqual(self.mod.mod_id, game.mod_id)
        self.assertIsNotNone(game.ambiguity_group)

    def test_same_mod_with_ambiguous_root_requires_selection(self) -> None:
        executable = self.game / self.mod.executable
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", str(executable), str(self.game))
        )
        mod = replace(self.mod, executable_alternatives=("b1.exe",))
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (mod,))
        self.assertEqual(2, len(report.supported_games))
        self.assertEqual({str(self.game), str(executable.parent)}, {g.game_path for g in report.supported_games})
        self.assertEqual(1, len({g.ambiguity_group for g in report.supported_games}))
        self.assertIsNotNone(report.supported_games[0].ambiguity_group)

    def test_shared_game_exe_offers_all_three_choices(self) -> None:
        executable = self.game / "game.exe"
        executable.touch()
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "DOA5", str(executable), str(self.game))
        )
        mods = tuple(mod for mod in load_mod_catalog() if mod.executable == "game.exe")
        self.assertEqual({"doa5lrfix", "sotdfix", "p5strikersfix"}, {mod.mod_id for mod in mods})
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), mods)
        self.assertEqual({mod.mod_id for mod in mods}, {game.mod_id for game in report.supported_games})
        self.assertEqual(1, len({game.ambiguity_group for game in report.supported_games}))
        self.assertTrue(all(game.ambiguity_group and game.matched_by_executable for game in report.supported_games))
        fake_decky = types.ModuleType("decky")
        fake_decky.logger = logging.getLogger("test_non_steam_shortcuts")
        with patch.dict(sys.modules, {"decky": fake_decky}):
            main = importlib.import_module("main")
        with patch.object(main.Plugin, "_scan", return_value=report):
            for mod in mods:
                self.assertEqual((mod, str(self.game)), main.Plugin._resolve_mod_game(mod.mod_id, str(self.game)))
            with self.assertRaisesRegex(ValueError, "no longer available"):
                main.Plugin._resolve_mod_game(mods[0].mod_id, str(self.root))

    def test_choice_is_persisted_and_other_matches_are_hidden_after_scan(self) -> None:
        executable = self.game / "game.exe"
        executable.touch()
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "DOA5", str(executable), str(self.game))
        )
        mods = tuple(mod for mod in load_mod_catalog() if mod.executable == "game.exe")
        scan = SteamScanReport(0, 0, [], [], shortcuts)
        store = PluginStore(self.root / "data")
        fake_decky = types.ModuleType("decky")
        fake_decky.logger = logging.getLogger("test_non_steam_shortcuts")
        with patch.dict(sys.modules, {"decky": fake_decky}):
            main = importlib.import_module("main")
        with (
            patch.object(main, "get_store", return_value=store),
            patch.object(main, "load_active_catalog", return_value=CatalogSource("test", mods)),
            patch.object(main, "scan_supported_games_report", side_effect=lambda *args, **kwargs: match_supported_games(scan, mods)),
        ):
            plugin = main.Plugin()
            initial = asyncio.run(plugin.scan_supported_games())
            games = initial["supported_games"]
            self.assertEqual(3, len(games))
            choice = next(game for game in games if game["mod_id"] == "doa5lrfix")
            with self.assertRaisesRegex(ValueError, "no longer available"):
                asyncio.run(plugin.select_matched_game(choice["ambiguity_group"], "sotdfix", str(self.root)))
            asyncio.run(plugin.select_matched_game(choice["ambiguity_group"], choice["mod_id"], choice["game_path"]))
            self.assertEqual(
                {"mod_id": "doa5lrfix", "game_path": str(self.game)},
                PluginStore(self.root / "data").load_matched_games()[choice["ambiguity_group"]],
            )
            self.assertIsNone(asyncio.run(plugin.get_cached_scan()))
            store.save_last_scan(initial)
            cached = asyncio.run(plugin.get_cached_scan())["report"]["supported_games"]
            self.assertEqual(["doa5lrfix"], [game["mod_id"] for game in cached])
            self.assertTrue(cached[0]["match_locked"])
            refreshed = asyncio.run(plugin.scan_supported_games())["supported_games"]
            self.assertEqual(["doa5lrfix"], [game["mod_id"] for game in refreshed])
            self.assertTrue(refreshed[0]["match_locked"])
            with self.assertRaisesRegex(ValueError, "changed"):
                asyncio.run(plugin.clear_matched_game(choice["ambiguity_group"], "sotdfix", str(self.game)))
            self.assertEqual("doa5lrfix", main.Plugin._resolve_mod_game("doa5lrfix", str(self.game))[0].mod_id)
            with self.assertRaisesRegex(ValueError, "no longer available"):
                main.Plugin._resolve_mod_game("sotdfix", str(self.game))
            store.set_matched_game("another-shortcut", "doa5lrfix", str(self.game))
            store.set_matched_game("unrelated", "doa5lrfix", str(self.root))
            with patch.object(main, "remove_mod_files", side_effect=OSError("removal failed")):
                with self.assertRaisesRegex(OSError, "removal failed"):
                    asyncio.run(plugin.set_mod_applied("doa5lrfix", str(self.game), False))
            self.assertIn(choice["ambiguity_group"], store.load_matched_games())
            with (
                patch.object(main, "remove_mod_files", return_value=[]),
                patch.object(store, "set_applied_hash", side_effect=OSError("hash write failed")),
            ):
                with self.assertRaisesRegex(ValueError, "applied version could not be saved"):
                    asyncio.run(plugin.set_mod_applied("doa5lrfix", str(self.game), False))
            self.assertIn(choice["ambiguity_group"], store.load_matched_games())
            with (
                patch.object(main, "remove_mod_files", return_value=[]),
                patch.object(store, "remove_matched_game", side_effect=OSError("match write failed")),
            ):
                with self.assertRaisesRegex(ValueError, "files were removed.*saved game match"):
                    asyncio.run(plugin.set_mod_applied("doa5lrfix", str(self.game), False))
            self.assertIn(choice["ambiguity_group"], store.load_matched_games())
            mod = next(mod for mod in mods if mod.mod_id == "doa5lrfix")
            for name in mod.files:
                (self.game / name).write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "Remove the mod files"):
                asyncio.run(plugin.clear_matched_game(choice["ambiguity_group"], choice["mod_id"], choice["game_path"]))
            log_path = self.game / "DOA5LRFix.log"
            log_path.write_text("mod log", encoding="utf-8")
            refreshed = asyncio.run(plugin.scan_supported_games())["supported_games"]
            self.assertEqual(["DOA5LRFix.log"], refreshed[0]["log_files"])
            result = asyncio.run(plugin.get_mod_log(choice["mod_id"], choice["game_path"], "DOA5LRFix.log"))
            self.assertEqual("mod log", result["content"])
            self.assertFalse(result["truncated"])
            log_path.unlink()
            cached = asyncio.run(plugin.get_cached_scan())["report"]["supported_games"]
            self.assertEqual([], cached[0]["log_files"])
            log_path.write_text("mod log", encoding="utf-8")
            with patch("scanner.mod_installer._read_source_file", return_value=b"original"):
                result = asyncio.run(plugin.set_mod_applied("doa5lrfix", str(self.game), False))
            self.assertEqual(["DOA5LRFix.ini"], result["conflicting_files"])
            self.assertTrue(log_path.is_file())
            self.assertIn(choice["ambiguity_group"], store.load_matched_games())
            self.assertTrue(all((self.game / name).is_file() for name in mod.files))
            with patch("scanner.mod_installer._read_source_file", return_value=b"original"):
                asyncio.run(plugin.set_mod_applied("doa5lrfix", str(self.game), False, None, True))
            self.assertFalse(log_path.exists())
            self.assertEqual(b"modified", (self.game / mod.ini_file).read_bytes())
            self.assertTrue(all(not (self.game / name).exists() for name in mod.files if name != mod.ini_file))
            self.assertEqual(
                {"unrelated": {"mod_id": "doa5lrfix", "game_path": str(self.root)}},
                store.load_matched_games(),
            )
            after = asyncio.run(plugin.scan_supported_games())["supported_games"]
            self.assertEqual({"doa5lrfix", "sotdfix", "p5strikersfix"}, {game["mod_id"] for game in after})
            self.assertTrue(all(not game["match_locked"] for game in after))
            asyncio.run(plugin.select_matched_game(choice["ambiguity_group"], choice["mod_id"], choice["game_path"]))
            with patch.object(main, "remove_mod_files", return_value=[]):
                asyncio.run(plugin.set_mod_applied("doa5lrfix", str(self.game), False))
            self.assertNotIn(choice["ambiguity_group"], store.load_matched_games())
            (self.game / mod.ini_file).unlink()
            asyncio.run(plugin.select_matched_game(choice["ambiguity_group"], choice["mod_id"], choice["game_path"]))
            asyncio.run(plugin.clear_matched_game(choice["ambiguity_group"], choice["mod_id"], choice["game_path"]))
            self.assertNotIn(choice["ambiguity_group"], store.load_matched_games())

    def test_relative_shortcut_executable_is_not_matched(self) -> None:
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", self.mod.executable, str(self.game))
        )
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (self.mod,))
        self.assertEqual([], report.supported_games)

    def test_manual_choice_can_be_cleared_even_when_files_exist(self) -> None:
        executable = self.game / self.mod.executable
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", str(executable), str(self.game))
        )
        manual = replace(self.mod, manual=True)
        other = replace(manual, mod_id="other")
        scan = SteamScanReport(0, 0, [], [], shortcuts)
        store = PluginStore(self.root / "data")
        fake_decky = types.ModuleType("decky")
        fake_decky.logger = logging.getLogger("test_non_steam_shortcuts")
        with patch.dict(sys.modules, {"decky": fake_decky}):
            main = importlib.import_module("main")
        target = self.game / manual.files[0]
        target.write_bytes(b"existing")
        with (
            patch.object(main, "get_store", return_value=store),
            patch.object(main, "load_active_catalog", return_value=CatalogSource("test", (manual, other))),
            patch.object(main, "scan_supported_games_report", side_effect=lambda *args, **kwargs: match_supported_games(scan, (manual, other))),
        ):
            plugin = main.Plugin()
            games = asyncio.run(plugin.scan_supported_games())["supported_games"]
            choice = next(game for game in games if game["mod_id"] == manual.mod_id)
            asyncio.run(plugin.select_matched_game(choice["ambiguity_group"], choice["mod_id"], choice["game_path"]))
            asyncio.run(plugin.clear_matched_game(choice["ambiguity_group"], choice["mod_id"], choice["game_path"]))
            self.assertEqual({}, store.load_matched_games())
            self.assertEqual(b"existing", target.read_bytes())
            self.assertEqual(2, len(asyncio.run(plugin.scan_supported_games())["supported_games"]))

    def test_steam_install_in_same_folder_is_not_listed_twice(self) -> None:
        self._write_shortcuts(self.game)
        shortcuts = scan_non_steam_shortcuts([self.root / "steam"])
        steam_app = InstalledApp(
            "2358720", "Black Myth: Wukong", "BlackMythWukong", "", str(self.game)
        )
        report = match_supported_games(
            SteamScanReport(1, 1, [steam_app], [], shortcuts), (self.mod,)
        )
        self.assertEqual(len(report.supported_games), 1)
        self.assertEqual(report.supported_games[0].steam_app_id, "2358720")


if __name__ == "__main__":
    unittest.main()
