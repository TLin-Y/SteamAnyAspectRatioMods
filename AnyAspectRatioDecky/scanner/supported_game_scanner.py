import os
from dataclasses import asdict, dataclass, field, replace
from pathlib import Path

from installer import launcher
from scanner.local_archives import build_local_mods, scan_local_archives
from scanner.mod_catalog import ModDefinition, load_mod_catalog
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
        data = asdict(self)
        data["kind"] = data.pop("source_kind")
        return data

    @classmethod
    def from_dict(cls, data: dict[str, object]) -> "SupportedGame":
        """Inverse of to_dict; raises TypeError/KeyError for data in another shape."""
        fields = dict(data)
        fields["source_kind"] = fields.pop("kind")
        return cls(**fields)


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
            "steam_scan": {
                "libraries_scanned": self.steam_scan.libraries_scanned,
                "apps_scanned": self.steam_scan.apps_scanned,
            },
            "catalogued_mods": self.catalogued_mods,
            "local_archive_count": self.local_mods,
            "supported_games": [
                game.to_dict() for game in self.supported_games
            ],
            "warnings": self.warnings,
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
        replacement = mod.launcher_replacement
        launcher_backup_present = (
            game_path is not None and _is_file(game_path / replacement.backup, file_cache)
        )
        if game_path is not None and launcher.setup_complete(mod, game_path):
            present_files.append(replacement.target)
        else:
            missing_files.append(replacement.target)
    if not present_files and not launcher_backup_present:
        file_status = "not_detected"
    elif missing_files:
        file_status = "partial"
    else:
        file_status = "complete"

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


@dataclass
class _ExecutableIndex:
    owners: dict[str, set[str]] = field(default_factory=dict)
    by_name: dict[str, list[tuple[ModDefinition, str]]] = field(default_factory=dict)
    by_root: dict[str, list[tuple[ModDefinition, str]]] = field(default_factory=dict)

    @classmethod
    def build(cls, catalog: tuple[ModDefinition, ...]) -> "_ExecutableIndex":
        index = cls()
        for mod in catalog:
            for executable in _executable_paths(mod):
                path = Path(executable)
                name = path.name.casefold()
                index.owners.setdefault(name, set()).add(mod.mod_id)
                index.by_name.setdefault(name, []).append((mod, executable))
                index.by_root.setdefault(os.path.normcase(path.parts[0]), []).append((mod, executable))
        return index


# Fallback matching, by .exe name
def _match_by_executable(
    app: InstalledApp,
    catalog: tuple[ModDefinition, ...],
    index: _ExecutableIndex,
    file_cache: dict[Path, bool],
    warnings: list[str],
    allowed: set[str] | None = None,
) -> list[tuple[ModDefinition, bool]]:
    if app.game_path is None:
        return []
    
    root = Path(app.game_path)
    try:
        with os.scandir(root) as entries:
            root_entries = {
                os.path.normcase(entry.name) for entry in entries
                if os.path.normcase(entry.name) in index.by_root
            }
    except OSError as error:
        warnings.append(f"Could not scan game directory {root}: {error}")
        return []
    
    probes: dict[Path, bool] = {}
    matched_ids: set[str] = set()
    matching_names: set[str] = set()
    for root_name in root_entries:
        for mod, path in index.by_root[root_name]:
            if _is_file(root / path, probes):
                matched_ids.add(mod.mod_id)
                matching_names.add(Path(path).name.casefold())
    if not matched_ids:
        return []
    
    file_cache.update((path, True) for path, exists in probes.items() if exists)
    ambiguous = len(matched_ids) > 1 or any(len(index.owners[name]) > 1 for name in matching_names)
    
    return [
        (mod, ambiguous) for mod in catalog
        if mod.mod_id in matched_ids and (allowed is None or mod.mod_id in allowed)
    ]


def _shortcut_roots(
    executable: Path, candidates: list[tuple[ModDefinition, str]], file_cache: dict[Path, bool],
) -> list[tuple[ModDefinition, Path]]:
    roots = []
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
        roots.append((mod, root))
    return roots


def _layout_warnings(
    mod: ModDefinition, app: InstalledApp, game: SupportedGame, directory_cache: dict[Path, bool],
) -> list[str]:
    if app.game_path is None:
        return []
    
    warnings = []
    if _executable_paths(mod) and not game.executable_found:
        expected = ", ".join(_executable_paths(mod))
        warnings.append(
            f"Mod {mod.mod_id} for app {app.app_id}: no catalogued executable found in "
            f"{app.game_path}; expected {expected}. The game layout or catalog may have changed."
        )

    if mod.source.destination and not mod.manual:
        destination = Path(app.game_path) / mod.source.destination
        if destination not in directory_cache:
            directory_cache[destination] = destination.is_dir()
        if not directory_cache[destination]:
            warnings.append(
                f"Mod {mod.mod_id} for app {app.app_id}: catalog destination {destination} "
                "does not exist. Applying the mod may create this directory; verify the game layout first."
            )
    return warnings


@dataclass
class _GameMatcher:
    """Collects the supported games of single iteration. (light weight)"""

    catalog: tuple[ModDefinition, ...]
    index: _ExecutableIndex
    warnings: list[str]
    games: list[SupportedGame] = field(default_factory=list)
    file_cache: dict[Path, bool] = field(default_factory=dict)
    directory_cache: dict[Path, bool] = field(default_factory=dict)
    known: set[tuple[str | None, str]] = field(default_factory=set)

    def add_game(self, mod: ModDefinition, app: InstalledApp, by_executable: bool, ambiguity_group: str | None) -> None:
        game = _build_game(mod, app, self.file_cache)
        if by_executable or ambiguity_group is not None:
            game = replace(game, matched_by_executable=by_executable, ambiguity_group=ambiguity_group)
        self.games.append(game)
        self.warnings.extend(_layout_warnings(mod, app, game, self.directory_cache))
        self.known.add((app.game_path, mod.mod_id))

    def match_app(self, app: InstalledApp, app_mods: list[ModDefinition]) -> None:
        """Match by install folder; fall back to the executable when the folder differs or no mod names the app."""
        matched = [(mod, False, False) for mod in app_mods if app.install_dir == mod.install_dir]
        mismatched = [mod for mod in app_mods if app.install_dir != mod.install_dir]
        for mod in mismatched:
            self.warnings.append(
                f"Installed app {app.app_id} directory {app.install_dir} "
                f"does not match catalog directory {mod.install_dir}"
            )
        if mismatched or not app_mods:
            allowed = {mod.mod_id for mod in mismatched} if mismatched else None
            found = _match_by_executable(app, self.catalog, self.index, self.file_cache, self.warnings, allowed)
            matched += [(mod, True, ambiguous) for mod, ambiguous in found]

        for mod, by_executable, ambiguous in matched:
            self.add_game(mod, app, by_executable, f"{app.app_id}:{app.game_path}" if ambiguous else None)

    def match_shortcut(self, shortcut: InstalledApp) -> None:
        """Match a non-Steam shortcut whose executable is a catalogued one; skip games already found."""
        if not shortcut.shortcut_executable:
            return
        executable = Path(shortcut.shortcut_executable)
        name = executable.name.casefold()
        candidates = self.index.by_name.get(name, [])
        if not candidates or not executable.is_file():
            return

        roots = _shortcut_roots(executable, candidates, self.file_cache)
        ambiguous = len(self.index.owners[name]) > 1 or len(roots) > 1
        for mod, root in roots:
            game = replace(shortcut, game_path=str(root))
            if (game.game_path, mod.mod_id) not in self.known:
                self.add_game(mod, game, True, f"{shortcut.app_id}:{executable}" if ambiguous else None)


def match_supported_games(
    steam_scan: SteamScanReport,
    catalog: tuple[ModDefinition, ...],
    local_mods: tuple[ModDefinition, ...] = (),
) -> SupportedGameScanReport:
    mods_by_app_id: dict[str, list[ModDefinition]] = {}
    for mod in (*local_mods, *catalog):
        mods_by_app_id.setdefault(mod.steam_app_id, []).append(mod)

    matcher = _GameMatcher(catalog, _ExecutableIndex.build(catalog), list(steam_scan.warnings))
    for app in steam_scan.apps:
        matcher.match_app(app, mods_by_app_id.get(app.app_id, []))
    for shortcut in steam_scan.shortcuts:
        matcher.match_shortcut(shortcut)

    return SupportedGameScanReport(
        steam_scan=steam_scan,
        catalogued_mods=len(catalog),
        supported_games=matcher.games,
        warnings=matcher.warnings,
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
