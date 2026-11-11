import os
from dataclasses import dataclass, field, replace
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
    MOD_CATALOG_DESTINATION_MISSING_WARNING,
    MOD_CATALOG_EXECUTABLE_MISSING_WARNING,
    MOD_CATALOG_INSTALL_DIRECTORY_MISMATCH_WARNING,
    MOD_CATALOG_MOD_ID_FIELD,
    MOD_CATALOG_MOD_NAME_FIELD,
    MOD_DISPLAY_TAGS_KEY,
    MOD_CATALOG_SOURCE_KIND_FIELD,
    MOD_MANUAL_KEY,
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
from scanner.mod_installer import launcher_setup_complete
from scanner.non_steam_shortcuts import scan_non_steam_shortcuts
from scanner.steam_library_scanner import (
    InstalledApp,
    SteamScanReport,
    discover_steam_apps,
    find_steam_roots,
)


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
    manual: bool = False
    matched_by_executable: bool = False
    update_available: bool = False
    ambiguity_group: str | None = None
    match_locked: bool = False
    log_files: list[str] = field(default_factory=list)
    offline_only: bool = False
    display_tags: list[str] = field(default_factory=list)
    backup_files: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, object]:
        return {
            MOD_MANUAL_KEY: self.manual,
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
            "matched_by_executable": self.matched_by_executable,
            "update_available": self.update_available,
            "ambiguity_group": self.ambiguity_group,
            "match_locked": self.match_locked,
            "log_files": self.log_files,
            "offline_only": self.offline_only,
            MOD_DISPLAY_TAGS_KEY: self.display_tags,
            "backup_files": self.backup_files,
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


def _executable_paths(mod: ModDefinition) -> tuple[str, ...]:
    return ((mod.executable,) if mod.executable else ()) + mod.executable_alternatives


def _is_file(path: Path, cache: dict[Path, bool]) -> bool:
    if path not in cache:
        cache[path] = path.is_file()
    return cache[path]


def _find_executable(
    mod: ModDefinition, game_path: Path, file_cache: dict[Path, bool]
) -> Path | None:
    return next(
        (path for name in _executable_paths(mod) if _is_file(path := game_path / name, file_cache)),
        None,
    )


def _build_game(
    mod: ModDefinition, app: InstalledApp, file_cache: dict[Path, bool]
) -> SupportedGame:
    game_path = Path(app.game_path) if app.game_path is not None else None
    found_executable = (
        _find_executable(mod, game_path, file_cache) if game_path is not None else None
    )
    executable_path = found_executable or (
        game_path / mod.executable if game_path is not None and mod.executable else None
    )
    executable_found = found_executable is not None
    backup_paths = {item.target: item.backup for item in mod.file_backups}
    present_files = [
        path
        for path in mod.files
        if game_path is not None and _is_file(game_path / path, file_cache)
        and (
            path not in backup_paths
            or _is_file(game_path / backup_paths[path], file_cache)
        )
    ]
    present_set = set(present_files)
    missing_files = [path for path in mod.files if path not in present_set]
    launcher_backup_present = False
    if mod.launcher_replacement is not None:
        launcher = mod.launcher_replacement
        launcher_backup_present = (
            game_path is not None and _is_file(game_path / launcher.backup, file_cache)
        )
        if game_path is not None and launcher_setup_complete(mod, game_path):
            present_files.append(launcher.target)
        else:
            missing_files.append(launcher.target)
    if not present_files and not launcher_backup_present:
        file_status = MOD_CATALOG_FILE_STATUS_NONE
    elif missing_files:
        file_status = MOD_CATALOG_FILE_STATUS_PARTIAL
    else:
        file_status = MOD_CATALOG_FILE_STATUS_COMPLETE

    ini_path = (
        str(game_path / mod.ini_file)
        if game_path is not None
        and mod.ini_file is not None
        and _is_file(game_path / mod.ini_file, file_cache)
        else None
    )
    return SupportedGame(
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
        mod_files=[
            *(path for path in mod.files if path not in backup_paths or path in present_set),
            *([mod.launcher_replacement.target] if mod.launcher_replacement else []),
        ],
        source_kind=mod.source.kind,
        wine_dll_overrides=list(mod.wine_dll_overrides),
        manual=mod.manual,
        offline_only=mod.offline_only,
        display_tags=list(mod.display_tags),
        backup_files=[item.target for item in mod.file_backups],
    )


def match_supported_games(
    steam_scan: SteamScanReport,
    catalog: tuple[ModDefinition, ...],
    local_mods: tuple[ModDefinition, ...] = (),
) -> SupportedGameScanReport:
    mods_by_app_id: dict[str, list[ModDefinition]] = {}
    for mod in (*local_mods, *catalog):
        mods_by_app_id.setdefault(mod.steam_app_id, []).append(mod)
    games: list[SupportedGame] = []
    warnings = list(steam_scan.warnings)
    file_cache: dict[Path, bool] = {}
    directory_cache: dict[Path, bool] = {}

    executable_owners: dict[str, set[str]] = {}
    executables_by_name: dict[str, list[tuple[ModDefinition, str]]] = {}
    executables_by_root: dict[str, list[tuple[ModDefinition, str]]] = {}
    for mod in catalog:
        for executable in _executable_paths(mod):
            path = Path(executable)
            name = path.name.casefold()
            executable_owners.setdefault(name, set()).add(mod.mod_id)
            executables_by_name.setdefault(name, []).append((mod, executable))
            executables_by_root.setdefault(os.path.normcase(path.parts[0]), []).append(
                (mod, executable)
            )

    def fallback(app: InstalledApp, allowed: set[str] | None = None) -> list[tuple[ModDefinition, bool]]:
        if app.game_path is None:
            return []
        root = Path(app.game_path)
        try:
            with os.scandir(root) as entries:
                root_entries = {
                    os.path.normcase(entry.name)
                    for entry in entries
                    if os.path.normcase(entry.name) in executables_by_root
                }
        except OSError as error:
            warnings.append(f"Could not scan game directory {root}: {error}")
            return []
        probes: dict[Path, bool] = {}
        matched_ids: set[str] = set()
        matching_names: set[str] = set()
        for root_name in root_entries:
            for mod, path in executables_by_root[root_name]:
                if _is_file(root / path, probes):
                    matched_ids.add(mod.mod_id)
                    matching_names.add(Path(path).name.casefold())
        if not matched_ids:
            return []
        file_cache.update((path, True) for path, exists in probes.items() if exists)
        ambiguous = len(matched_ids) > 1 or any(
            len(executable_owners[name]) > 1 for name in matching_names
        )
        return [
            (mod, ambiguous) for mod in catalog
            if mod.mod_id in matched_ids and (allowed is None or mod.mod_id in allowed)
        ]

    def add_game(
        mod: ModDefinition, app: InstalledApp, *, by_executable: bool = False,
        ambiguity_group: str | None = None,
    ) -> None:
        game = _build_game(mod, app, file_cache)
        if by_executable or ambiguity_group is not None:
            game = replace(game, matched_by_executable=by_executable, ambiguity_group=ambiguity_group)
        games.append(game)
        if app.game_path is None:
            return
        if _executable_paths(mod) and not game.executable_found:
            warnings.append(
                MOD_CATALOG_EXECUTABLE_MISSING_WARNING.format(
                    mod_id=mod.mod_id, app_id=app.app_id, path=app.game_path,
                    executables=", ".join(_executable_paths(mod)),
                )
            )
        if mod.source.destination and not mod.manual:
            destination = Path(app.game_path) / mod.source.destination
            if destination not in directory_cache:
                directory_cache[destination] = destination.is_dir()
            if not directory_cache[destination]:
                warnings.append(
                    MOD_CATALOG_DESTINATION_MISSING_WARNING.format(
                        mod_id=mod.mod_id, app_id=app.app_id, path=destination,
                    )
                )

    known: set[tuple[str | None, str]] = set()
    for app in steam_scan.apps:
        app_mods = mods_by_app_id.get(app.app_id, [])
        matched = [(mod, False, False) for mod in app_mods if app.install_dir == mod.install_dir]
        mismatched = [mod for mod in app_mods if app.install_dir != mod.install_dir]
        for mod in mismatched:
            warnings.append(
                MOD_CATALOG_INSTALL_DIRECTORY_MISMATCH_WARNING.format(
                    app_id=app.app_id, actual=app.install_dir, expected=mod.install_dir,
                )
            )
        if mismatched:
            matched.extend((mod, True, ambiguous) for mod, ambiguous in fallback(app, {mod.mod_id for mod in mismatched}))
        elif not app_mods:
            matched.extend((mod, True, ambiguous) for mod, ambiguous in fallback(app))
        for mod, by_executable, ambiguous in matched:
            add_game(
                mod, app, by_executable=by_executable,
                ambiguity_group=f"{app.app_id}:{app.game_path}" if ambiguous else None,
            )
            known.add((app.game_path, mod.mod_id))

    for shortcut in steam_scan.shortcuts:
        if not shortcut.shortcut_executable:
            continue
        executable = Path(shortcut.shortcut_executable)
        candidates = executables_by_name.get(executable.name.casefold(), [])
        if not candidates or not executable.is_file():
            continue
        owners = executable_owners[executable.name.casefold()]
        matched_roots: list[tuple[ModDefinition, Path]] = []
        for mod, relative in candidates:
            parts = Path(relative).parts
            if len(executable.parts) <= len(parts):
                continue
            root = executable
            for _ in parts:
                root = root.parent
            if root / relative != executable:
                continue
            if (
                mod.launcher_replacement is not None
                and relative != mod.executable
                and not _is_file(root / mod.executable, file_cache)
            ):
                continue
            matched_roots.append((mod, root))
        ambiguous = len(owners) > 1 or len(matched_roots) > 1
        for mod, root in matched_roots:
            game = replace(shortcut, game_path=str(root))
            if (game.game_path, mod.mod_id) not in known:
                add_game(
                    mod, game, by_executable=True,
                    ambiguity_group=f"{shortcut.app_id}:{executable}" if ambiguous else None,
                )
                known.add((game.game_path, mod.mod_id))

    return SupportedGameScanReport(
        steam_scan=steam_scan,
        catalogued_mods=len(catalog),
        supported_games=games,
        warnings=warnings,
        local_mods=len(local_mods),
        mods={mod.mod_id: mod for mod in (*local_mods, *catalog)},
    )


def scan_supported_games(
    catalog: tuple[ModDefinition, ...] | None = None,
    *,
    local_archive_dir: str | None = None,
    local_index_path: Path | None = None,
) -> SupportedGameScanReport:
    steam_scan = discover_steam_apps()
    steam_scan.shortcuts = scan_non_steam_shortcuts(find_steam_roots())
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
