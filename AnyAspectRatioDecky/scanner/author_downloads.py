import hashlib
import os
import sys
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import replace
from pathlib import Path

from helper.errors import PluginError
from installer.staging import source_file_map
from scanner.local_archives import LocalArchive, list_archive_files, scan_local_archives
from scanner.mod_catalog import ModDefinition, SourceKind
from store.mod_sources import MAX_MOD_DOWNLOAD_SIZE_BYTES
from utils import files, sizes

# Windows keeps each user's (possibly moved) Downloads path under its known-folder ID, FOLDERID_Downloads.
_FOLDERID_DOWNLOADS = "{374DE290-123F-4565-9164-39C4925E467B}"
_USER_SHELL_FOLDERS_KEY = r"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders"


def _windows_downloads() -> Path | None:
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, _USER_SHELL_FOLDERS_KEY) as key:
            value, _ = winreg.QueryValueEx(key, _FOLDERID_DOWNLOADS)
    except (ImportError, OSError):
        return None
    return Path(os.path.expandvars(value)) if isinstance(value, str) and value else None


def _xdg_downloads(home: Path, environment: Mapping[str, str]) -> Path | None:
    config = Path(environment.get("XDG_CONFIG_HOME") or home / ".config") / "user-dirs.dirs"
    try:
        lines = config.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return None
    for line in lines:
        name, equals, value = line.strip().partition("=")
        if name != "XDG_DOWNLOAD_DIR" or not equals:
            continue
        value = value.strip().strip('"')
        if value.startswith("$HOME"):
            return home / value.removeprefix("$HOME").lstrip("/")
        if value.startswith("/"):
            return Path(value)
    return None


def downloads_folder(
    home: Path | None = None, platform: str = sys.platform, environment: Mapping[str, str] = os.environ,
) -> Path:
    home = Path.home() if home is None else home
    configured = (
        _windows_downloads() if platform == "win32"
        else None if platform == "darwin"
        else _xdg_downloads(home, environment)
    )
    return configured or home / "Downloads"


def archive_root(names: Iterable[str], wanted: Sequence[str]) -> str | None:
    """The one folder inside a zip that holds every wanted file ("" for the zip root); None if none or several do."""
    present = set(names)
    first = wanted[0]
    roots = sorted(
        {name.removesuffix(first) for name in present if name == first or name.endswith(f"/{first}")},
        key=len,
    )
    fitting = [root for root in roots if all(f"{root}{name}" in present for name in wanted)]
    if len(fitting) == 1 or (len(fitting) > 1 and len(fitting[0]) < len(fitting[1])):
        return fitting[0]
    return None


def _wanted(mod: ModDefinition) -> list[str]:
    return list(source_file_map(replace(mod, source=replace(mod.source, archive_root=""))).values())


def _with_download(mod: ModDefinition, path: Path, root: str) -> ModDefinition:
    return replace(mod, source=replace(mod.source, download_path=str(path), archive_root=root))


def _index_path(index_dir: Path | None, folder: Path) -> Path | None:
    if index_dir is None:
        return None
    return index_dir / f"author_downloads-{hashlib.sha256(str(folder).encode()).hexdigest()[:16]}.json"


def scan_download_folders(folders: Sequence[Path], index_dir: Path | None = None) -> list[LocalArchive]:
    archives = []
    for folder in dict.fromkeys(folders):
        found, _ = scan_local_archives(str(folder), _index_path(index_dir, folder))
        archives += found
    return archives


def find_downloads(mods: Iterable[ModDefinition], archives: Sequence[LocalArchive]) -> dict[str, ModDefinition]:
    """Each author-download mod with the newest zip that holds its files; unchanged when none does."""
    result = {}
    for mod in mods:
        if mod.source.kind != SourceKind.AUTHOR_DOWNLOAD:
            continue
        wanted = _wanted(mod)
        candidates = []
        for archive in archives:
            root = archive_root(archive.files, wanted)
            if root is None:
                continue
            try:
                candidates.append((Path(archive.path).stat().st_mtime, archive.path, root))
            except OSError:
                continue
        if candidates:
            _, found, root = max(candidates)
            result[mod.mod_id] = _with_download(mod, Path(found), root)
        else:
            result[mod.mod_id] = mod
    return result


def stored_path(store_dir: Path, mod: ModDefinition) -> Path:
    return store_dir / f"{mod.mod_id}.zip"


def _checked_download(mod: ModDefinition, path: Path) -> ModDefinition:
    try:
        names = list_archive_files(path)
    except PluginError as error:
        raise PluginError(f"{path.name}: {error}") from error
    wanted = _wanted(mod)
    root = archive_root(names, wanted)
    if root is None:
        raise PluginError(f"{path.name} does not hold {', '.join(wanted)} in exactly one folder")
    return _with_download(mod, path, root)


def applied_download(mod: ModDefinition, store_dir: Path) -> ModDefinition:
    """The saved copy of the zip that was applied, so Remove compares against what was installed."""
    saved = stored_path(store_dir, mod)
    return _checked_download(mod, saved) if saved.is_file() else mod


def keep_download(mod: ModDefinition, store_dir: Path) -> ModDefinition:
    """Copy the user's zip into the plugin's folder, so Remove still works after it leaves Downloads."""
    source, target = Path(mod.source.download_path), stored_path(store_dir, mod)
    if not mod.source.download_path or not source.is_file():
        raise PluginError(f"Download {mod.name} from the author's page first: {mod.source.page}")
    if source == target:
        return mod
    if source.stat().st_size > MAX_MOD_DOWNLOAD_SIZE_BYTES:
        raise PluginError(f"The downloaded zip exceeds the {sizes.label(MAX_MOD_DOWNLOAD_SIZE_BYTES)} limit")
    store_dir.mkdir(parents=True, exist_ok=True)
    staged = files.stage_file(source, store_dir)
    try:
        files.move(staged, target)
    finally:
        staged.unlink(missing_ok=True)
    return replace(mod, source=replace(mod.source, download_path=str(target)))


def import_download(mod: ModDefinition, path: Path, store_dir: Path) -> ModDefinition:
    return keep_download(_checked_download(mod, path), store_dir)
