import os
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Mapping, Sequence

from helper.constants import (
    SCAN_REPORT_APP_LIST_KEY,
    SCAN_REPORT_APPS_KEY,
    SCAN_REPORT_LIBRARIES_KEY,
    SCAN_REPORT_WARNINGS_KEY,
    STEAM_APP_ID_KEY,
    STEAM_APP_INSTALL_DIRECTORY_KEY,
    STEAM_APP_MANIFEST_GLOB,
    STEAM_APP_MANIFEST_PATTERN,
    STEAM_APP_NAME_KEY,
    STEAM_APP_STATE_KEY,
    STEAMAPPS_DIRECTORY,
    STEAM_COMMON_DIRECTORY,
    STEAM_GAME_DIRECTORY_UNAVAILABLE_WARNING,
    STEAM_LIBRARY_CONFIG_FILE,
    STEAM_LIBRARY_CONFIG_MISSING_OBJECT_WARNING,
    STEAM_LIBRARY_CONFIG_NOT_FOUND_WARNING,
    STEAM_LIBRARY_CONFIG_READ_WARNING,
    STEAM_LIBRARY_NO_STEAMAPPS_WARNING,
    STEAM_LIBRARY_PATH_MISSING_WARNING,
    STEAM_LIBRARY_PATH_NOT_ABSOLUTE_WARNING,
    STEAM_LIBRARY_UNAVAILABLE_WARNING,
    STEAM_MANIFEST_APP_ID_MISMATCH_WARNING,
    STEAM_MANIFEST_APP_STATE_MISSING_WARNING,
    STEAM_MANIFEST_FIELDS_MISSING_WARNING,
    STEAM_MANIFEST_INSTALL_DIRECTORY_ESCAPE_WARNING,
    STEAM_MANIFEST_INSTALL_DIRECTORY_UNSAFE_WARNING,
    STEAM_MANIFEST_READ_WARNING,
    STEAM_MANIFEST_ENUMERATION_WARNING,
    STEAM_NOT_FOUND_WARNING,
    STEAM_ROOT_ENVIRONMENT_VARIABLE,
    STEAM_ROOT_RELATIVE_PATHS,
    VDF_PATH_KEY,
    STEAM_LIBRARY_FOLDERS_KEY,
)
from utils.steam_vdf import VdfParseError, read_vdf


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


@dataclass
class SteamScanReport:
    libraries_scanned: int
    apps_scanned: int
    apps: list[InstalledApp]
    warnings: list[str]
    shortcuts: list[InstalledApp] = field(default_factory=list)

    def to_dict(self) -> dict[str, object]:
        return {
            SCAN_REPORT_LIBRARIES_KEY: self.libraries_scanned,
            SCAN_REPORT_APPS_KEY: self.apps_scanned,
            SCAN_REPORT_APP_LIST_KEY: [asdict(app) for app in self.apps],
            SCAN_REPORT_WARNINGS_KEY: self.warnings,
        }


def find_steam_roots(
    home: Path | None = None,
    environment: Mapping[str, str] | None = None,
) -> list[Path]:
    env = os.environ if environment is None else environment
    user_home = Path.home() if home is None else home
    candidates: list[Path] = []
    configured_root = env.get(STEAM_ROOT_ENVIRONMENT_VARIABLE)
    if configured_root:
        candidates.append(Path(configured_root).expanduser())
    candidates.extend(user_home / relative_path for relative_path in STEAM_ROOT_RELATIVE_PATHS)

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
    config_path = steam_root / STEAMAPPS_DIRECTORY / STEAM_LIBRARY_CONFIG_FILE
    if not config_path.exists():
        return roots, [STEAM_LIBRARY_CONFIG_NOT_FOUND_WARNING.format(path=config_path)]

    try:
        document = read_vdf(config_path)
    except (OSError, UnicodeError, VdfParseError) as error:
        return roots, [
            STEAM_LIBRARY_CONFIG_READ_WARNING.format(path=config_path, error=error)
        ]

    library_folders = document.get(STEAM_LIBRARY_FOLDERS_KEY)
    if not isinstance(library_folders, dict):
        return roots, [
            STEAM_LIBRARY_CONFIG_MISSING_OBJECT_WARNING.format(path=config_path)
        ]

    seen: set[str] = set()
    _add_unique_path(roots, seen, roots[0])
    for index, value in library_folders.items():
        if not index.isdigit():
            continue
        raw_path = value.get(VDF_PATH_KEY) if isinstance(value, dict) else value
        if not isinstance(raw_path, str) or not raw_path:
            warnings.append(
                STEAM_LIBRARY_PATH_MISSING_WARNING.format(index=index, path=config_path)
            )
            continue
        library_path = Path(raw_path).expanduser()
        if not library_path.is_absolute():
            warnings.append(
                STEAM_LIBRARY_PATH_NOT_ABSOLUTE_WARNING.format(index=index, path=config_path)
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
            warnings.append(STEAM_LIBRARY_UNAVAILABLE_WARNING.format(path=library))
            continue
        steamapps = library / STEAMAPPS_DIRECTORY
        if not steamapps.is_dir():
            warnings.append(STEAM_LIBRARY_NO_STEAMAPPS_WARNING.format(path=library))
            continue

        common_dir = (steamapps / STEAM_COMMON_DIRECTORY).resolve(strict=False)
        try:
            manifests = sorted(steamapps.glob(STEAM_APP_MANIFEST_GLOB))
        except OSError as error:
            warnings.append(
                STEAM_MANIFEST_ENUMERATION_WARNING.format(path=steamapps, error=error)
            )
            continue

        for manifest_path in manifests:
            filename_match = STEAM_APP_MANIFEST_PATTERN.fullmatch(manifest_path.name)
            if filename_match is None:
                continue
            try:
                document = read_vdf(manifest_path)
            except (OSError, UnicodeError, VdfParseError) as error:
                warnings.append(STEAM_MANIFEST_READ_WARNING.format(path=manifest_path, error=error))
                continue

            app_state = document.get(STEAM_APP_STATE_KEY)
            if not isinstance(app_state, dict):
                warnings.append(STEAM_MANIFEST_APP_STATE_MISSING_WARNING.format(path=manifest_path))
                continue
            app_id = app_state.get(STEAM_APP_ID_KEY)
            name = app_state.get(STEAM_APP_NAME_KEY)
            install_dir = app_state.get(STEAM_APP_INSTALL_DIRECTORY_KEY)
            if not all(isinstance(value, str) and value for value in (app_id, name, install_dir)):
                warnings.append(STEAM_MANIFEST_FIELDS_MISSING_WARNING.format(path=manifest_path))
                continue
            if app_id != filename_match.group(1):
                warnings.append(STEAM_MANIFEST_APP_ID_MISMATCH_WARNING.format(path=manifest_path))
                continue
            if not _safe_install_directory(install_dir):
                warnings.append(
                    STEAM_MANIFEST_INSTALL_DIRECTORY_UNSAFE_WARNING.format(path=manifest_path)
                )
                continue

            candidate_path = (common_dir / install_dir).resolve(strict=False)
            try:
                candidate_path.relative_to(common_dir)
            except ValueError:
                warnings.append(
                    STEAM_MANIFEST_INSTALL_DIRECTORY_ESCAPE_WARNING.format(path=manifest_path)
                )
                continue
            game_path = str(candidate_path) if candidate_path.is_dir() else None
            if game_path is None:
                warnings.append(
                    STEAM_GAME_DIRECTORY_UNAVAILABLE_WARNING.format(path=candidate_path)
                )

            apps.append(
                InstalledApp(
                    app_id=app_id,
                    name=name,
                    install_dir=install_dir,
                    library_path=str(library),
                    game_path=game_path,
                )
            )

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
            warnings=[STEAM_NOT_FOUND_WARNING],
        )
    return scan_steam_libraries(roots)
