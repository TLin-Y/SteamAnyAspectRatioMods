import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from backend import plugin_root

sys.path.insert(0, str(plugin_root()))

from helper.errors import PluginError

from launch_setup import LINUX_MANUAL, LaunchSetup, bottle_for, describe, parse_overrides

KEY = r"HKCU\Software\Wine\AppDefaults\Game.exe\DllOverrides"


class FakeBottle:
    def __init__(self, values: dict[str, str] | None = None) -> None:
        self.prefix = Path("/bottles/Steam")
        self.registry = {KEY: dict(values or {})}
        self.writes: list[tuple[str, str | None]] = []

    def read(self, key: str) -> dict[str, str]:
        return dict(self.registry.get(key, {}))

    def write(self, key: str, name: str, value: str | None) -> None:
        self.writes.append((name, value))
        values = self.registry.setdefault(key, {})
        if value is None:
            values.pop(name, None)
        else:
            values[name] = value


class FakeSetup(LaunchSetup):
    def __init__(self, records: Path, bottle: FakeBottle) -> None:
        super().__init__(records, "darwin", Path("/home"))
        self.bottle = bottle

    def _bottle(self, game_path: str) -> FakeBottle:
        return self.bottle


class OverrideTextTests(unittest.TestCase):
    def test_catalog_groups_become_wine_values(self) -> None:
        self.assertEqual(
            {"d3d9": "native,builtin", "dinput8": "native,builtin", "vcruntime140": "builtin"},
            parse_overrides(["d3d9,DInput8=n,b", "vcruntime140=b"]),
        )
        for bad in ("dinput8", "=n,b", "dinput8=x"):
            with self.subTest(bad=bad), self.assertRaises(PluginError):
                parse_overrides([bad])

    def test_values_are_shown_in_launch_option_shorthand(self) -> None:
        self.assertEqual("dinput8=n,b;wininet=b;x=disabled", describe(
            {"wininet": "builtin", "dinput8": "native,builtin", "x": "disabled"},
        ))


class LaunchSetupTests(unittest.TestCase):
    def setUp(self) -> None:
        self.records = Path(tempfile.mkdtemp()) / "wine_overrides.json"

    def test_apply_then_remove_restores_the_bottle_exactly(self) -> None:
        bottle = FakeBottle({"winmm": "builtin", "dsound": "native,builtin"})
        setup = FakeSetup(self.records, bottle)
        self.assertEqual(
            {"currentOptions": "dsound=n,b;winmm=b", "proposedOptions": "dinput8=n,b;dsound=n,b;winmm=n,b"},
            setup.prepare("/game", "Game.exe", ["dinput8,winmm=n,b"]),
        )
        result = setup.apply("1:mod", "/game", "C:\\Game\\Game.exe", ["dinput8,winmm=n,b", "dsound=n,b"])
        self.assertEqual("dinput8=n,b;dsound=n,b;winmm=n,b", result["updatedOptions"])
        self.assertEqual([("dinput8", "native,builtin"), ("winmm", "native,builtin")], bottle.writes)

        removal = setup.remove("1:mod", "/game", "Game.exe")
        self.assertTrue(removal["hadRecord"] and removal["changed"])
        self.assertEqual({"winmm": "builtin", "dsound": "native,builtin"}, bottle.registry[KEY])
        self.assertEqual({}, setup._load())

    def test_remove_keeps_values_the_user_changed_and_ignores_unknown_mods(self) -> None:
        bottle = FakeBottle()
        setup = FakeSetup(self.records, bottle)
        setup.apply("1:mod", "/game", "Game.exe", ["dinput8=n,b"])
        bottle.registry[KEY]["dinput8"] = "native"
        removal = setup.remove("1:mod", "/game", "Game.exe")
        self.assertFalse(removal["changed"])
        self.assertEqual({"dinput8": "native"}, bottle.registry[KEY])
        self.assertFalse(setup.remove("1:mod", "/game", "Game.exe")["hadRecord"])

    def test_linux_without_a_bottle_explains_the_steam_setting(self) -> None:
        setup = LaunchSetup(self.records, "linux", Path(tempfile.mkdtemp()))
        with self.assertRaisesRegex(PluginError, "Launch Options"):
            setup.read("/games/Game", "Game.exe")
        info = setup.info("/games/Game", "Game.exe", ["dinput8=n,b"])
        self.assertEqual((None, LINUX_MANUAL), (info["bottle"], info["message"]))

    def test_info_shows_the_bottle_what_is_set_and_what_the_mod_needs(self) -> None:
        bottle = FakeBottle({"winmm": "native,builtin"})
        bottle.tool = "CrossOver"
        setup = FakeSetup(self.records, bottle)
        info = setup.info("/game", "Game.exe", ["dinput8,winmm=n,b"])
        self.assertEqual({"name": "Steam", "tool": "CrossOver"}, info["bottle"])
        self.assertEqual(("winmm=n,b", "dinput8=n,b;winmm=n,b"), (info["current"], info["required"]))

    def test_the_typed_overrides_become_exactly_what_the_game_has(self) -> None:
        bottle = FakeBottle({"winmm": "native,builtin", "dsound": "builtin"})
        setup = FakeSetup(self.records, bottle)
        self.assertEqual("dinput8=n,b;dsound=b", setup.set_overrides("/game", "Game.exe", " dinput8=n,b ; dsound=builtin;"))
        self.assertEqual({"dinput8": "native,builtin", "dsound": "builtin"}, bottle.registry[KEY])
        self.assertEqual([("winmm", None), ("dinput8", "native,builtin")], bottle.writes)
        self.assertEqual("", setup.set_overrides("/game", "Game.exe", "  "))
        self.assertEqual({}, bottle.registry[KEY])
        with self.assertRaisesRegex(PluginError, "dinput8=n,b"):
            setup.set_overrides("/game", "Game.exe", "dinput8=nb")

    def test_a_game_inside_a_prefix_uses_that_prefix(self) -> None:
        home = Path(tempfile.mkdtemp())
        game = home / "prefix/drive_c/Games/Game"
        game.mkdir(parents=True)
        bottle = LaunchSetup(self.records, "darwin", home)._bottle(str(game))
        self.assertEqual(game.parents[2].resolve(), bottle.prefix.resolve())
        with self.assertRaisesRegex(PluginError, "Launch Options"):
            LaunchSetup(self.records, "linux", home)._bottle(str(game))


class BottleTests(unittest.TestCase):
    def test_crossover_and_generic_prefixes_get_their_wine_command(self) -> None:
        home = Path(tempfile.mkdtemp())
        wine = home / "Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine"
        wine.parent.mkdir(parents=True)
        wine.touch()
        steam = home / "Library/Application Support/CrossOver/Bottles/Steam-3/drive_c/Program Files (x86)/Steam"
        bottle = bottle_for(steam, home)
        self.assertEqual((str(wine), "--bottle", "Steam-3"), bottle.command)
        generic = bottle_for(home / "prefix/drive_c/Steam", home)
        self.assertEqual(("wine",), generic.command)
        self.assertEqual((("WINEPREFIX", str(home / "prefix")),), generic.environment)
        self.assertIsNone(bottle_for(home / ".local/share/Steam", home))


if __name__ == "__main__":
    unittest.main()
