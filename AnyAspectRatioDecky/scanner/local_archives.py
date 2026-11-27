import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from helper import errors
from helper.errors import PluginError
from helper.validator import Charset, Validator
from scanner.mod_catalog import ModDefinition, ModSource, SourceKind
from scanner.steam_library_scanner import InstalledApp
from utils import folders, text_files, zips

# Bounded limits for local archive scanning (to prevent excessive resource usage)
MAX_LOCAL_ARCHIVES = 500
MAX_LOCAL_ARCHIVE_FILES = 5000
MAX_SCAN_DEPTH = 2
# How deep to look for a game's main executable below its install folder.
MAX_EXECUTABLE_DEPTH = 3
INDEX_VERSION = 1
# Unused version suffixes in file names (e.g., "v1.2.3")
VERSION_SUFFIX = re.compile(r"[\s._-]*v?\d+(\.\d+)+.*$", re.IGNORECASE)
# Bracketed expressions in file names (e.g., "[1.2.3]")
BRACKETED = re.compile(r"[\[(][^\])]*[\])]")
# Steam App ID token in file names (e.g., "123456")
APP_ID_TOKEN = re.compile(r"(?<!\d)(\d{3,10})(?!\d)")


@dataclass(frozen=True)
class LocalArchive:
    path: str
    name: str
    files: tuple[str, ...]


def list_archive_files(path: Path) -> list[str]:
    try:
        files = zips.file_names(path)
    except (zips.BadZipFile, OSError) as error:
        raise PluginError(f"not a readable zip file ({error})") from error
    _check_archive_files(files)
    return files


def _check_archive_files(files: list[str]) -> None:
    unsafe = next((name for name in files if not Validator.Paths.is_relative(name)), None)
    if unsafe is not None:
        raise PluginError(f"unsafe entry name {unsafe!r}")
    if not files:
        raise PluginError("archive contains no files")
    if len(files) > MAX_LOCAL_ARCHIVE_FILES:
        raise PluginError(f"more than {MAX_LOCAL_ARCHIVE_FILES} files")
    if len({name.lower() for name in files}) != len(files):
        raise PluginError("entry names collide on case-insensitive file systems")


def _find_archives(directory: Path) -> list[Path]:
    found = [path for path in folders.walk_files(directory, MAX_SCAN_DEPTH) if Validator.Files.has_extension(path.name, ".zip")]
    return found[:MAX_LOCAL_ARCHIVES]


def _load_index(index_path: Path | None, directory: Path) -> dict[str, dict]:
    if index_path is None:
        return {}
    try:
        document = text_files.load_json(index_path)
    except (OSError, ValueError):
        return {}
    match document:
        case {"version": version, "directory": stored, "archives": dict() as archives} if (
            version == INDEX_VERSION and stored == str(directory)
        ):
            return archives
        case _:
            return {}


def _save_index(index_path: Path, directory: Path, archives: dict[str, dict]) -> None:
    text_files.write_json_atomic(
        index_path,
        {"version": INDEX_VERSION, "directory": str(directory), "archives": archives},
        indent=None,
    )


def scan_local_archives(
    directory: str | None,
    index_path: Path | None = None,
) -> tuple[list[LocalArchive], list[str]]:
    """List valid *.zip mods in a directory, reusing cached file lists for unchanged zips."""
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
        key = str(path)
        entry = _index_entry(path, cached.get(key))
        if entry is None:
            continue
        fresh[key] = entry
        if entry.get("error"):
            warnings.append(f"Skipped local archive {path.name}: {entry['error']}")
        else:
            archives.append(LocalArchive(key, path.stem, tuple(entry["files"])))

    if index_path is not None and fresh != cached:
        with errors.collect(warnings, "Local archive index could not be cached"):
            _save_index(index_path, root, fresh)
    return archives, warnings


def _index_entry(path: Path, cached: object) -> dict | None:
    try:
        stat_result = path.stat()
    except OSError:
        return None
    size, mtime_ns = stat_result.st_size, stat_result.st_mtime_ns
    match cached:
        case {"size": cached_size, "mtime_ns": cached_mtime_ns} if (cached_size, cached_mtime_ns) == (size, mtime_ns):
            return cached
    return {"size": size, "mtime_ns": mtime_ns, **_read_archive(path)}


def _read_archive(path: Path) -> dict:
    try:
        return {"files": list_archive_files(path), "error": None}
    except PluginError as failure:
        return {"files": [], "error": str(failure)}


def _normalize(value: str) -> str:
    """Lower case letters and digits only, so "Dark Souls III" and "dark_souls_iii" compare equal."""
    return "".join(character for character in value.lower() if character in Charset.LOWER_ALNUM)


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
    candidates = [
        path
        for path in folders.walk_files(root, MAX_EXECUTABLE_DEPTH)
        if Validator.Files.has_extension(path.name, ".exe") and not Validator.Files.is_helper_executable(path.name)
    ]
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
            and Validator.Files.has_extension(name, ".dll")
            and Validator.Wine.is_proxy_dll(PurePosixPath(name).stem)
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
                kind=SourceKind.LOCAL_ARCHIVE,
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
