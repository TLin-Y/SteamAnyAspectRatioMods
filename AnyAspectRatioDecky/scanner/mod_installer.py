import hashlib
import math
import re
from collections.abc import Generator
from contextlib import ExitStack, contextmanager
from pathlib import Path, PurePosixPath
from urllib.parse import quote, urlparse

from helper.errors import PluginError
from helper.validator import Validator
from scanner.mod_catalog import (
    ZIP_SOURCE_KINDS,
    IniResolutionPair,
    IniValueKind,
    ModDefinition,
    ModSource,
    SourceKind,
    mod_log_names,
)
from utils import files, folders, streams, text_files, zips
from utils.network import NetworkError, OfflineModeError, download_buffered, download_staged, is_offline_mode

MAX_MOD_DOWNLOAD_SIZE_BYTES = 256 * 1024 * 1024
MAX_MOD_FILE_SIZE_BYTES = 256 * 1024 * 1024
MAX_MOD_TOTAL_SIZE_BYTES = 512 * 1024 * 1024
MAX_INI_SIZE_BYTES = 256 * 1024
MAX_README_SIZE_BYTES = 1024 * 1024
MAX_MOD_LOG_SIZE_BYTES = 256 * 1024
INI_ASSIGNMENT_PATTERN = re.compile(
    rb"^([ \t]*)([A-Za-z][A-Za-z0-9_]*)([ \t]*=[ \t]*)"
    rb"([^;#\r\n]*?)([ \t]*(?:[;#].*)?)(\r?\n)?$"
)
INI_NUMBER_PATTERN = re.compile(rb"[0-9]+(?:\.[0-9]+)?")
INI_RESOLUTION_STRING_PATTERN = re.compile(rb'(["\']?)[1-9][0-9]*x[1-9][0-9]*\1')

def _repository_parts(repository_url: str) -> str:
    if not Validator.GitHub.repository(repository_url):
        raise PluginError("Mod source repository must look like https://github.com/<owner>/<repo>")
    return urlparse(repository_url).path.strip("/")


def repository_file_url(repository_url: str, repository_ref: str, path: str) -> str:
    return (
        f"https://raw.githubusercontent.com/{_repository_parts(repository_url)}/"
        f"{quote(repository_ref, safe='')}/{quote(path, safe='/')}"
    )


class MissingModBackupError(PluginError):
    """A replacement file cannot be safely restored because its original backup is missing."""


_pack_dir: Path | None = None


def set_pack_dir(path: Path | None) -> None:
    """Offline pack: a directory laid out like the repository, used before the network."""
    global _pack_dir
    _pack_dir = path


def _pack_file(path: str) -> Path | None:
    if _pack_dir is None:
        return None
    candidate = _pack_dir / PurePosixPath(path)
    try:
        candidate.resolve().relative_to(_pack_dir.resolve())
    except ValueError:
        return None
    return candidate if candidate.is_file() else None


@contextmanager
def open_repository_file(source: ModSource, path: str):
    """Newest online file first; the offline pack is used in offline mode or when the network fails."""
    local = _pack_file(path)
    with ExitStack() as stack:
        stream = None
        if local is None or not is_offline_mode():
            url = repository_file_url(source.repository_url, source.repository_ref, path)
            try:
                stream = stack.enter_context(download_buffered(url, MAX_MOD_FILE_SIZE_BYTES, "Mod file"))
            except NetworkError:
                if local is None:
                    raise
        if stream is None:
            stream = stack.enter_context(local.open("rb"))
        yield stream


def _mib(size: int) -> int:
    return size // (1024 * 1024)


def _check_total_size(total: int) -> None:
    if total > MAX_MOD_TOTAL_SIZE_BYTES:
        raise PluginError(
            f"Mod files exceed the {_mib(MAX_MOD_TOTAL_SIZE_BYTES)} MiB total limit"
        )


_archive_cache_dir: Path | None = None


def set_archive_cache_dir(path: Path | None) -> None:
    global _archive_cache_dir
    _archive_cache_dir = path


def _cached_archive_path(url: str, sha256: str = "") -> Path | None:
    """Cache file for an archive; the catalog checksum is part of the name, so a replaced zip is re-downloaded."""
    if _archive_cache_dir is None:
        return None
    name = hashlib.sha256(url.encode()).hexdigest()
    return _archive_cache_dir / (f"{name}-{sha256[:16]}.zip" if sha256 else f"{name}.zip")


def _drop_stale_cache(cached_path: Path) -> None:
    prefix = cached_path.name.split("-")[0].removesuffix(".zip")
    for other in cached_path.parent.glob(f"{prefix}*.zip"):
        if other != cached_path:
            other.unlink(missing_ok=True)


@contextmanager
def _open_archive(source: ModSource) -> Generator[Path, None, None]:
    """Yield a local path to the mod archive.

    A pack archive whose checksum matches the catalog is the newest version, so it is used
    directly. Otherwise the newest archive is downloaded; the pack copy is only a fallback
    when the network fails, and the only source in offline mode.
    """
    if source.path is None:
        raise PluginError("Archive source path is missing")
    if source.kind == SourceKind.LOCAL_ARCHIVE:
        local_path = Path(source.path)
        if not local_path.is_file():
            raise PluginError("Local mod archive is unavailable")
        yield local_path
        return
    packed = _pack_file(source.path)
    if packed is not None:
        if source.sha256:
            if files.sha256(packed) == source.sha256:
                yield packed
                return
            if is_offline_mode():
                raise OfflineModeError(
                    "The offline pack has an outdated mod archive. "
                    "Import an updated offline pack or turn off offline mode."
                )
        elif is_offline_mode():
            yield packed
            return

    with ExitStack() as stack:
        try:
            archive_path = stack.enter_context(_open_downloaded_archive(source))
        except NetworkError:
            if packed is None or source.sha256:
                raise
            archive_path = packed
        yield archive_path


@contextmanager
def _open_downloaded_archive(source: ModSource) -> Generator[Path, None, None]:
    """Yield the downloaded (or cached) archive, verified against the catalog checksum."""
    archive_url = repository_file_url(
        source.repository_url,
        source.repository_ref,
        source.path,
    )
    cached_path = _cached_archive_path(archive_url, source.sha256)
    if cached_path is not None and cached_path.is_file() and (source.sha256 or is_offline_mode()):
        if source.sha256 and files.sha256(cached_path) != source.sha256:
            cached_path.unlink(missing_ok=True)
        else:
            try:
                yield cached_path
            except BaseException:
                cached_path.unlink(missing_ok=True)
                raise
            return

    archive_path, _ = download_staged(
        archive_url,
        cached_path.parent if cached_path is not None else None,
        MAX_MOD_DOWNLOAD_SIZE_BYTES,
        "Mod archive download",
        suffix=".zip",
    )
    try:
        if source.sha256 and files.sha256(archive_path) != source.sha256:
            raise PluginError(
                "The downloaded mod archive does not match the catalog checksum. "
                "Update the catalog and try again."
            )
        if cached_path is not None and zips.is_zip(archive_path):
            files.move(archive_path, cached_path)
            archive_path = cached_path
            _drop_stale_cache(cached_path)
        yield archive_path
    finally:
        if cached_path is None or archive_path != cached_path:
            archive_path.unlink(missing_ok=True)


def mod_readme_path(mod: ModDefinition) -> str:
    """README location: beside an archive mod's zip, else in the mod's top-level folder."""
    if mod.source.kind == SourceKind.FILES:
        source_path = mod.source.files[0].path
    elif mod.source.path is not None:
        source_path = mod.source.path
    else:
        raise PluginError(f"Mod {mod.mod_id} has no source path for its README")
    parts = PurePosixPath(source_path).parts
    folder = (
        parts[:1]
        if mod.source.kind == SourceKind.DIRECTORY
        else parts[:-1]
    )
    return "/".join((*folder, "README.md"))


def _read_local_archive_readme(mod: ModDefinition) -> str:
    local_path = Path(mod.source.path or "")
    try:
        with zips.open_zip(local_path, "Local mod archive is unavailable") as archive:
            readme = next(
                (
                    info for info in archive.infolist()
                    if "/" not in info.filename and info.filename.lower().startswith("readme")
                    and info.file_size <= MAX_README_SIZE_BYTES
                ),
                None,
            )
            if readme is not None:
                content = zips.read_file(archive, readme, MAX_README_SIZE_BYTES, "Mod README")
                return content.decode(text_files.BOM_TEXT_ENCODING, errors="replace")
    except OSError as error:
        raise PluginError("Local mod archive is unavailable") from error
    return (
        f"Local archive: `{local_path}`\n\n"
        "No README was found in the archive. Its files are extracted next to the "
        f"game executable (`{mod.source.destination or '.'}`)."
    )


def fetch_mod_readme(mod: ModDefinition) -> str:
    if mod.source.kind == SourceKind.LOCAL_ARCHIVE:
        return _read_local_archive_readme(mod)
    with open_repository_file(mod.source, mod_readme_path(mod)) as stream:
        content = streams.read_limited(stream, MAX_README_SIZE_BYTES, "Mod README")
    try:
        return content.decode(text_files.BOM_TEXT_ENCODING)
    except UnicodeDecodeError as error:
        raise PluginError("Mod README is not valid UTF-8 text") from error


def _game_root(game_path: str) -> Path:
    root = Path(game_path).resolve(strict=True)
    if not root.is_dir():
        raise PluginError("Game installation directory is unavailable")
    return root


def _source_file_map(mod: ModDefinition) -> dict[str, str]:
    source = mod.source
    if source.kind == SourceKind.FILES:
        return {source_file.destination: source_file.path for source_file in source.files}
    if source.kind == SourceKind.DIRECTORY:
        return {path: f"{source.path}/{path}" for path in mod.files}
    if source.kind in ZIP_SOURCE_KINDS:
        prefix = f"{source.destination}/" if source.destination else ""
        result = {}
        for path in mod.files:
            if prefix and not path.startswith(prefix):
                raise PluginError(
                    f"Mod file {path} is outside archive destination {source.destination}"
                )
            result[path] = path[len(prefix) :] if prefix else path
        return result
    raise PluginError(f"Unsupported mod source kind: {source.kind}")


def _read_source_file(mod: ModDefinition, relative_path: str) -> bytes:
    source_path = _source_file_map(mod)[relative_path]
    source = mod.source
    if source.kind in ZIP_SOURCE_KINDS:
        with _open_archive(source) as archive_path, zips.open_zip(archive_path, "Mod archive is invalid") as archive:
            info = _archive_member(archive, source_path)
            return zips.read_file(archive, info, MAX_INI_SIZE_BYTES, "Mod INI source")
    with open_repository_file(source, source_path) as stream:
        return streams.read_limited(stream, MAX_INI_SIZE_BYTES, "Mod INI source")


def _archive_member(archive: zips.ZipFile, name: str) -> zips.ZipInfo:
    try:
        info = archive.getinfo(name)
    except KeyError as error:
        raise PluginError(f"Mod archive is missing {name}") from error
    if info.is_dir():
        raise PluginError(f"Mod archive entry is not a file: {name}")
    return info


def launcher_setup_complete(mod: ModDefinition, game_root: Path) -> bool:
    launcher = mod.launcher_replacement
    if launcher is None:
        return True
    source = folders.resolve_inside(game_root, launcher.source)
    target = folders.resolve_inside(game_root, launcher.target)
    backup = folders.resolve_inside(game_root, launcher.backup)
    return (
        source.is_file() and target.is_file() and backup.is_file()
        and files.equal(source, target)
    )


def _stage_archive_files(
    mod: ModDefinition,
    source_files: dict[str, str],
    targets: dict[str, Path],
    staged_files: list[tuple[Path, Path]],
) -> None:
    with _open_archive(mod.source) as archive_path, zips.open_zip(archive_path, "Mod archive is invalid") as archive:
        members = {path: _archive_member(archive, name) for path, name in source_files.items()}
        total_size = 0
        for info in members.values():
            if info.file_size > MAX_MOD_FILE_SIZE_BYTES:
                raise PluginError(f"Mod file exceeds the {_mib(MAX_MOD_FILE_SIZE_BYTES)} MiB limit")
            total_size += info.file_size
            _check_total_size(total_size)
        for relative_path, info in members.items():
            target = targets[relative_path]
            staged, actual_size = zips.stage_file(archive, info, target.parent, MAX_MOD_FILE_SIZE_BYTES, "Mod file")
            staged_files.append((staged, target))
            total_size += actual_size - info.file_size
            _check_total_size(total_size)


def _stage_downloads(
    mod: ModDefinition,
    source_files: dict[str, str],
    targets: dict[str, Path],
    staged_files: list[tuple[Path, Path]],
) -> None:
    expected_hashes = dict(mod.source.file_hashes)
    total_size = 0
    for relative_path, source_path in source_files.items():
        with open_repository_file(mod.source, source_path) as response:
            staged, size = files.stage_stream(response, targets[relative_path].parent, MAX_MOD_FILE_SIZE_BYTES, "Mod file")
        staged_files.append((staged, targets[relative_path]))
        expected = expected_hashes.get(relative_path)
        if expected and files.sha256(staged) != expected:
            raise PluginError(f"Mod file {relative_path} does not match the catalog checksum")
        total_size += size
        _check_total_size(total_size)


def _adjust_staged_ini(
    mod: ModDefinition, staged_ini: Path, ini_resolution: tuple[int, int], warnings: list[str] | None,
) -> None:
    """Write the configured resolution into the staged INI; keep the original INI if that fails."""
    try:
        content = files.read_limited(staged_ini, MAX_INI_SIZE_BYTES, "Mod INI")
        adjusted = _rewrite_ini_resolution(content, *ini_resolution, mod.ini_resolution)
        if adjusted != content:
            files.write_bytes_atomic(staged_ini, adjusted)
    except (OSError, ValueError):
        if warnings is not None:
            warnings.append(f"Could not adjust {mod.ini_file}; the original INI was kept.")


def apply_mod_files(
    mod: ModDefinition,
    game_path: str,
    *,
    replace_existing_files: bool | None = None,
    ini_resolution: tuple[int, int] | None = None,
    ini_adjustment_warnings: list[str] | None = None,
) -> list[str]:
    """Stage every mod file beside its target, then move the staged files into place.

    Returns the existing files that differ from the mod. While replace_existing_files is None
    nothing is written if there are any; False keeps them and True overwrites them.
    """
    if mod.manual:
        raise PluginError(f"Mod {mod.mod_id} must be installed manually; follow its ReadMe")
    game_root = _game_root(game_path)
    source_files = _source_file_map(mod)
    targets = {relative_path: folders.resolve_inside(game_root, relative_path) for relative_path in mod.files}
    backups = {targets[item.target]: folders.resolve_inside(game_root, item.backup) for item in mod.file_backups}
    for backup_target, backup in backups.items():
        if backup.exists() and not backup.is_file():
            raise PluginError(f"Mod backup path is not a regular file: {backup.name}")
        if not backup.is_file() and not backup_target.is_file():
            raise PluginError("Original mod target and its backup are missing; verify game files first")
    launcher = mod.launcher_replacement
    if launcher is not None:
        launcher_target = folders.resolve_inside(game_root, launcher.target)
        launcher_backup = folders.resolve_inside(game_root, launcher.backup)
        for path in (launcher_target, launcher_backup):
            if path.exists() and not path.is_file():
                raise PluginError(f"Launcher path is not a regular file: {path.name}")
        if not launcher_target.is_file() and not launcher_backup.is_file():
            raise PluginError("Original launcher and its backup are missing; verify game files first")

    staged_files: list[tuple[Path, Path]] = []
    staged_launcher = None
    try:
        if mod.source.kind in ZIP_SOURCE_KINDS:
            _stage_archive_files(mod, source_files, targets, staged_files)
        else:
            _stage_downloads(mod, source_files, targets, staged_files)
        staged_by_target = {target: staged for staged, target in staged_files}
        if mod.ini_file is not None and ini_resolution is not None and mod.ini_resolution:
            staged_ini = staged_by_target.get(folders.resolve_inside(game_root, mod.ini_file))
            if staged_ini is not None:
                _adjust_staged_ini(mod, staged_ini, ini_resolution, ini_adjustment_warnings)

        conflicts = []
        installed_without_backup: set[Path] = set()
        for relative_path, target in targets.items():
            if target.is_dir():
                raise PluginError(f"Mod target is a directory: {relative_path}")
            if not target.is_file():
                continue
            if not files.equal(target, staged_by_target[target]):
                conflicts.append(relative_path)
            elif target in backups and not backups[target].is_file():
                installed_without_backup.add(target)
        if launcher is not None:
            source = folders.resolve_inside(game_root, launcher.source)
            source = staged_by_target.get(source, source)
            if not source.is_file():
                raise PluginError(f"Launcher copy source is missing: {launcher.source}")
            staged_launcher = files.stage_file(source, launcher_target.parent)
            if launcher_target.is_file():
                if not files.equal(launcher_target, staged_launcher):
                    conflicts.append(launcher.target)
                elif not launcher_backup.is_file():
                    raise PluginError(
                        "Launcher is already replaced but its original backup is missing; "
                        "verify game files before applying"
                    )
        if conflicts and replace_existing_files is None:
            return conflicts

        for relative_path, target in targets.items():
            if target in installed_without_backup:
                continue
            if replace_existing_files is False and relative_path in conflicts:
                continue
            files.replace_with_backup(staged_by_target[target], target, backups.get(target), "Mod replacement")
        if staged_launcher is not None and not (replace_existing_files is False and conflicts):
            files.replace_with_backup(staged_launcher, launcher_target, launcher_backup, "Launcher setup")
        return conflicts if replace_existing_files is False else []
    finally:
        for staged, _ in staged_files:
            staged.unlink(missing_ok=True)
        if staged_launcher is not None:
            staged_launcher.unlink(missing_ok=True)


def read_mod_ini(mod: ModDefinition, game_path: str) -> tuple[str, bool]:
    if mod.ini_file is None:
        raise PluginError("This mod does not define an INI configuration file")

    game_root = _game_root(game_path)
    ini_path = folders.resolve_inside(game_root, mod.ini_file)
    if not ini_path.exists():
        return "", False
    if not ini_path.is_file():
        raise PluginError("Mod INI path is not a regular file")
    content = files.read_limited(ini_path, MAX_INI_SIZE_BYTES, "Mod INI file")
    try:
        return content.decode(text_files.TEXT_ENCODING), True
    except UnicodeDecodeError as error:
        raise PluginError("Mod INI file is not valid UTF-8 text") from error


def existing_mod_logs(mod_files: tuple[str, ...] | list[str], game_path: str) -> list[str]:
    root = _game_root(game_path)
    return [name for name in mod_log_names(mod_files) if folders.resolve_inside(root, name).is_file()]


def read_mod_log(mod: ModDefinition, game_path: str, name: str) -> tuple[str, bool]:
    if name not in mod_log_names(mod.files):
        raise PluginError("Log is not associated with this mod")
    root = _game_root(game_path)
    path = folders.resolve_inside(root, name)
    if not path.is_file():
        raise PluginError("Mod log is no longer available")
    return text_files.read_tail(path, MAX_MOD_LOG_SIZE_BYTES)


def _rewrite_ini_resolution(
    content: bytes, width: int, height: int, pairs: tuple[IniResolutionPair, ...]
) -> bytes:
    lines = content.splitlines(keepends=True)
    sections: dict[bytes, dict[bytes, list[tuple[int, re.Match[bytes], int]]]] = {}
    section = b""

    for index, line in enumerate(lines):
        prefix = b"\xef\xbb\xbf" if index == 0 and line.startswith(b"\xef\xbb\xbf") else b""
        candidate = line[len(prefix):]
        stripped = candidate.strip()
        if stripped.startswith(b"[") and b"]" in stripped:
            section = stripped[1:stripped.index(b"]")].strip().lower()
            continue
        match = INI_ASSIGNMENT_PATTERN.match(candidate)
        if match is None:
            continue
        key = match.group(2).lower()
        sections.setdefault(section, {}).setdefault(key, []).append(
            (index, match, len(prefix))
        )

    replacements: dict[int, tuple[int, int, bytes]] = {}
    divisor = math.gcd(width, height)
    ratio_width, ratio_height = width // divisor, height // divisor
    for pair in pairs:
        values = sections.get(pair.section.lower().encode("ascii"), {})
        if pair.kind == IniValueKind.PIXELS_STRING:
            # Only a "WxH" value is rewritten; non-numeric values such as "auto" are left as chosen.
            rows = values.get(pair.width.lower().encode("ascii"), [])
            if len(rows) != 1:
                continue
            index, match, offset = rows[0]
            current = INI_RESOLUTION_STRING_PATTERN.fullmatch(match.group(4))
            if current is None:
                continue
            quote = current.group(1)
            replacements[index] = (
                match.start(4) + offset,
                match.end(4) + offset,
                quote + f"{width}x{height}".encode("ascii") + quote,
            )
            continue
        width_rows = values.get(pair.width.lower().encode("ascii"), [])
        height_rows = values.get(pair.height.lower().encode("ascii"), [])
        if len(width_rows) != 1 or len(height_rows) != 1:
            continue
        width_row, height_row = width_rows[0], height_rows[0]
        width_value, height_value = width_row[1].group(4), height_row[1].group(4)
        if not all(INI_NUMBER_PATTERN.fullmatch(value) for value in (width_value, height_value)):
            continue
        if pair.kind == IniValueKind.PIXELS and (not width_value.isdigit() or not height_value.isdigit()):
            continue
        if float(width_value) <= 0 or float(height_value) <= 0:
            continue
        dimensions = (ratio_width, ratio_height) if pair.kind == IniValueKind.RATIO else (width, height)
        for row, dimension in zip((width_row, height_row), dimensions):
            old_value = row[1].group(4)
            value = str(dimension).encode("ascii")
            if b"." in old_value:
                value += b"." + b"0" * len(old_value.split(b".", 1)[1])
            replacements[row[0]] = (
                row[1].start(4) + row[2],
                row[1].end(4) + row[2],
                value,
            )

    for index, (start, end, value) in replacements.items():
        lines[index] = lines[index][:start] + value + lines[index][end:]
    return b"".join(lines)


def write_mod_ini(
    mod: ModDefinition, game_path: str, content: str, *, expected_content: bytes | None = None
) -> bool:
    if mod.ini_file is None:
        raise PluginError("This mod does not define an INI configuration file")
    if not isinstance(content, str):
        raise PluginError("Mod INI content must be text")

    encoded_content = content.encode(text_files.TEXT_ENCODING)
    if len(encoded_content) > MAX_INI_SIZE_BYTES:
        raise PluginError("Mod INI content exceeds the 256 KiB editor limit")

    game_root = _game_root(game_path)
    ini_path = folders.resolve_inside(game_root, mod.ini_file)
    if ini_path.exists() and not ini_path.is_file():
        raise PluginError("Mod INI path is not a regular file")

    def unchanged_since_read() -> None:
        if expected_content is None:
            return
        if not ini_path.is_file():
            raise PluginError("Mod INI no longer exists")
        if files.read_limited(ini_path, MAX_INI_SIZE_BYTES + 1, "Mod INI file") != expected_content:
            raise PluginError("Mod INI changed during the update; try again")

    return files.write_bytes_atomic(ini_path, encoded_content, keep_mode=True, before_replace=unchanged_since_read)


def update_existing_ini_resolution(
    mod: ModDefinition, game_path: str, width: int, height: int
) -> tuple[bool, bool]:
    if not mod.ini_resolution:
        raise PluginError("This mod has no catalogued resolution fields")
    content, exists = read_mod_ini(mod, game_path)
    if not exists:
        raise PluginError("Mod INI no longer exists")
    original = content.encode(text_files.TEXT_ENCODING)
    adjusted = _rewrite_ini_resolution(original, width, height, mod.ini_resolution)
    if adjusted == original:
        return False, True
    return True, write_mod_ini(
        mod, game_path, adjusted.decode(text_files.TEXT_ENCODING), expected_content=original
    )


def _ini_is_modified(mod: ModDefinition, ini_path: Path, ini_resolution: tuple[int, int] | None) -> bool:
    """Compare the installed INI with the mod's original (with the resolution the plugin wrote)."""
    try:
        original = _read_source_file(mod, mod.ini_file)
        if ini_resolution is not None:
            original = _rewrite_ini_resolution(original, *ini_resolution, mod.ini_resolution)
        return files.read_limited(ini_path, MAX_INI_SIZE_BYTES, "Mod INI file") != original
    except (OSError, ValueError, KeyError):
        # Original unavailable (offline, bad cache): keep the user's INI safe by asking.
        return True


def _restore_backups(backups: dict[Path, Path]) -> None:
    """Copy each backup over its target; nothing is replaced unless every copy was staged."""
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


def remove_mod_files(
    mod: ModDefinition,
    game_path: str,
    *,
    preserve_modified_ini: bool | None = None,
    ini_resolution: tuple[int, int] | None = None,
) -> list[str]:
    """Delete the mod's files and logs and restore backed-up originals.

    A user-modified INI is returned for confirmation while preserve_modified_ini is None;
    True keeps it, False deletes it too.
    """
    if mod.manual:
        raise PluginError(f"Mod {mod.mod_id} must be installed manually; follow its ReadMe")
    game_root = _game_root(game_path)
    backups = {
        folders.resolve_inside(game_root, item.target): folders.resolve_inside(game_root, item.backup)
        for item in mod.file_backups
    }
    for backup_target, backup in backups.items():
        if backup.exists() and not backup.is_file():
            raise PluginError(f"Mod backup path is not a regular file: {backup.name}")
        if backup_target.exists() and not backup_target.is_file():
            raise PluginError(f"Mod target is not a regular file: {backup_target.name}")
        if not backup.is_file() and backup_target.exists():
            raise MissingModBackupError(
                f"Cannot safely remove {backup_target.name} because its original backup "
                f"{backup.name} is missing. In SteamOS, open Steam Library, select the game, "
                "open Properties > Installed Files, and choose Verify integrity of game files. "
                "Steam will restore the original file."
            )
    launcher = mod.launcher_replacement
    if launcher is not None:
        launcher_target = folders.resolve_inside(game_root, launcher.target)
        launcher_backup = folders.resolve_inside(game_root, launcher.backup)
        for path in (launcher_target, launcher_backup):
            if path.exists() and not path.is_file():
                raise PluginError(f"Launcher path is not a regular file: {path.name}")
        if not launcher_backup.is_file():
            launcher_source = folders.resolve_inside(game_root, launcher.source)
            if (
                launcher_source.is_file() and launcher_target.is_file()
                and files.equal(launcher_source, launcher_target)
            ):
                raise PluginError("Original launcher backup is missing; verify game files before removing the mod")

    kept_ini = None
    if mod.ini_file is not None:
        ini_path = folders.resolve_inside(game_root, mod.ini_file)
        if ini_path.is_file() and preserve_modified_ini is not False:
            if _ini_is_modified(mod, ini_path, ini_resolution):
                if preserve_modified_ini is None:
                    return [mod.ini_file]
                kept_ini = ini_path

    targets = list(dict.fromkeys(
        folders.resolve_inside(game_root, name) for name in (*mod.files, *mod_log_names(mod.files))
    ))
    # A launcher copy made from a mod file is removed after the original launcher is back.
    launcher_source = (
        folders.resolve_inside(game_root, launcher.source)
        if launcher is not None and launcher.source in mod.files else None
    )
    _restore_backups(backups)
    for target in targets:
        if target not in (kept_ini, launcher_source) and target not in backups:
            target.unlink(missing_ok=True)
    if launcher is not None and launcher_backup.is_file():
        files.move(launcher_backup, launcher_target)
    if launcher_source is not None:
        launcher_source.unlink(missing_ok=True)
    for backup in backups.values():
        backup.unlink(missing_ok=True)
    folders.remove_empty_parents([target for target in targets if target != kept_ini], game_root)
    return []
