"""Where a mod's Wine DLL overrides go on each desktop OS.

Windows needs none. Windows Steam in a macOS Wine bottle (CrossOver, Whisky) ignores WINEDLLOVERRIDES,
so the overrides go into the bottle's registry for the game's exe only. Native Linux Steam takes them
as a launch option, which the user sets in Steam.
"""
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath

from helper.errors import PluginError
from scanner import steam_layout
from scanner.steam_library_scanner import _library_roots

MODES = {"n": "native", "b": "builtin"}
_REG_VALUE = re.compile(r"^\s+(\S+)\s+REG_SZ\s+(.*?)\s*$")
_CROSSOVER_APPS = ("Applications/CrossOver.app", "/Applications/CrossOver.app")
_CROSSOVER_WINE = "Contents/SharedSupport/CrossOver/bin/wine"
_WHISKY_WINE = "Library/Application Support/com.isaacmarovitz.Whisky/Libraries/Wine/bin/wine64"
LINUX_MANUAL = (
    "This app cannot change Steam launch options on Linux. "
    "Set them in Steam: right-click the game > Properties > General > Launch Options."
)


def parse_overrides(groups: list[str]) -> dict[str, str]:
    """Groups such as "d3d9,dinput8=n,b" as {dll: "native,builtin"}; modes may also be spelled out."""
    spelled = {name: name for name in MODES.values()}
    overrides: dict[str, str] = {}
    for group in groups:
        dlls, separator, modes = group.partition("=")
        values = [MODES.get(mode.strip().lower()) or spelled.get(mode.strip().lower()) for mode in modes.split(",")]
        names = [dll.strip().lower() for dll in dlls.split(",")]
        if not separator or not all(names) or not all(values):
            raise PluginError(f"Invalid Wine DLL override: {group.strip()} (use the form dinput8=n,b)")
        for dll in names:
            overrides[dll] = ",".join(values)
    return overrides


def parse_override_text(text: str) -> dict[str, str]:
    """What the user typed, in launch-option form: "dinput8=n,b;winmm=n,b". Empty means none."""
    return parse_overrides([group for group in text.split(";") if group.strip()])


def describe(values: dict[str, str]) -> str:
    short = {name: letter for letter, name in MODES.items()}
    return ";".join(
        f"{dll}={','.join(short[mode] for mode in value.split(','))}" if set(value.split(",")) <= short.keys()
        else f"{dll}={value}"
        for dll, value in sorted(values.items())
    )


@dataclass(frozen=True)
class Bottle:
    prefix: Path
    command: tuple[str, ...]
    environment: tuple[tuple[str, str], ...] = ()
    tool: str = "Wine"

    def reg(self, *args: str) -> subprocess.CompletedProcess:
        environment = {**os.environ, **dict(self.environment)}
        try:
            return subprocess.run(
                [*self.command, "reg", *args], capture_output=True, text=True, errors="replace",
                env=environment, timeout=60,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise PluginError(f"Could not run Wine for the bottle {self.prefix.name}: {error}") from error

    def read(self, key: str) -> dict[str, str]:
        result = self.reg("query", key)
        if result.returncode != 0:
            return {}  # The key does not exist yet.
        return {
            match[1].lower(): match[2]
            for line in result.stdout.splitlines()
            if (match := _REG_VALUE.match(line))
        }

    def write(self, key: str, name: str, value: str | None) -> None:
        args = ("add", key, "/v", name, "/d", value, "/f") if value is not None else ("delete", key, "/v", name, "/f")
        result = self.reg(*args)
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip().splitlines()
            raise PluginError(f"Wine could not update {name} in the bottle {self.prefix.name}: {detail[-1] if detail else result.returncode}")


def _crossover_command(prefix: Path, home: Path) -> tuple[str, ...] | None:
    for app in _CROSSOVER_APPS:
        wine = (home / app if not app.startswith("/") else Path(app)) / _CROSSOVER_WINE
        if wine.is_file():
            return (str(wine), "--bottle", prefix.name)
    return None


def bottle_for(steam_root: Path, home: Path) -> Bottle | None:
    prefix = next((folder.parent for folder in (steam_root, *steam_root.parents) if folder.name == "drive_c"), None)
    if prefix is None:
        return None
    if prefix.parent.parent.name == "CrossOver":
        command = _crossover_command(prefix, home)
        if command is None:
            raise PluginError("CrossOver.app was not found in Applications.")
        return Bottle(prefix, command, tool="CrossOver")
    whisky = home / _WHISKY_WINE
    if "com.isaacmarovitz.Whisky" in prefix.parts and whisky.is_file():
        return Bottle(prefix, (str(whisky),), (("WINEPREFIX", str(prefix)),), "Whisky")
    return Bottle(prefix, ("wine",), (("WINEPREFIX", str(prefix)),))


class LaunchSetup:
    """Owns the DLL overrides this app added, so removing a mod restores exactly what was there before."""

    def __init__(self, records_path: Path, platform: str = sys.platform, home: Path | None = None) -> None:
        self._records_path = records_path
        self._platform = platform
        self._home = Path.home() if home is None else home

    def _bottle(self, game_path: str) -> Bottle:
        game = Path(game_path).resolve()
        inside = bottle_for(game, self._home)  # A game installed in the bottle's own drive_c.
        if inside is not None:
            return inside
        for root in steam_layout.system_roots(self._platform, self._home):
            libraries, _ = _library_roots(root)
            if any(game.is_relative_to(library.resolve()) for library in libraries):
                bottle = bottle_for(root, self._home)
                if bottle is not None:
                    return bottle
        if self._platform == "darwin":
            raise PluginError("The game's Steam is not inside a CrossOver or Whisky bottle.")
        raise PluginError(LINUX_MANUAL)

    @staticmethod
    def _key(executable_path: str) -> str:
        exe = PureWindowsPath(executable_path).name if "\\" in executable_path else Path(executable_path).name
        if not exe:
            raise PluginError("The game's executable is unknown, so its Wine overrides cannot be set.")
        return rf"HKCU\Software\Wine\AppDefaults\{exe}\DllOverrides"

    def _load(self) -> dict[str, dict]:
        try:
            return json.loads(self._records_path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return {}
        except (OSError, ValueError) as error:
            raise PluginError(f"Saved Wine override records are unreadable: {error}") from error

    def _save(self, records: dict[str, dict]) -> None:
        self._records_path.parent.mkdir(parents=True, exist_ok=True)
        staged = self._records_path.with_suffix(".tmp")
        staged.write_text(json.dumps(records, indent=2, sort_keys=True), encoding="utf-8")
        os.replace(staged, self._records_path)

    def info(self, game_path: str, executable_path: str, groups: list[str]) -> dict[str, object]:
        required = parse_overrides(groups)
        launch_option = f'WINEDLLOVERRIDES="{";".join(groups)}" %command%'
        try:
            bottle = self._bottle(game_path)
        except PluginError as error:
            return {"bottle": None, "message": str(error), "launch_option": launch_option, "required": describe(required)}
        current = bottle.read(self._key(executable_path))
        return {
            "bottle": {"name": bottle.prefix.name, "tool": bottle.tool},
            "current": describe(current),
            "required": describe(required),
            "launch_option": launch_option,
        }

    def set_overrides(self, game_path: str, executable_path: str, text: str) -> str:
        """Make the game's overrides exactly what the user typed."""
        wanted = parse_override_text(text)
        bottle, key = self._bottle(game_path), self._key(executable_path)
        current = bottle.read(key)
        for dll in sorted(current.keys() - wanted.keys()):
            bottle.write(key, dll, None)
        for dll, value in sorted(wanted.items()):
            if current.get(dll) != value:
                bottle.write(key, dll, value)
        return describe(wanted)

    def read(self, game_path: str, executable_path: str) -> str:
        return describe(self._bottle(game_path).read(self._key(executable_path)))

    def prepare(self, game_path: str, executable_path: str, groups: list[str]) -> dict[str, object]:
        current = self._bottle(game_path).read(self._key(executable_path))
        return {"currentOptions": describe(current), "proposedOptions": describe({**current, **parse_overrides(groups)})}

    def apply(self, record_key: str, game_path: str, executable_path: str, groups: list[str]) -> dict[str, str]:
        bottle, key = self._bottle(game_path), self._key(executable_path)
        current = bottle.read(key)
        records = self._load()
        record = records.get(record_key) or {"bottle": str(bottle.prefix), "key": key, "previous": {}, "added": {}}
        updated = dict(current)
        for dll, value in parse_overrides(groups).items():
            if current.get(dll) == value:
                continue
            record["previous"].setdefault(dll, current.get(dll))
            record["added"][dll] = value
            # Recorded first: a crash after the write still lets Remove undo it.
            records[record_key] = record
            self._save(records)
            bottle.write(key, dll, value)
            updated[dll] = value
        return {"currentOptions": describe(current), "updatedOptions": describe(updated)}

    def remove(self, record_key: str, game_path: str, executable_path: str) -> dict[str, object]:
        bottle, key = self._bottle(game_path), self._key(executable_path)
        current = bottle.read(key)
        records = self._load()
        record = records.get(record_key)
        if record is None:
            text = describe(current)
            return {"hadRecord": False, "changed": False, "currentOptions": text, "updatedOptions": text}
        updated = dict(current)
        for dll, value in record["added"].items():
            if current.get(dll) != value:
                continue  # Changed by the user since; theirs to keep.
            previous = record["previous"].get(dll)
            bottle.write(record.get("key", key), dll, previous)
            if previous is None:
                updated.pop(dll, None)
            else:
                updated[dll] = previous
        del records[record_key]
        self._save(records)
        return {
            "hadRecord": True, "changed": updated != current,
            "currentOptions": describe(current), "updatedOptions": describe(updated),
        }
