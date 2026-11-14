import hashlib
import math
import os
import re
import socket
import ssl
import stat
import tempfile
from collections.abc import Iterator
from contextlib import ExitStack, contextmanager
from http.client import IncompleteRead
from pathlib import Path, PurePosixPath
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlparse
from urllib.request import urlopen
from zipfile import BadZipFile, ZipFile, is_zipfile

from scanner.mod_catalog import IniResolutionPair, ModDefinition, ModSource

DOWNLOAD_TIMEOUT_SECONDS = 30
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
SYSTEM_CA_BUNDLE_PATHS = (
    Path("/etc/ssl/certs/ca-certificates.crt"),
    Path("/etc/ssl/cert.pem"),
)


def create_ssl_context() -> ssl.SSLContext:
    for ca_bundle in SYSTEM_CA_BUNDLE_PATHS:
        if ca_bundle.is_file():
            return ssl.create_default_context(cafile=str(ca_bundle))
    return ssl.create_default_context()


def _repository_parts(repository_url: str) -> str:
    repository = urlparse(repository_url)
    if repository.hostname != "github.com":
        raise ValueError("Mod source repository must be hosted on github.com")
    owner_and_repo = repository.path.strip("/").split("/")
    if len(owner_and_repo) != 2 or not all(owner_and_repo):
        raise ValueError("Mod source repository URL must identify an owner and repository")
    return "/".join(quote(part, safe="") for part in owner_and_repo)


def repository_file_url(repository_url: str, repository_ref: str, path: str) -> str:
    return (
        f"https://raw.githubusercontent.com/{_repository_parts(repository_url)}/"
        f"{quote(repository_ref, safe='')}/{quote(path, safe='/')}"
    )


class NetworkError(OSError):
    """Any failed internet access; the message is written for the user, not for developers."""


class OfflineModeError(NetworkError):
    """Raised instead of any network access while offline mode is on."""


class MissingModBackupError(ValueError):
    """A replacement file cannot be safely restored because its original backup is missing."""


_offline_mode = False


def set_offline_mode(enabled: bool) -> None:
    global _offline_mode
    _offline_mode = enabled


def describe_network_error(error: BaseException, url: str) -> str:
    host = urlparse(url).hostname or "the server"
    if isinstance(error, HTTPError):
        message = f"{host} answered HTTP {error.code}."
        if error.code == 404:
            message += " The file was not found: check the repository, branch and file path."
        return message
    reason = error.reason if isinstance(error, URLError) else error
    if isinstance(reason, (TimeoutError, socket.timeout)):
        return f"Timed out while contacting {host}. Check your internet connection."
    if isinstance(reason, socket.gaierror):
        return f"No internet connection: could not find {host}."
    if isinstance(reason, ssl.SSLError):
        return f"A secure connection to {host} could not be made."
    detail = str(reason).strip()
    return f"Could not reach {host}" + (f": {detail}." if detail else ".")


class _GuardedResponse:
    """Turns connection failures while reading a download into NetworkError."""

    def __init__(self, response, url: str) -> None:
        self._response = response
        self._url = url
        self._stream = None

    def __enter__(self):
        self._stream = self._response.__enter__()
        return self

    def __exit__(self, *exc_info) -> bool | None:
        return self._response.__exit__(*exc_info)

    def read(self, size: int = -1) -> bytes:
        try:
            return self._stream.read(size)
        except (OSError, IncompleteRead) as error:
            raise NetworkError(describe_network_error(error, self._url)) from error


def open_url(url: str):
    if _offline_mode:
        raise OfflineModeError(
            "Offline mode is on, so nothing is downloaded. "
            "Import the offline pack on the Mods tab to install mods."
        )
    try:
        response = urlopen(url, timeout=DOWNLOAD_TIMEOUT_SECONDS, context=create_ssl_context())
    except OSError as error:
        raise NetworkError(describe_network_error(error, url)) from error
    return _GuardedResponse(response, url)


def _read_limited(stream, max_bytes: int, description: str) -> bytes:
    content = stream.read(max_bytes + 1)
    if len(content) > max_bytes:
        raise ValueError(f"{description} exceeds the size limit")
    return content


def read_url_limited(url: str, max_bytes: int, description: str) -> bytes:
    with open_url(url) as response:
        return _read_limited(response, max_bytes, description)


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
    if local is not None and _offline_mode:
        with local.open("rb") as stream:
            yield stream
        return
    url = repository_file_url(source.repository_url, source.repository_ref, path)
    with tempfile.TemporaryFile() as staged:
        try:
            with open_url(url) as stream:
                copy_limited(stream, staged, MAX_MOD_FILE_SIZE_BYTES, "Mod file")
        except NetworkError:
            if local is None:
                raise
            with local.open("rb") as stream:
                yield stream
            return
        staged.seek(0)
        yield staged


def _mib(size: int) -> int:
    return size // (1024 * 1024)


def _check_total_size(total: int) -> None:
    if total > MAX_MOD_TOTAL_SIZE_BYTES:
        raise ValueError(
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


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _drop_stale_cache(cached_path: Path) -> None:
    prefix = cached_path.name.split("-")[0].removesuffix(".zip")
    for other in cached_path.parent.glob(f"{prefix}*.zip"):
        if other != cached_path:
            other.unlink(missing_ok=True)


@contextmanager
def _open_archive(source: ModSource) -> Iterator[Path]:
    """Yield a local path to the mod archive.

    A pack archive whose checksum matches the catalog is the newest version, so it is used
    directly. Otherwise the newest archive is downloaded; the pack copy is only a fallback
    when the network fails, and the only source in offline mode.
    """
    if source.path is None:
        raise ValueError("Archive source path is missing")
    if source.kind == "local_archive":
        local_path = Path(source.path)
        if not local_path.is_file():
            raise ValueError("Local mod archive is unavailable")
        yield local_path
        return
    packed = _pack_file(source.path)
    if packed is not None:
        if source.sha256:
            if _file_sha256(packed) == source.sha256:
                yield packed
                return
            if _offline_mode:
                raise OfflineModeError(
                    "The offline pack has an outdated mod archive. "
                    "Import an updated offline pack or turn off offline mode."
                )
        elif _offline_mode:
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
def _open_downloaded_archive(source: ModSource) -> Iterator[Path]:
    """Yield the downloaded (or cached) archive, verified against the catalog checksum."""
    archive_url = repository_file_url(
        source.repository_url,
        source.repository_ref,
        source.path,
    )
    cached_path = _cached_archive_path(archive_url, source.sha256)
    if cached_path is not None and cached_path.is_file() and (source.sha256 or _offline_mode):
        if source.sha256 and _file_sha256(cached_path) != source.sha256:
            cached_path.unlink(missing_ok=True)
        else:
            try:
                yield cached_path
            except BaseException:
                cached_path.unlink(missing_ok=True)
                raise
            return

    if cached_path is not None:
        cached_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix="anyaspectratio-",
        suffix=".zip",
        dir=cached_path.parent if cached_path is not None else None,
    )
    archive_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as archive_file:
            with open_url(archive_url) as response:
                copy_limited(
                    response,
                    archive_file,
                    MAX_MOD_DOWNLOAD_SIZE_BYTES,
                    "Mod archive download",
                )
        if source.sha256 and _file_sha256(archive_path) != source.sha256:
            raise ValueError(
                "The downloaded mod archive does not match the catalog checksum. "
                "Update the catalog and try again."
            )
        if cached_path is not None and is_zipfile(archive_path):
            os.replace(archive_path, cached_path)
            archive_path = cached_path
            _drop_stale_cache(cached_path)
        yield archive_path
    finally:
        if cached_path is None or archive_path != cached_path:
            archive_path.unlink(missing_ok=True)


def mod_readme_path(mod: ModDefinition) -> str:
    """README location: beside an archive mod's zip, else in the mod's top-level folder."""
    if mod.source.kind == "files":
        source_path = mod.source.files[0].path
    elif mod.source.path is not None:
        source_path = mod.source.path
    else:
        raise ValueError(f"Mod {mod.mod_id} has no source path for its README")
    parts = PurePosixPath(source_path).parts
    folder = (
        parts[:1]
        if mod.source.kind == "directory"
        else parts[:-1]
    )
    return "/".join((*folder, "README.md"))


def _read_local_archive_readme(mod: ModDefinition) -> str:
    local_path = Path(mod.source.path or "")
    try:
        with ZipFile(local_path) as archive:
            for info in archive.infolist():
                name = info.filename.lower()
                if (
                    "/" not in name
                    and name.startswith("readme")
                    and info.file_size <= MAX_README_SIZE_BYTES
                ):
                    return archive.read(info).decode("utf-8-sig", errors="replace")
    except (BadZipFile, OSError) as error:
        raise ValueError("Local mod archive is unavailable") from error
    return (
        f"Local archive: `{local_path}`\n\n"
        "No README was found in the archive. Its files are extracted next to the "
        f"game executable (`{mod.source.destination or '.'}`)."
    )


def fetch_mod_readme(mod: ModDefinition) -> str:
    if mod.source.kind == "local_archive":
        return _read_local_archive_readme(mod)
    with open_repository_file(mod.source, mod_readme_path(mod)) as stream:
        content = _read_limited(stream, MAX_README_SIZE_BYTES, "Mod README")
    try:
        return content.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ValueError("Mod README is not valid UTF-8 text") from error


def _game_root(game_path: str) -> Path:
    root = Path(game_path).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("Game installation directory is unavailable")
    return root


def _target_path(game_root: Path, relative_path: str) -> Path:
    relative = PurePosixPath(relative_path)
    if (
        not relative_path
        or relative.is_absolute()
        or "\\" in relative_path
        or ":" in relative_path
        or any(part in ("", ".", "..") for part in relative_path.split("/"))
    ):
        raise ValueError(f"Unsafe mod target path: {relative_path}")

    target = game_root.joinpath(*relative.parts)
    if target.is_symlink():
        raise ValueError(f"Mod target is a symbolic link: {relative_path}")
    current = game_root
    for part in relative.parts[:-1]:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"Mod target traverses a symbolic link: {relative_path}")
    try:
        target.parent.resolve(strict=False).relative_to(game_root)
    except ValueError as error:
        raise ValueError(f"Mod target escapes the game directory: {relative_path}") from error
    return target


def _source_file_map(mod: ModDefinition) -> dict[str, str]:
    source = mod.source
    if source.kind == "files":
        return {source_file.destination: source_file.path for source_file in source.files}
    if source.kind == "directory":
        return {path: f"{source.path}/{path}" for path in mod.files}
    if source.kind in ("archive", "local_archive"):
        prefix = f"{source.destination}/" if source.destination else ""
        result = {}
        for path in mod.files:
            if prefix and not path.startswith(prefix):
                raise ValueError(
                    f"Mod file {path} is outside archive destination {source.destination}"
                )
            result[path] = path[len(prefix) :] if prefix else path
        return result
    raise ValueError(f"Unsupported mod source kind: {source.kind}")


def _read_source_file(mod: ModDefinition, relative_path: str) -> bytes:
    source_path = _source_file_map(mod)[relative_path]
    source = mod.source
    if source.kind in ("archive", "local_archive"):
        try:
            with _open_archive(source) as archive_path:
                with ZipFile(archive_path) as archive:
                    try:
                        info = archive.getinfo(source_path)
                    except KeyError as error:
                        raise ValueError(
                            f"Mod archive is missing {relative_path}"
                        ) from error
                    if info.file_size > MAX_INI_SIZE_BYTES:
                        raise ValueError(
                            "Mod INI source exceeds the 256 KiB comparison limit"
                        )
                    with archive.open(info) as source_file:
                        content = source_file.read(MAX_INI_SIZE_BYTES + 1)
        except BadZipFile as error:
            raise ValueError("Mod archive is invalid") from error
    else:
        with open_repository_file(source, source_path) as stream:
            content = _read_limited(stream, MAX_INI_SIZE_BYTES, "Mod INI source")

    if len(content) > MAX_INI_SIZE_BYTES:
        raise ValueError("Mod INI source exceeds the 256 KiB comparison limit")
    return content


def copy_limited(stream, target, max_bytes: int, description: str) -> int:
    total_bytes = 0
    while True:
        chunk = stream.read(min(1024 * 1024, max_bytes - total_bytes + 1))
        if not chunk:
            return total_bytes
        total_bytes += len(chunk)
        if total_bytes > max_bytes:
            raise ValueError(f"{description} exceeds the {max_bytes // (1024 * 1024)} MiB limit")
        target.write(chunk)


def _stage_stream(stream, target: Path, max_bytes: int) -> tuple[Path, int]:
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".anyaspectratio-",
        dir=target.parent,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as staged:
            size = copy_limited(stream, staged, max_bytes, "Mod file")
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise
    return temporary_path, size


def files_equal(first: Path, second: Path) -> bool:
    if first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as left, second.open("rb") as right:
        while True:
            left_chunk = left.read(1024 * 1024)
            right_chunk = right.read(1024 * 1024)
            if left_chunk != right_chunk:
                return False
            if not left_chunk:
                return True


def launcher_setup_complete(mod: ModDefinition, game_root: Path) -> bool:
    launcher = mod.launcher_replacement
    if launcher is None:
        return True
    source = _target_path(game_root, launcher.source)
    target = _target_path(game_root, launcher.target)
    backup = _target_path(game_root, launcher.backup)
    return (
        source.is_file() and target.is_file() and backup.is_file()
        and files_equal(source, target)
    )


def _stage_archive_files(
    mod: ModDefinition,
    source_files: dict[str, str],
    targets: dict[str, Path],
    staged_files: list[tuple[Path, Path]],
) -> None:
    try:
        with _open_archive(mod.source) as archive_path:
            with ZipFile(archive_path) as archive:
                members = {}
                total_uncompressed_size = 0
                for relative_path, member_name in source_files.items():
                    try:
                        info = archive.getinfo(member_name)
                    except KeyError as error:
                        raise ValueError(
                            f"Mod archive is missing {member_name}"
                        ) from error
                    if info.is_dir():
                        raise ValueError(
                            f"Mod archive entry is not a file: {member_name}"
                        )
                    if info.file_size > MAX_MOD_FILE_SIZE_BYTES:
                        raise ValueError(
                            f"Mod file exceeds the "
                            f"{MAX_MOD_FILE_SIZE_BYTES // (1024 * 1024)} MiB limit"
                        )
                    total_uncompressed_size += info.file_size
                    _check_total_size(total_uncompressed_size)
                    members[relative_path] = info
                for relative_path, info in members.items():
                    target = targets[relative_path]
                    with archive.open(info) as source_stream:
                        staged, actual_size = _stage_stream(
                            source_stream,
                            target,
                            MAX_MOD_FILE_SIZE_BYTES,
                        )
                    staged_files.append((staged, target))
                    total_uncompressed_size += actual_size - info.file_size
                    _check_total_size(total_uncompressed_size)
    except BadZipFile as error:
        raise ValueError("Mod archive is invalid") from error


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
            staged, size = _stage_stream(response, targets[relative_path], MAX_MOD_FILE_SIZE_BYTES)
        staged_files.append((staged, targets[relative_path]))
        expected = expected_hashes.get(relative_path)
        if expected and _file_sha256(staged) != expected:
            raise ValueError(f"Mod file {relative_path} does not match the catalog checksum")
        total_size += size
        _check_total_size(total_size)


def _adjust_staged_ini(
    mod: ModDefinition, staged_ini: Path, ini_resolution: tuple[int, int], warnings: list[str] | None,
) -> None:
    """Write the configured resolution into the staged INI; keep the original INI if that fails."""
    try:
        content = staged_ini.read_bytes()
        if len(content) > MAX_INI_SIZE_BYTES:
            raise ValueError("Mod INI exceeds the 256 KiB automatic edit limit")
        adjusted = _rewrite_ini_resolution(content, *ini_resolution, mod.ini_resolution)
        if adjusted != content:
            _replace_staged_file(staged_ini, adjusted)
    except (OSError, ValueError):
        if warnings is not None:
            warnings.append(f"Could not adjust {mod.ini_file}; the original INI was kept.")


def _replace_with_backup(staged: Path, target: Path, backup: Path | None, action: str) -> None:
    """Move staged over target, first renaming an existing original to backup (undone on failure)."""
    renamed = False
    try:
        if backup is not None and target.is_file() and not backup.exists():
            target.rename(backup)
            renamed = True
        os.replace(staged, target)
    except OSError as error:
        if renamed:
            try:
                backup.rename(target)
            except OSError as restore_error:
                raise OSError(
                    f"{action} failed ({error}) and restoration failed "
                    f"({restore_error}); restore {backup.name} manually"
                ) from restore_error
        raise


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
        raise ValueError(f"Mod {mod.mod_id} must be installed manually; follow its ReadMe")
    game_root = _game_root(game_path)
    source_files = _source_file_map(mod)
    targets = {relative_path: _target_path(game_root, relative_path) for relative_path in mod.files}
    backups = {targets[item.target]: _target_path(game_root, item.backup) for item in mod.file_backups}
    for backup_target, backup in backups.items():
        if backup.exists() and not backup.is_file():
            raise ValueError(f"Mod backup path is not a regular file: {backup.name}")
        if not backup.is_file() and not backup_target.is_file():
            raise ValueError("Original mod target and its backup are missing; verify game files first")
    launcher = mod.launcher_replacement
    if launcher is not None:
        launcher_target = _target_path(game_root, launcher.target)
        launcher_backup = _target_path(game_root, launcher.backup)
        for path in (launcher_target, launcher_backup):
            if path.exists() and not path.is_file():
                raise ValueError(f"Launcher path is not a regular file: {path.name}")
        if not launcher_target.is_file() and not launcher_backup.is_file():
            raise ValueError("Original launcher and its backup are missing; verify game files first")

    staged_files: list[tuple[Path, Path]] = []
    staged_launcher = None
    try:
        if mod.source.kind in ("archive", "local_archive"):
            _stage_archive_files(mod, source_files, targets, staged_files)
        else:
            _stage_downloads(mod, source_files, targets, staged_files)
        staged_by_target = {target: staged for staged, target in staged_files}
        if mod.ini_file is not None and ini_resolution is not None and mod.ini_resolution:
            staged_ini = staged_by_target.get(_target_path(game_root, mod.ini_file))
            if staged_ini is not None:
                _adjust_staged_ini(mod, staged_ini, ini_resolution, ini_adjustment_warnings)

        conflicts = []
        installed_without_backup: set[Path] = set()
        for relative_path, target in targets.items():
            if target.is_dir():
                raise ValueError(f"Mod target is a directory: {relative_path}")
            if not target.is_file():
                continue
            if not files_equal(target, staged_by_target[target]):
                conflicts.append(relative_path)
            elif target in backups and not backups[target].is_file():
                installed_without_backup.add(target)
        if launcher is not None:
            source = _target_path(game_root, launcher.source)
            source = staged_by_target.get(source, source)
            if not source.is_file():
                raise ValueError(f"Launcher copy source is missing: {launcher.source}")
            with source.open("rb") as stream:
                staged_launcher, _ = _stage_stream(stream, launcher_target, MAX_MOD_FILE_SIZE_BYTES)
            if launcher_target.is_file():
                if not files_equal(launcher_target, staged_launcher):
                    conflicts.append(launcher.target)
                elif not launcher_backup.is_file():
                    raise ValueError(
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
            _replace_with_backup(staged_by_target[target], target, backups.get(target), "Mod replacement")
        if staged_launcher is not None and not (replace_existing_files is False and conflicts):
            _replace_with_backup(staged_launcher, launcher_target, launcher_backup, "Launcher setup")
        return conflicts if replace_existing_files is False else []
    finally:
        for staged, _ in staged_files:
            staged.unlink(missing_ok=True)
        if staged_launcher is not None:
            staged_launcher.unlink(missing_ok=True)


def read_mod_ini(mod: ModDefinition, game_path: str) -> tuple[str, bool]:
    if mod.ini_file is None:
        raise ValueError("This mod does not define an INI configuration file")

    game_root = _game_root(game_path)
    ini_path = _target_path(game_root, mod.ini_file)
    if not ini_path.exists():
        return "", False
    if not ini_path.is_file():
        raise ValueError("Mod INI path is not a regular file")
    with ini_path.open("rb") as ini_stream:
        content = ini_stream.read(MAX_INI_SIZE_BYTES + 1)
    if len(content) > MAX_INI_SIZE_BYTES:
        raise ValueError("Mod INI file exceeds the 256 KiB editor limit")
    try:
        return content.decode("utf-8"), True
    except UnicodeDecodeError as error:
        raise ValueError("Mod INI file is not valid UTF-8 text") from error


def mod_log_names(files: tuple[str, ...] | list[str]) -> list[str]:
    return list(dict.fromkeys(
        str(PurePosixPath(path).with_suffix(".log"))
        for path in files
        if PurePosixPath(path).suffix.lower() == ".asi"
    ))


def existing_mod_logs(files: tuple[str, ...] | list[str], game_path: str) -> list[str]:
    root = _game_root(game_path)
    return [name for name in mod_log_names(files) if _target_path(root, name).is_file()]


def read_mod_log(mod: ModDefinition, game_path: str, name: str) -> tuple[str, bool]:
    if name not in mod_log_names(mod.files):
        raise ValueError("Log is not associated with this mod")
    root = _game_root(game_path)
    path = _target_path(root, name)
    if not path.is_file():
        raise ValueError("Mod log is no longer available")
    with path.open("rb") as stream:
        stream.seek(0, os.SEEK_END)
        size = stream.tell()
        stream.seek(max(0, size - MAX_MOD_LOG_SIZE_BYTES))
        content = stream.read(MAX_MOD_LOG_SIZE_BYTES)
    return content.decode("utf-8", errors="replace"), size > MAX_MOD_LOG_SIZE_BYTES


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
        if pair.kind == "pixels_string":
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
        if pair.kind == "pixels" and (not width_value.isdigit() or not height_value.isdigit()):
            continue
        if float(width_value) <= 0 or float(height_value) <= 0:
            continue
        dimensions = (ratio_width, ratio_height) if pair.kind == "ratio" else (width, height)
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


def _replace_staged_file(path: Path, content: bytes) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".anyaspectratio-ini-edit-",
        dir=path.parent,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as target:
            target.write(content)
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def write_mod_ini(
    mod: ModDefinition, game_path: str, content: str, *, expected_content: bytes | None = None
) -> bool:
    if mod.ini_file is None:
        raise ValueError("This mod does not define an INI configuration file")
    if not isinstance(content, str):
        raise ValueError("Mod INI content must be text")

    encoded_content = content.encode("utf-8")
    if len(encoded_content) > MAX_INI_SIZE_BYTES:
        raise ValueError("Mod INI content exceeds the 256 KiB editor limit")

    game_root = _game_root(game_path)
    ini_path = _target_path(game_root, mod.ini_file)
    if ini_path.exists() and not ini_path.is_file():
        raise ValueError("Mod INI path is not a regular file")

    ini_path.parent.mkdir(parents=True, exist_ok=True)
    existing_mode = (
        stat.S_IMODE(ini_path.stat().st_mode)
        if ini_path.exists()
        else 0o644
    )
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".anyaspectratio-ini-",
        dir=ini_path.parent,
    )
    temporary_path = Path(temporary_name)
    permissions_preserved = True
    try:
        with os.fdopen(descriptor, "wb") as target:
            target.write(encoded_content)
        try:
            os.chmod(temporary_path, existing_mode)
        except PermissionError:
            permissions_preserved = False
        if expected_content is not None:
            if not ini_path.is_file():
                raise ValueError("Mod INI no longer exists")
            with ini_path.open("rb") as current:
                if current.read(MAX_INI_SIZE_BYTES + 1) != expected_content:
                    raise ValueError("Mod INI changed during the update; try again")
        os.replace(temporary_path, ini_path)
    finally:
        temporary_path.unlink(missing_ok=True)
    return permissions_preserved


def update_existing_ini_resolution(
    mod: ModDefinition, game_path: str, width: int, height: int
) -> tuple[bool, bool]:
    if not mod.ini_resolution:
        raise ValueError("This mod has no catalogued resolution fields")
    content, exists = read_mod_ini(mod, game_path)
    if not exists:
        raise ValueError("Mod INI no longer exists")
    original = content.encode("utf-8")
    adjusted = _rewrite_ini_resolution(original, width, height, mod.ini_resolution)
    if adjusted == original:
        return False, True
    return True, write_mod_ini(
        mod, game_path, adjusted.decode("utf-8"), expected_content=original
    )


def _ini_is_modified(mod: ModDefinition, ini_path: Path, ini_resolution: tuple[int, int] | None) -> bool:
    """Compare the installed INI with the mod's original (with the resolution the plugin wrote)."""
    try:
        original = _read_source_file(mod, mod.ini_file)
        if ini_resolution is not None:
            original = _rewrite_ini_resolution(original, *ini_resolution, mod.ini_resolution)
        return ini_path.stat().st_size != len(original) or ini_path.read_bytes() != original
    except (OSError, ValueError, KeyError, BadZipFile):
        # Original unavailable (offline, bad cache): keep the user's INI safe by asking.
        return True


def _restore_backups(backups: dict[Path, Path]) -> None:
    """Copy each backup over its target; nothing is replaced unless every copy was staged."""
    with ExitStack() as cleanup:
        staged_backups = []
        for target, backup in backups.items():
            if not backup.is_file():
                continue
            backup_size = backup.stat().st_size
            with backup.open("rb") as stream:
                staged, staged_size = _stage_stream(stream, target, backup_size)
            if staged_size != backup_size:
                staged.unlink(missing_ok=True)
                raise OSError(f"Mod backup changed while being restored: {backup.name}")
            cleanup.callback(staged.unlink, missing_ok=True)
            staged_backups.append((staged, target))
        for staged, target in staged_backups:
            os.replace(staged, target)


def _remove_empty_parents(paths: list[Path], game_root: Path) -> None:
    for path in paths:
        parent = path.parent
        while parent != game_root:
            try:
                parent.rmdir()
            except OSError:
                break
            parent = parent.parent


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
        raise ValueError(f"Mod {mod.mod_id} must be installed manually; follow its ReadMe")
    game_root = _game_root(game_path)
    backups = {
        _target_path(game_root, item.target): _target_path(game_root, item.backup)
        for item in mod.file_backups
    }
    for backup_target, backup in backups.items():
        if backup.exists() and not backup.is_file():
            raise ValueError(f"Mod backup path is not a regular file: {backup.name}")
        if backup_target.exists() and not backup_target.is_file():
            raise ValueError(f"Mod target is not a regular file: {backup_target.name}")
        if not backup.is_file() and backup_target.exists():
            raise MissingModBackupError(
                f"Cannot safely remove {backup_target.name} because its original backup "
                f"{backup.name} is missing. In SteamOS, open Steam Library, select the game, "
                "open Properties > Installed Files, and choose Verify integrity of game files. "
                "Steam will restore the original file."
            )
    launcher = mod.launcher_replacement
    if launcher is not None:
        launcher_target = _target_path(game_root, launcher.target)
        launcher_backup = _target_path(game_root, launcher.backup)
        for path in (launcher_target, launcher_backup):
            if path.exists() and not path.is_file():
                raise ValueError(f"Launcher path is not a regular file: {path.name}")
        if not launcher_backup.is_file():
            launcher_source = _target_path(game_root, launcher.source)
            if (
                launcher_source.is_file() and launcher_target.is_file()
                and files_equal(launcher_source, launcher_target)
            ):
                raise ValueError("Original launcher backup is missing; verify game files before removing the mod")

    kept_ini = None
    if mod.ini_file is not None:
        ini_path = _target_path(game_root, mod.ini_file)
        if ini_path.is_file() and preserve_modified_ini is not False:
            if _ini_is_modified(mod, ini_path, ini_resolution):
                if preserve_modified_ini is None:
                    return [mod.ini_file]
                kept_ini = ini_path

    targets = list(dict.fromkeys(
        _target_path(game_root, name) for name in (*mod.files, *mod_log_names(mod.files))
    ))
    # A launcher copy made from a mod file is removed after the original launcher is back.
    launcher_source = (
        _target_path(game_root, launcher.source)
        if launcher is not None and launcher.source in mod.files else None
    )
    _restore_backups(backups)
    for target in targets:
        if target not in (kept_ini, launcher_source) and target not in backups:
            target.unlink(missing_ok=True)
    if launcher is not None and launcher_backup.is_file():
        os.replace(launcher_backup, launcher_target)
    if launcher_source is not None:
        launcher_source.unlink(missing_ok=True)
    for backup in backups.values():
        backup.unlink(missing_ok=True)
    _remove_empty_parents([target for target in targets if target != kept_ini], game_root)
    return []
