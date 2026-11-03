from dataclasses import dataclass, field
from pathlib import Path

from helper.constants import (
    MOD_CATALOG_EXECUTABLE_FOUND_FIELD,
    MOD_CATALOG_APP_NAME_FIELD,
    MOD_CATALOG_FILE_STATUS_FIELD,
    MOD_CATALOG_FILES_MISSING_FIELD,
    MOD_CATALOG_FILES_PRESENT_FIELD,
    MOD_CATALOG_FILE_STATUS_COMPLETE,
    MOD_CATALOG_FILE_STATUS_NONE,
    MOD_CATALOG_FILE_STATUS_PARTIAL,
    MOD_CATALOG_INI_PATH_FIELD,
    MOD_CATALOG_INI_FILE_FIELD,
    MOD_CATALOG_MOD_FILES_FIELD,
    MOD_CATALOG_GAME_PATH_FIELD,
    MOD_CATALOG_INSTALL_DIRECTORY_MISMATCH_WARNING,
    MOD_CATALOG_MOD_ID_FIELD,
    MOD_CATALOG_MOD_NAME_FIELD,
    MOD_CATALOG_SOURCE_KIND_FIELD,
    MOD_WINE_OVERRIDES_KEY,
    MOD_CATALOG_COUNT_FIELD,
    MOD_CATALOG_SCAN_REPORT_FIELD,
    MOD_CATALOG_GAMES_FIELD,
    MOD_CATALOG_EXECUTABLE_PATH_FIELD,
    MOD_STEAM_APP_ID_KEY,
    SCAN_REPORT_APPS_KEY,
    SCAN_REPORT_LIBRARIES_KEY,
    SCAN_REPORT_WARNINGS_KEY,
)
from scanner.local_archives import build_local_mods, scan_local_archives
from scanner.mod_catalog import ModDefinition, load_mod_catalog
from scanner.steam_library_scanner import SteamScanReport, discover_steam_apps


@dataclass(frozen=True)
class SupportedGame:
    mod_id: str
    mod_name: str
    steam_app_id: str
    app_name: str
    game_path: str | None
    executable_path: str
    executable_found: bool
    file_status: str
    files_present: list[str]
    files_missing: list[str]
    ini_path: str | None
    ini_file: str | None
    mod_files: list[str]
    source_kind: str
    wine_dll_overrides: list[str]

    def to_dict(self) -> dict[str, object]:
        return {
            MOD_CATALOG_MOD_ID_FIELD: self.mod_id,
            MOD_CATALOG_MOD_NAME_FIELD: self.mod_name,
            MOD_STEAM_APP_ID_KEY: self.steam_app_id,
            MOD_CATALOG_APP_NAME_FIELD: self.app_name,
            MOD_CATALOG_GAME_PATH_FIELD: self.game_path,
            MOD_CATALOG_EXECUTABLE_PATH_FIELD: self.executable_path,
            MOD_CATALOG_EXECUTABLE_FOUND_FIELD: self.executable_found,
            MOD_CATALOG_FILE_STATUS_FIELD: self.file_status,
            MOD_CATALOG_FILES_PRESENT_FIELD: self.files_present,
            MOD_CATALOG_FILES_MISSING_FIELD: self.files_missing,
            MOD_CATALOG_INI_PATH_FIELD: self.ini_path,
            MOD_CATALOG_INI_FILE_FIELD: self.ini_file,
            MOD_CATALOG_MOD_FILES_FIELD: self.mod_files,
            MOD_CATALOG_SOURCE_KIND_FIELD: self.source_kind,
            MOD_WINE_OVERRIDES_KEY: self.wine_dll_overrides,
        }


@dataclass
class SupportedGameScanReport:
    steam_scan: SteamScanReport
    catalogued_mods: int
    supported_games: list[SupportedGame]
    warnings: list[str]
    local_mods: int = 0
    mods: dict[str, ModDefinition] = field(default_factory=dict)

    def to_dict(self) -> dict[str, object]:
        return {
            MOD_CATALOG_SCAN_REPORT_FIELD: {
                SCAN_REPORT_LIBRARIES_KEY: self.steam_scan.libraries_scanned,
                SCAN_REPORT_APPS_KEY: self.steam_scan.apps_scanned,
            },
            MOD_CATALOG_COUNT_FIELD: self.catalogued_mods,
            "local_archive_count": self.local_mods,
            MOD_CATALOG_GAMES_FIELD: [
                game.to_dict() for game in self.supported_games
            ],
            SCAN_REPORT_WARNINGS_KEY: self.warnings,
        }


def match_supported_games(
    steam_scan: SteamScanReport,
    catalog: tuple[ModDefinition, ...],
    local_mods: tuple[ModDefinition, ...] = (),
) -> SupportedGameScanReport:
    mods_by_app_id = {mod.steam_app_id: mod for mod in (*local_mods, *catalog)}
    games: list[SupportedGame] = []
    warnings = list(steam_scan.warnings)

    for app in steam_scan.apps:
        mod = mods_by_app_id.get(app.app_id)
        if mod is None:
            continue
        if app.install_dir != mod.install_dir:
            warnings.append(
                MOD_CATALOG_INSTALL_DIRECTORY_MISMATCH_WARNING.format(
                    app_id=app.app_id,
                    actual=app.install_dir,
                    expected=mod.install_dir,
                )
            )
            continue

        game_path = Path(app.game_path) if app.game_path is not None else None
        executable_path = (
            game_path / mod.executable if game_path is not None else None
        )
        executable_found = executable_path is not None and executable_path.is_file()
        present_files = [
            path
            for path in mod.files
            if game_path is not None and (game_path / path).is_file()
        ]
        missing_files = [path for path in mod.files if path not in present_files]
        if not present_files:
            file_status = MOD_CATALOG_FILE_STATUS_NONE
        elif missing_files:
            file_status = MOD_CATALOG_FILE_STATUS_PARTIAL
        else:
            file_status = MOD_CATALOG_FILE_STATUS_COMPLETE

        ini_path = (
            str(game_path / mod.ini_file)
            if game_path is not None
            and mod.ini_file is not None
            and (game_path / mod.ini_file).is_file()
            else None
        )
        games.append(
            SupportedGame(
                mod_id=mod.mod_id,
                mod_name=mod.name,
                steam_app_id=app.app_id,
                app_name=app.name,
                game_path=app.game_path,
                executable_path=str(executable_path) if executable_path is not None else "",
                executable_found=executable_found,
                file_status=file_status,
                files_present=present_files,
                files_missing=missing_files,
                ini_path=ini_path,
                ini_file=mod.ini_file,
                mod_files=list(mod.files),
                source_kind=mod.source.kind,
                wine_dll_overrides=list(mod.wine_dll_overrides),
            )
        )

    return SupportedGameScanReport(
        steam_scan=steam_scan,
        catalogued_mods=len(catalog),
        supported_games=games,
        warnings=warnings,
        local_mods=len(local_mods),
        mods={mod.mod_id: mod for mod in mods_by_app_id.values()},
    )


def scan_supported_games(
    catalog: tuple[ModDefinition, ...] | None = None,
    *,
    local_archive_dir: str | None = None,
    local_index_path: Path | None = None,
) -> SupportedGameScanReport:
    steam_scan = discover_steam_apps()
    catalog = load_mod_catalog() if catalog is None else catalog
    archives, archive_warnings = scan_local_archives(local_archive_dir, local_index_path)
    local_mods, match_warnings = build_local_mods(
        archives,
        steam_scan.apps,
        {mod.steam_app_id for mod in catalog},
    )
    report = match_supported_games(steam_scan, catalog, local_mods)
    report.warnings.extend([*archive_warnings, *match_warnings])
    return report
