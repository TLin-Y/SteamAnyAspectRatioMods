import os
import re
from collections.abc import Mapping, Sequence
from dataclasses import asdict, dataclass, field
from pathlib import Path

from utils.steam_vdf import VdfParseError, read_vdf

_MANIFEST_PATTERN = re.compile(r"appmanifest_(\d+)\.acf\Z")
_STEAM_ROOT_PATHS = (".local/share/Steam", ".steam/steam", "Steam")


def _add_unique_path(paths: list[Path], seen: set[str], path: Path) -> None:
    key = os.path.normcase(str(path))
    if key not in seen:
        paths.append(path)
        seen.add(key)


@dataclass(frozen=True)
class InstalledApp:
    app_id: str
    name: str
    install_dir: str
    library_path: str
    game_path: str | None
    shortcut_executable: str | None = None


@dataclass
class SteamScanReport:
    libraries_scanned: int
    apps_scanned: int
    apps: list[InstalledApp]
    warnings: list[str]
    shortcuts: list[InstalledApp] = field(default_factory=list)

    def to_dict(self) -> dict[str, object]:
        return {
            "libraries_scanned": self.libraries_scanned,
            "apps_scanned": self.apps_scanned,
            "apps": [asdict(app) for app in self.apps],
            "warnings": self.warnings,
        }


def find_steam_roots(
    home: Path | None = None,
    environment: Mapping[str, str] | None = None,
) -> list[Path]:
    env = os.environ if environment is None else environment
    user_home = Path.home() if home is None else home
    candidates: list[Path] = []
    configured_root = env.get("STEAM_COMPAT_CLIENT_INSTALL_PATH")
    if configured_root:
        candidates.append(Path(configured_root).expanduser())
    candidates.extend(user_home / relative_path for relative_path in _STEAM_ROOT_PATHS)

    roots: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        resolved = candidate.resolve(strict=False)
        if resolved.is_dir():
            _add_unique_path(roots, seen, resolved)
    return roots


def _library_roots(steam_root: Path) -> tuple[list[Path], list[str]]:
    roots = [steam_root.resolve(strict=False)]
    warnings: list[str] = []
    config_path = steam_root / "steamapps" / "libraryfolders.vdf"
    if not config_path.exists():
        return roots, [f"Library configuration not found: {config_path}"]

    try:
        document = read_vdf(config_path)
    except (OSError, UnicodeError, VdfParseError) as error:
        return roots, [
            f"Could not read library configuration {config_path}: {error}"
        ]

    library_folders = document.get("libraryfolders")
    if not isinstance(library_folders, dict):
        return roots, [
            f"Library configuration has no libraryfolders object: {config_path}"
        ]

    seen: set[str] = set()
    _add_unique_path(roots, seen, roots[0])
    for index, value in library_folders.items():
        if not index.isdigit():
            continue
        raw_path = value.get("path") if isinstance(value, dict) else value
        if not isinstance(raw_path, str) or not raw_path:
            warnings.append(
                f"Library {index} has no valid path in {config_path}"
            )
            continue
        library_path = Path(raw_path).expanduser()
        if not library_path.is_absolute():
            warnings.append(
                f"Library {index} path is not absolute in {config_path}"
            )
            continue
        resolved = library_path.resolve(strict=False)
        _add_unique_path(roots, seen, resolved)
    return roots, warnings


def _safe_install_directory(value: str) -> bool:
    relative = Path(value)
    return bool(value) and not relative.is_absolute() and all(
        part not in ("", ".", "..") for part in value.replace("\\", "/").split("/")
    )


def _directory_size(path: str | None) -> int:
    if path is None:
        return -1
    total = 0
    for current, _, filenames in os.walk(path):
        for filename in filenames:
            try:
                total += os.lstat(os.path.join(current, filename)).st_size
            except OSError:
                continue
    return total


def _keep_largest_installs(apps: list[InstalledApp]) -> list[InstalledApp]:
    """A game moved between libraries can leave a stale copy behind; keep the largest folder per app."""
    by_app_id: dict[str, list[InstalledApp]] = {}
    for app in apps:
        by_app_id.setdefault(app.app_id, []).append(app)
    winners: dict[str, InstalledApp] = {}
    for app_id, group in by_app_id.items():
        winners[app_id] = (
            group[0]
            if len(group) == 1
            else max(group, key=lambda app: _directory_size(app.game_path))
        )
    return [app for app in apps if winners[app.app_id] is app]


def _read_manifest(
    manifest_path: Path, library: Path, common_dir: Path, warnings: list[str],
) -> InstalledApp | None:
    """Parse appmanifest_<id>.acf; an unusable manifest adds a warning and returns None."""
    filename_match = _MANIFEST_PATTERN.fullmatch(manifest_path.name)
    if filename_match is None:
        return None
    try:
        document = read_vdf(manifest_path)
    except (OSError, UnicodeError, VdfParseError) as error:
        warnings.append(f"Could not read app manifest {manifest_path}: {error}")
        return None
    app_state = document.get("AppState")
    if not isinstance(app_state, dict):
        warnings.append(f"App manifest has no AppState object: {manifest_path}")
        return None
    app_id, name, install_dir = (app_state.get(key) for key in ("appid", "name", "installdir"))
    if not all(isinstance(value, str) and value for value in (app_id, name, install_dir)):
        warnings.append(f"App manifest is missing appid, name, or installdir: {manifest_path}")
        return None
    if app_id != filename_match.group(1):
        warnings.append(f"App ID does not match manifest filename: {manifest_path}")
        return None
    if not _safe_install_directory(install_dir):
        warnings.append(f"Unsafe install directory in app manifest: {manifest_path}")
        return None
    candidate_path = (common_dir / install_dir).resolve(strict=False)
    try:
        candidate_path.relative_to(common_dir)
    except ValueError:
        warnings.append(f"Install directory escapes the Steam common folder: {manifest_path}")
        return None
    game_path = str(candidate_path) if candidate_path.is_dir() else None
    if game_path is None:
        warnings.append(f"Installed game directory is unavailable: {candidate_path}")
    return InstalledApp(
        app_id=app_id, name=name, install_dir=install_dir, library_path=str(library), game_path=game_path,
    )


def scan_steam_libraries(steam_roots: Sequence[Path]) -> SteamScanReport:
    libraries: list[Path] = []
    warnings: list[str] = []
    seen_libraries: set[str] = set()
    for steam_root in steam_roots:
        found, root_warnings = _library_roots(steam_root)
        warnings.extend(root_warnings)
        for library in found:
            _add_unique_path(libraries, seen_libraries, library)

    apps: list[InstalledApp] = []
    for library in libraries:
        if not library.is_dir():
            warnings.append(f"Steam library is unavailable: {library}")
            continue
        steamapps = library / "steamapps"
        if not steamapps.is_dir():
            warnings.append(f"Steam library has no steamapps directory: {library}")
            continue

        common_dir = (steamapps / "common").resolve(strict=False)
        try:
            manifests = sorted(steamapps.glob("appmanifest_*.acf"))
        except OSError as error:
            warnings.append(
                f"Could not enumerate manifests in {steamapps}: {error}"
            )
            continue

        for manifest_path in manifests:
            app = _read_manifest(manifest_path, library, common_dir, warnings)
            if app is not None:
                apps.append(app)

    apps = _keep_largest_installs(apps)
    return SteamScanReport(
        libraries_scanned=len(libraries),
        apps_scanned=len(apps),
        apps=apps,
        warnings=warnings,
    )


def discover_steam_apps() -> SteamScanReport:
    roots = find_steam_roots()
    if not roots:
        return SteamScanReport(
            libraries_scanned=0,
            apps_scanned=0,
            apps=[],
            warnings=["No Steam installation was found in the current user's standard locations."],
        )
    return scan_steam_libraries(roots)
