from pathlib import Path

from helper.errors import PluginError


def root(game_path: str) -> Path:
    path = Path(game_path).resolve(strict=True)
    if not path.is_dir():
        raise PluginError("Game installation directory is unavailable")
    return path


def reject_non_file(path: Path, label: str) -> None:
    """A missing path is fine, but anything other than a regular file (a folder, say) is not."""
    if path.exists() and not path.is_file():
        raise PluginError(f"{label} is not a regular file: {path.name}")
