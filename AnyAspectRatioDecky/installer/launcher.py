"""A mod that replaces the game's launcher with a copy of another file (the original is kept as a backup)."""

from dataclasses import dataclass
from pathlib import Path

from helper.errors import PluginError
from installer import game_folder
from scanner.mod_catalog import LauncherReplacement, ModDefinition
from utils import files, folders


@dataclass(frozen=True, slots=True)
class Launcher:
    entry: LauncherReplacement
    source: Path
    target: Path
    backup: Path


def resolve(mod: ModDefinition, game_root: Path) -> Launcher | None:
    entry = mod.launcher_replacement
    if entry is None:
        return None
    return Launcher(
        entry,
        folders.resolve_inside(game_root, entry.source),
        folders.resolve_inside(game_root, entry.target),
        folders.resolve_inside(game_root, entry.backup),
    )


def setup_complete(mod: ModDefinition, game_root: Path) -> bool:
    launcher = resolve(mod, game_root)
    return launcher is None or (
        launcher.source.is_file() and launcher.target.is_file() and launcher.backup.is_file()
        and files.equal(launcher.source, launcher.target)
    )


def _reject_non_files(launcher: Launcher) -> None:
    for path in (launcher.target, launcher.backup):
        game_folder.reject_non_file(path, "Launcher path")


def check_before_apply(launcher: Launcher | None) -> None:
    if launcher is None:
        return
    _reject_non_files(launcher)
    if not launcher.target.is_file() and not launcher.backup.is_file():
        raise PluginError("Original launcher and its backup are missing; verify game files first")


def check_before_remove(launcher: Launcher | None) -> None:
    if launcher is None:
        return
    _reject_non_files(launcher)
    if (
        not launcher.backup.is_file() and launcher.source.is_file() and launcher.target.is_file()
        and files.equal(launcher.source, launcher.target)
    ):
        raise PluginError("Original launcher backup is missing; verify game files before removing the mod")


def stage(launcher: Launcher, staged_by_target: dict[Path, Path]) -> Path:
    """Stage the replacement beside the launcher, taking it from the staged mod files when the mod ships it."""
    source = staged_by_target.get(launcher.source, launcher.source)
    if not source.is_file():
        raise PluginError(f"Launcher copy source is missing: {launcher.entry.source}")
    return files.stage_file(source, launcher.target.parent)


def conflicts(launcher: Launcher, staged: Path) -> bool:
    """Whether the current launcher is neither the original nor already the replacement."""
    if not launcher.target.is_file():
        return False
    if not files.equal(launcher.target, staged):
        return True
    if not launcher.backup.is_file():
        raise PluginError(
            "Launcher is already replaced but its original backup is missing; verify game files before applying"
        )
    return False


def restore(launcher: Launcher | None) -> None:
    if launcher is not None and launcher.backup.is_file():
        files.move(launcher.backup, launcher.target)
