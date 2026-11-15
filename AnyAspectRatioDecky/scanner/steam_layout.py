from pathlib import Path

from helper.validator import Validator

ROOTS = (
    ".local/share/Steam",  # SteamOS and native Linux
    ".steam/steam",  # legacy symlink to the root
    ".var/app/com.valvesoftware.Steam/.local/share/Steam",  # Flatpak
    "Steam",
)

ROOT_ENVIRONMENT = "STEAM_COMPAT_CLIENT_INSTALL_PATH"

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
