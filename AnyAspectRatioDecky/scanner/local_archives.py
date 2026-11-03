import json
import os
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from zipfile import BadZipFile, ZipFile

from helper.constants import MOD_CATALOG_UNSAFE_PATH_CHARACTERS, MOD_SOURCE_LOCAL_ARCHIVE
from scanner.mod_catalog import ModDefinition, ModSource
from scanner.steam_library_scanner import InstalledApp

MAX_LOCAL_ARCHIVES = 500
MAX_LOCAL_ARCHIVE_FILES = 5000
MAX_SCAN_DEPTH = 2
INDEX_VERSION = 1
IGNORED_MEMBERS = {".ds_store", "thumbs.db"}
# DLLs that Wine only loads from the game directory when overridden to "native".
PROXY_DLLS = frozenset(
    {
        "d3d9",
        "d3d11",
        "dinput8",
        "dsound",
        "dxgi",
        "version",
        "wininet",
        "winhttp",
        "winmm",
        "xinput1_3",
        "xinput1_4",
        "xinput9_1_0",
    }
)
HELPER_EXECUTABLE = re.compile(
    r"(unins|crash|redist|vcredist|setup|installer|launcher|eac|easyanticheat|dxsetup)",
    re.IGNORECASE,
)
VERSION_SUFFIX = re.compile(r"[\s._-]*v?\d+(\.\d+)+.*$", re.IGNORECASE)
BRACKETED = re.compile(r"[\[(][^\])]*[\])]")
APP_ID_TOKEN = re.compile(r"(?<!\d)(\d{3,10})(?!\d)")


@dataclass(frozen=True)
class LocalArchive:
    path: str
    name: str
    files: tuple[str, ...]


def _safe_member(name: str) -> bool:
    return (
        bool(name)
        and not name.startswith("/")
        and not any(character in name for character in MOD_CATALOG_UNSAFE_PATH_CHARACTERS)
        and not any(part in ("", ".", "..") for part in name.split("/"))
    )


def _list_archive_files(path: Path) -> tuple[list[str], str | None]:
    try:
        with ZipFile(path) as archive:
            files: list[str] = []
            for info in archive.infolist():
                name = info.filename
                if info.is_dir() or name.startswith("__MACOSX/"):
                    continue
                if name.rsplit("/", 1)[-1].lower() in IGNORED_MEMBERS:
                    continue
                if not _safe_member(name):
                    return [], f"unsafe entry name {name!r}"
                files.append(name)
                if len(files) > MAX_LOCAL_ARCHIVE_FILES:
                    return [], f"more than {MAX_LOCAL_ARCHIVE_FILES} files"
    except (BadZipFile, OSError) as error:
        return [], f"not a readable zip file ({error})"
    if not files:
        return [], "archive contains no files"
    if len({name.lower() for name in files}) != len(files):
        return [], "entry names collide on case-insensitive file systems"
    return files, None


def _find_archives(directory: Path) -> list[Path]:
    found: list[Path] = []
    base_depth = len(directory.parts)
    for root, dirnames, filenames in os.walk(directory):
        root_path = Path(root)
        if len(root_path.parts) - base_depth >= MAX_SCAN_DEPTH:
            dirnames[:] = []
        dirnames[:] = sorted(name for name in dirnames if not name.startswith("."))
        for filename in sorted(filenames):
            if filename.lower().endswith(".zip") and not filename.startswith("."):
                found.append(root_path / filename)
    return found[:MAX_LOCAL_ARCHIVES]


def _load_index(index_path: Path | None, directory: Path) -> dict[str, dict]:
    if index_path is None:
        return {}
    try:
        document = json.loads(index_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    if (
        not isinstance(document, dict)
        or document.get("version") != INDEX_VERSION
        or document.get("directory") != str(directory)
        or not isinstance(document.get("archives"), dict)
    ):
        return {}
    return document["archives"]


def _save_index(index_path: Path, directory: Path, archives: dict[str, dict]) -> None:
    index_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(dir=index_path.parent, suffix=".tmp")
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(
                {"version": INDEX_VERSION, "directory": str(directory), "archives": archives},
                stream,
            )
        os.replace(temporary_name, index_path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def scan_local_archives(
    directory: str | None,
    index_path: Path | None = None,
) -> tuple[list[LocalArchive], list[str]]:
    """List valid *.zip mods in a directory, reusing cached member lists for unchanged files."""
    if not directory:
        return [], []
    root = Path(directory)
    if not root.is_dir():
        return [], [f"Local mod archive folder is unavailable: {directory}"]

    cached = _load_index(index_path, root)
    fresh: dict[str, dict] = {}
    archives: list[LocalArchive] = []
    warnings: list[str] = []
    for path in _find_archives(root):
        try:
            stat_result = path.stat()
        except OSError:
            continue
        key = str(path)
        entry = cached.get(key)
        if (
            not isinstance(entry, dict)
            or entry.get("size") != stat_result.st_size
            or entry.get("mtime_ns") != stat_result.st_mtime_ns
        ):
            files, error = _list_archive_files(path)
            entry = {
                "size": stat_result.st_size,
                "mtime_ns": stat_result.st_mtime_ns,
                "files": files,
                "error": error,
            }
        fresh[key] = entry
        if entry.get("error"):
            warnings.append(f"Skipped local archive {path.name}: {entry['error']}")
            continue
        archives.append(LocalArchive(key, path.stem, tuple(entry["files"])))

    if index_path is not None and fresh != cached:
        try:
            _save_index(index_path, root, fresh)
        except OSError:
            warnings.append("Local archive index could not be cached.")
    return archives, warnings


def _normalize(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def _archive_label(stem: str) -> str:
    return _normalize(VERSION_SUFFIX.sub("", BRACKETED.sub(" ", stem)))


def _match_archive_to_app(
    archive: LocalArchive,
    apps: list[InstalledApp],
) -> InstalledApp | None:
    installed = [app for app in apps if app.game_path is not None]
    app_ids = set(APP_ID_TOKEN.findall(archive.name))
    for app in installed:
        if app.app_id in app_ids:
            return app
    label = _archive_label(archive.name)
    if not label:
        return None
    for app in installed:
        if label in (_normalize(app.name), _normalize(app.install_dir)):
            return app
    return None


def _executable_directory(game_path: Path, app: InstalledApp) -> str:
    """Return the game-relative directory of the game's main executable, or "" for its root."""
    root = game_path.resolve()
    candidates: list[Path] = []
    for current, dirnames, filenames in os.walk(root):
        current_path = Path(current)
        depth = len(current_path.relative_to(root).parts)
        if depth >= 3:
            dirnames[:] = []
        dirnames[:] = [name for name in dirnames if not name.startswith(".")]
        for filename in filenames:
            if filename.lower().endswith(".exe") and not HELPER_EXECUTABLE.search(filename):
                candidates.append(current_path / filename)
    if not candidates:
        return ""
    names = {_normalize(app.name), _normalize(app.install_dir)}
    named = [path for path in candidates if _normalize(path.stem) in names]
    chosen = named or candidates
    directories = {path.parent for path in chosen}
    if len(directories) != 1:
        return ""
    directory = directories.pop().relative_to(root)
    return directory.as_posix() if directory.parts else ""


def _wine_overrides(files: tuple[str, ...], destination: str) -> tuple[str, ...]:
    prefix = f"{destination}/" if destination else ""
    names = sorted(
        {
            PurePosixPath(name).stem.lower()
            for name in files
            if name.startswith(prefix)
            and "/" not in name[len(prefix) :]
            and name.lower().endswith(".dll")
            and PurePosixPath(name).stem.lower() in PROXY_DLLS
        }
    )
    return (f"{','.join(names)}=n,b",) if names else ()


def build_local_mods(
    archives: list[LocalArchive],
    apps: list[InstalledApp],
    reserved_app_ids: set[str],
) -> tuple[tuple[ModDefinition, ...], list[str]]:
    """Match archives to installed games by name/app id and describe where they install."""
    mods: dict[str, ModDefinition] = {}
    warnings: list[str] = []
    for archive in archives:
        app = _match_archive_to_app(archive, apps)
        if app is None:
            warnings.append(f"Local archive {archive.name} does not match an installed game.")
            continue
        if app.app_id in reserved_app_ids:
            warnings.append(
                f"Local archive {archive.name} ignored: {app.name} already has a catalog mod."
            )
            continue
        if app.app_id in mods:
            warnings.append(
                f"Local archive {archive.name} ignored: {app.name} already uses another local archive."
            )
            continue
        destination = _executable_directory(Path(app.game_path or ""), app)
        prefix = f"{destination}/" if destination else ""
        installed_files = tuple(f"{prefix}{name}" for name in archive.files)
        mods[app.app_id] = ModDefinition(
            mod_id=f"local-{app.app_id}",
            name=f"{archive.name} (local archive)",
            steam_app_id=app.app_id,
            install_dir=app.install_dir,
            executable="",
            source=ModSource(
                kind=MOD_SOURCE_LOCAL_ARCHIVE,
                path=archive.path,
                destination=destination,
                files=(),
                repository_url="",
                repository_ref="",
            ),
            files=installed_files,
            ini_file=None,
            wine_dll_overrides=_wine_overrides(installed_files, destination),
        )
    return tuple(mods.values()), warnings
