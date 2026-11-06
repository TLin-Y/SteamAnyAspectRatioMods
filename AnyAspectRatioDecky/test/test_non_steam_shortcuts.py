import struct
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from zipfile import ZipFile

from scanner.mod_catalog import ModDefinition, ModSource
from scanner.mod_installer import apply_mod_files
from scanner.non_steam_shortcuts import parse_shortcuts, scan_non_steam_shortcuts
from scanner.steam_library_scanner import InstalledApp, SteamScanReport
from scanner.supported_game_scanner import match_supported_games


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

    def test_shortcut_with_ambiguous_basename_is_skipped(self) -> None:
        executable = self.game / self.mod.executable
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", str(executable), str(self.game))
        )
        other = replace(self.mod, mod_id="other", executable="elsewhere/b1.exe")
        report = match_supported_games(
            SteamScanReport(0, 0, [], [], shortcuts), (self.mod, other)
        )
        self.assertEqual([], report.supported_games)
        self.assertIn("ambiguous executable", report.warnings[0])

    def test_same_mod_with_ambiguous_root_is_skipped(self) -> None:
        executable = self.game / self.mod.executable
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", str(executable), str(self.game))
        )
        mod = replace(self.mod, executable_alternatives=("b1.exe",))
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (mod,))
        self.assertEqual([], report.supported_games)
        self.assertIn("ambiguous executable", report.warnings[0])

    def test_relative_shortcut_executable_is_not_matched(self) -> None:
        shortcuts = parse_shortcuts(
            _shortcuts_vdf(3000000001, "Game", self.mod.executable, str(self.game))
        )
        report = match_supported_games(SteamScanReport(0, 0, [], [], shortcuts), (self.mod,))
        self.assertEqual([], report.supported_games)

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
