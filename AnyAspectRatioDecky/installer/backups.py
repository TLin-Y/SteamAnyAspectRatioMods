from contextlib import ExitStack
from pathlib import Path

from helper.errors import PluginError
from installer import game_folder
from scanner.mod_catalog import ModDefinition
from utils import files, folders

# Kindly notify user no .bak file
class MissingModBackupError(PluginError):
    """A replacement file cannot be safely restored because its original backup is missing."""


def resolve_files(mod: ModDefinition, game_root: Path) -> dict[Path, Path]:
    """Map each replaced game file to the backup of its original."""
    return {
        folders.resolve_inside(game_root, item.target): folders.resolve_inside(game_root, item.backup)
        for item in mod.file_backups
    }


def check_before_apply(backups: dict[Path, Path]) -> None:
    for target, backup in backups.items():
        game_folder.reject_non_file(backup, "Mod backup path")
        if not backup.is_file() and not target.is_file():
            raise PluginError("Original mod target and its backup are missing; verify game files first")


def check_before_remove(backups: dict[Path, Path]) -> None:
    for target, backup in backups.items():
        game_folder.reject_non_file(backup, "Mod backup path")
        game_folder.reject_non_file(target, "Mod target")
        if not backup.is_file() and target.exists():
            raise MissingModBackupError(
                f"Cannot safely remove {target.name} because its original backup "
                f"{backup.name} is missing. In SteamOS, open Steam Library, select the game, "
                "open Properties > Installed Files, and choose Verify integrity of game files. "
                "Steam will restore the original file."
            )


def restore(backups: dict[Path, Path]) -> None:
    with ExitStack() as cleanup:
        staged_backups = []
        for target, backup in backups.items():
            if not backup.is_file():
                continue
            staged = files.stage_file(backup, target.parent)
            cleanup.callback(staged.unlink, missing_ok=True)
            staged_backups.append((staged, target))
        for staged, target in staged_backups:
            files.move(staged, target)
