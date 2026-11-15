import hashlib
from collections.abc import Generator
from contextlib import ExitStack, contextmanager
from pathlib import Path, PurePosixPath
from urllib.parse import quote, urlparse

from helper.errors import PluginError
from helper.validator import Validator
from scanner.mod_catalog import ModDefinition, ModSource, SourceKind
from utils import files, sizes, streams, text_files, zips
from utils.network import NetworkError, OfflineModeError, download_buffered, download_staged, is_offline_mode

# Bounded sizes for the plugin process, we shouldn't load excessively large files into memory
# Tested manually in DroidDeck/Steam OS/Steam OS ARM64
MAX_MOD_DOWNLOAD_SIZE_BYTES = 256 * sizes.MIB
MAX_MOD_FILE_SIZE_BYTES = 256 * sizes.MIB
MAX_README_SIZE_BYTES = sizes.MIB


def repository_path(repository_url: str) -> str:
    if not Validator.GitHub.repository(repository_url):
        raise PluginError("Mod source repository must look like https://github.com/<owner>/<repo>")
    return urlparse(repository_url).path.strip("/")


def repository_file_url(repository_url: str, repository_ref: str, path: str) -> str:
    return (
        f"https://raw.githubusercontent.com/{repository_path(repository_url)}/"
        f"{quote(repository_ref, safe='')}/{quote(path, safe='/')}"
    )


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
    """Order: 1. online file 2. the offline pack."""
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


_archive_cache_dir: Path | None = None


def set_archive_cache_dir(path: Path | None) -> None:
    global _archive_cache_dir
    _archive_cache_dir = path


def _cached_archive_path(url: str, sha256: str = "") -> Path | None:
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
def open_archive(source: ModSource) -> Generator[Path, None, None]:
    """Get a local path to the mod archive.

    1. from cache
    2. download from the repo
    3. the offline pack (only if the network fails or in offline mode)
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
            if files.matches_sha256(packed, source.sha256):
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
    archive_url = repository_file_url(
        source.repository_url,
        source.repository_ref,
        source.path,
    )
    cached_path = _cached_archive_path(archive_url, source.sha256)
    if cached_path is not None and cached_path.is_file() and (source.sha256 or is_offline_mode()):
        if source.sha256 and not files.matches_sha256(cached_path, source.sha256):
            cached_path.unlink(missing_ok=True)
        else:
            try:
                yield cached_path
            except BaseException:
                # The failure is the caller's unless the cached file is no longer a readable zip.
                if not zips.is_zip(cached_path):
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
        if source.sha256 and not files.matches_sha256(archive_path, source.sha256):
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
    """README location: beside an archive mod's zip, else in the mod's top level dir."""
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
