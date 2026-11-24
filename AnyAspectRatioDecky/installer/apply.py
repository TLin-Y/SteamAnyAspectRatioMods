from contextlib import ExitStack
from pathlib import Path

from helper.errors import PluginError
from installer import backups, game_folder, launcher, mod_ini, staging
from installer.launcher import Launcher
from scanner.mod_catalog import ModDefinition
from utils import files, folders


def _stage_mod(
    mod: ModDefinition,
    game_root: Path,
    targets: dict[str, Path],
    staged_files: staging.StagedFiles,
    ini_resolution: tuple[int, int] | None,
    ini_adjustment_warnings: list[str] | None,
) -> dict[Path, Path]:
    staging.stage_files(mod, staging.source_file_map(mod), targets, staged_files)
    staged_by_target = {target: staged for staged, target in staged_files}
    if ini_resolution is not None and mod.ini_file is not None and mod.ini_resolution:
        staged_ini = staged_by_target.get(folders.resolve_inside(game_root, mod.ini_file))
        if staged_ini is not None:
            mod_ini.adjust_staged(mod, staged_ini, ini_resolution, ini_adjustment_warnings)
    return staged_by_target


def _compare_existing(
    targets: dict[str, Path], staged_by_target: dict[Path, Path], file_backups: dict[Path, Path],
) -> tuple[list[str], set[str]]:
    conflicts, installed_without_backup = [], set()
    for relative_path, target in targets.items():
        if target.is_dir():
            raise PluginError(f"Mod target is a directory: {relative_path}")
        if not target.is_file():
            continue
        if not files.equal(target, staged_by_target[target]):
            conflicts.append(relative_path)
        elif target in file_backups and not file_backups[target].is_file():
            installed_without_backup.add(relative_path)
    return conflicts, installed_without_backup


def _stage_launcher(
    game_launcher: Launcher | None, staged_by_target: dict[Path, Path], staged_files: staging.StagedFiles,
) -> Path | None:
    if game_launcher is None:
        return None
    staged = launcher.stage(game_launcher, staged_by_target)
    staged_files.append((staged, game_launcher.target))
    return staged


def _replace_files(
    targets: dict[str, Path], staged_by_target: dict[Path, Path], file_backups: dict[Path, Path], skipped: set[str],
) -> None:
    with ExitStack() as undo:
        for relative_path, target in targets.items():
            if relative_path in skipped:
                continue
            backup = file_backups.get(target)
            is_new = not target.exists()
            makes_backup = backup is not None and not is_new and not backup.exists()
            files.replace_with_backup(staged_by_target[target], target, backup, "Mod replacement")
            if makes_backup:
                undo.callback(files.move, backup, target)
            elif is_new:
                undo.callback(files.delete, target)
        undo.pop_all()


def apply_mod_files(
    mod: ModDefinition,
    game_path: str,
    *,
    replace_existing_files: bool | None = None,
    ini_resolution: tuple[int, int] | None = None,
    ini_adjustment_warnings: list[str] | None = None,
) -> list[str]:
    if mod.manual:
        raise PluginError(f"Mod {mod.mod_id} must be installed manually; follow its ReadMe")
    game_root = game_folder.root(game_path)
    targets = {relative_path: folders.resolve_inside(game_root, relative_path) for relative_path in mod.files}
    file_backups = backups.resolve_files(mod, game_root)
    backups.check_before_apply(file_backups)
    game_launcher = launcher.resolve(mod, game_root)
    launcher.check_before_apply(game_launcher)

    with staging.area() as staged_files:
        staged_by_target = _stage_mod(mod, game_root, targets, staged_files, ini_resolution, ini_adjustment_warnings)
        conflicts, installed_without_backup = _compare_existing(targets, staged_by_target, file_backups)
        staged_launcher = _stage_launcher(game_launcher, staged_by_target, staged_files)
        if staged_launcher is not None and launcher.conflicts(game_launcher, staged_launcher):
            conflicts.append(game_launcher.entry.target)
        if conflicts and replace_existing_files is None:
            return conflicts

        keep_conflicts = replace_existing_files is False
        skipped = installed_without_backup | set(conflicts) if keep_conflicts else installed_without_backup
        _replace_files(targets, staged_by_target, file_backups, skipped)
        if staged_launcher is not None and not (keep_conflicts and conflicts):
            files.replace_with_backup(staged_launcher, game_launcher.target, game_launcher.backup, "Launcher setup")
        return conflicts if keep_conflicts else []
