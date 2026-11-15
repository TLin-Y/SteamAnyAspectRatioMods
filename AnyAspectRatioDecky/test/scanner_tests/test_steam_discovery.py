from pathlib import Path

from support.builders import shortcuts_vdf
from support.sandbox import SandboxTest

from scanner import steam_layout
from scanner.non_steam_shortcuts import parse_shortcuts
from scanner.steam_library_scanner import SteamScanReport, find_steam_roots, scan_steam_libraries


def manifest(app_id: str, install_dir: str, name: str = "Game") -> str:
    return f'"AppState" {{ "appid" "{app_id}" "name" "{name}" "installdir" "{install_dir}" }}'


def library_config(*libraries: Path) -> str:
    entries = " ".join(f'"{index}" {{ "path" "{path.as_posix()}" }}' for index, path in enumerate(libraries))
    return f'"libraryfolders" {{ {entries} }}'


class SteamLibraryTests(SandboxTest):
    def library(self, folder: str, apps: dict[str, str], *more_libraries: Path, games: bool = True) -> Path:
        """A Steam library listing itself (and more_libraries), with an app manifest per app id -> install folder."""
        root = self.folder(folder)
        self.write({"steamapps/libraryfolders.vdf": library_config(root, *more_libraries)}, root)
        for app_id, install_dir in apps.items():
            self.write({f"steamapps/appmanifest_{app_id}.acf": manifest(app_id, install_dir)}, root)
            if games:
                (root / "steamapps/common" / install_dir.replace("\\\\", "/")).mkdir(parents=True, exist_ok=True)
        return root

    def scan(self, *libraries: Path) -> SteamScanReport:
        return scan_steam_libraries(list(libraries))

    def test_steam_roots_come_from_known_folders_and_steam_itself(self) -> None:
        native = self.folder(".local/share/Steam")
        environment = {"STEAM_COMPAT_CLIENT_INSTALL_PATH": str(native)}
        self.assertEqual([native.resolve()], find_steam_roots(home=self.root, environment=environment))
        flatpak = self.folder(".var/app/com.valvesoftware.Steam/.local/share/Steam")
        self.assertEqual([native.resolve(), flatpak.resolve()], find_steam_roots(home=self.root, environment={}))

    def test_manifest_app_id_needs_the_exact_manifest_name(self) -> None:
        for name, expected in (("appmanifest_123.acf", "123"), ("appmanifest_.acf", None), ("appmanifest_12a.acf", None),
                               ("appmanifest_123.acf.bak", None), ("xappmanifest_123.acf", None),
                               ("appmanifest_\u0661.acf", None)):
            with self.subTest(name=name):
                self.assertEqual(expected, steam_layout.manifest_app_id(Path(name)))

    def test_scans_the_default_and_removable_libraries(self) -> None:
        sd_card = self.library("sd-card/SteamLibrary", {"200": "Nested/Game B"})
        report = self.scan(self.library("Steam", {"100": "Game A"}, sd_card))
        self.assertEqual((2, ["100", "200"]), (report.libraries_scanned, [app.app_id for app in report.apps]))
        self.assertEqual(("Nested/Game B", str(sd_card.resolve())), (report.apps[1].install_dir, report.apps[1].library_path))

    def test_problems_are_warned_about_without_hiding_other_games(self) -> None:
        steam = self.library("Steam", {"100": "Good"})
        self.write({"steamapps/appmanifest_200.acf": '"broken" {'}, steam)
        report = self.scan(steam)
        self.assertEqual(["100"], [app.app_id for app in report.apps])
        self.assertEqual(1, len(report.warnings))
        self.assertIn("appmanifest_200.acf", report.warnings[0])

        (steam / "steamapps/libraryfolders.vdf").unlink()
        self.assertTrue(any("configuration not found" in warning for warning in self.scan(steam).warnings))

        self.write({"steamapps/libraryfolders.vdf": library_config(steam, self.root / "missing/SteamLibrary")}, steam)
        report = self.scan(steam)
        self.assertEqual(1, report.apps_scanned)
        self.assertTrue(any("unavailable" in warning for warning in report.warnings))

    def test_a_moved_game_keeps_its_largest_copy(self) -> None:
        first = self.library("Steam", {"100": "Game"})
        second = self.library("sd/SteamLibrary", {"100": "Game"})
        self.write({"steamapps/common/Game/leftover.txt": "x"}, first)
        self.write({"steamapps/common/Game/game.bin": "x" * 1000}, second)
        moved = str((second / "steamapps/common/Game").resolve())
        for order in ((first, second), (second, first)):
            with self.subTest(first=order[0].name):
                report = self.scan(*order)
                self.assertEqual((1, moved), (report.apps_scanned, report.apps[0].game_path))

    def test_manifests_cannot_point_outside_the_library(self) -> None:
        unsafe = {"101": "..\\\\Outside", "102": "Game\\\\..\\\\..\\\\Outside", "103": "C:\\\\Games\\\\X", "104": "../Outside"}
        steam = self.library("Steam", {**unsafe, "200": "Good Game", "201": "Studio\\\\Game"}, games=False)
        (steam / "steamapps/common/Good Game").mkdir(parents=True)
        self.write({"steamapps/appmanifest_300.acf": manifest("999", "Game")}, steam)
        report = self.scan(steam)
        self.assertEqual(["200", "201"], sorted(app.app_id for app in report.apps))
        self.assertEqual(4, sum("Unsafe install directory" in warning for warning in report.warnings))
        self.assertTrue(any("does not match" in warning for warning in report.warnings))


class ShortcutFileTests(SandboxTest):
    def test_parses_a_shortcut_with_an_unsigned_app_id(self) -> None:
        game, script = self.folder("games/Wukong"), str(self.root / "x.sh")
        (app,) = parse_shortcuts(shortcuts_vdf((3000000001, "Black Myth GOG", script, str(game))))
        self.assertEqual(("3000000001", "Black Myth GOG", str(game), script),
                         (app.app_id, app.name, app.game_path, app.shortcut_executable))
