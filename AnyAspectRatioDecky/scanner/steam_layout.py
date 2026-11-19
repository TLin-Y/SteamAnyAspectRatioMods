import os
import re
import sys
from pathlib import Path

from helper.validator import Validator

ROOTS = (
    ".local/share/Steam",  # SteamOS and native Linux
    ".steam/steam",  # legacy symlink to the root
    ".var/app/com.valvesoftware.Steam/.local/share/Steam",  # Flatpak
    "Steam",
    "Library/Application Support/Steam",  # macOS
)

# macOS runs Windows games through Windows Steam inside a Wine bottle.
_MACOS_BOTTLES = (
    "Library/Application Support/CrossOver/Bottles",
    "Library/Containers/com.isaacmarovitz.Whisky/Bottles",
)
_BOTTLE_STEAM_DIRS = ("drive_c/Program Files (x86)/Steam", "drive_c/Program Files/Steam")
_WINDOWS_DRIVE_PATH = re.compile(r"^([A-Za-z]):[\\/]*(.*)$")

ROOT_ENVIRONMENT = "STEAM_COMPAT_CLIENT_INSTALL_PATH"

_WINDOWS_REGISTRY_ROOTS = (
    ("HKEY_CURRENT_USER", r"Software\Valve\Steam", "SteamPath"),
    ("HKEY_LOCAL_MACHINE", r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
    ("HKEY_LOCAL_MACHINE", r"SOFTWARE\Valve\Steam", "InstallPath"),
)


def system_roots(platform: str = sys.platform, home: Path | None = None) -> list[Path]:
    """Steam roots outside the fixed home-relative ROOTS: the Windows registry, or Steam in macOS Wine bottles."""
    if platform == "darwin":
        return bottle_roots(Path.home() if home is None else home)
    if platform != "win32":
        return []
    return _windows_roots()


def bottle_roots(home: Path) -> list[Path]:
    return [
        steam
        for bottles in _MACOS_BOTTLES
        for bottle in sorted((home / bottles).glob("*"))
        for steam in (bottle / relative for relative in _BOTTLE_STEAM_DIRS)
        if steam.is_dir()
    ]


def wine_prefix(path: Path) -> Path | None:
    """The Wine prefix (the folder holding drive_c) that contains path, if any."""
    return next((folder.parent for folder in (path, *path.parents) if folder.name.lower() == "drive_c"), None)


def wine_drive_path(raw_path: str, steam_root: Path, platform: str = sys.platform) -> Path | None:
    """A Windows library path such as D:\\Games, read by a Steam that runs inside a Wine prefix, mapped to the host.

    Wine exposes drive letters as dosdevices/<letter>: links in the prefix that holds drive_c.
    """
    match = _WINDOWS_DRIVE_PATH.match(raw_path)
    if platform == "win32" or not match:
        return None
    prefix = wine_prefix(steam_root)
    if prefix is None:
        return None
    letter = match[1].lower()
    drive = Path(f"{prefix}/dosdevices/{letter}:")  # joined as text: a Windows test host reads "x:" as a drive
    if letter == "c" and not drive.exists():
        drive = prefix / "drive_c"
    parts = [part for part in re.split(r"[\\/]+", match[2]) if part]
    return drive.joinpath(*parts)


def _windows_roots() -> list[Path]:
    import winreg

    roots: list[Path] = []
    for hive, key, value in _WINDOWS_REGISTRY_ROOTS:
        try:
            with winreg.OpenKey(getattr(winreg, hive), key) as handle:
                path, _ = winreg.QueryValueEx(handle, value)
        except OSError:
            continue
        if isinstance(path, str) and path:
            roots.append(Path(path))
    program_files = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if program_files:
        roots.append(Path(program_files) / "Steam")
    return roots

_MANIFEST_PREFIX, _MANIFEST_SUFFIX = "appmanifest_", ".acf"


def apps_dir(library: Path) -> Path:
    return library / "steamapps"


def games_dir(library: Path) -> Path:
    return apps_dir(library) / "common"


def library_config(root: Path) -> Path:
    """Lists every library folder of this Steam install (the root itself is always one)."""
    return apps_dir(root) / "libraryfolders.vdf"


def manifests(library: Path) -> list[Path]:
    return sorted(apps_dir(library).glob(f"{_MANIFEST_PREFIX}*{_MANIFEST_SUFFIX}"))


def manifest_app_id(manifest: Path) -> str | None:
    name = manifest.name
    app_id = name.removeprefix(_MANIFEST_PREFIX).removesuffix(_MANIFEST_SUFFIX)
    if name != f"{_MANIFEST_PREFIX}{app_id}{_MANIFEST_SUFFIX}" or not Validator.Catalog.steam_app_id(app_id):
        return None
    return app_id


def shortcut_files(root: Path) -> list[Path]:
    return sorted((root / "userdata").glob("*/config/shortcuts.vdf"))
