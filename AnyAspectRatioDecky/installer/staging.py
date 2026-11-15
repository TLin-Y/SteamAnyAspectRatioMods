from collections.abc import Generator
from contextlib import contextmanager
from pathlib import Path

from helper.errors import PluginError
from scanner.mod_catalog import ZIP_SOURCE_KINDS, ModDefinition, ModSource, SourceKind
from store import mod_sources
from utils import files, sizes, streams, zips

MAX_MOD_TOTAL_SIZE_BYTES = 512 * sizes.MIB

StagedFiles = list[tuple[Path, Path]]
"""(staged copy, target) pairs."""


@contextmanager
def area() -> Generator[StagedFiles, None, None]:
    """Collect staged copies and delete whatever is left of them (not moved into place) on exit."""
    staged_files: StagedFiles = []
    try:
        yield staged_files
    finally:
        for staged, _ in staged_files:
            staged.unlink(missing_ok=True)


def _check_total_size(total: int) -> None:
    if total > MAX_MOD_TOTAL_SIZE_BYTES:
        raise PluginError(f"Mod files exceed the {sizes.label(MAX_MOD_TOTAL_SIZE_BYTES)} total limit")


def source_file_map(mod: ModDefinition) -> dict[str, str]:
    """Map each installed file (relative to the game) to its path inside the mod source."""
    source = mod.source
    match source.kind:
        case SourceKind.FILES:
            return {source_file.destination: source_file.path for source_file in source.files}
        case SourceKind.DIRECTORY:
            return {path: f"{source.path}/{path}" for path in mod.files}
        case kind if kind in ZIP_SOURCE_KINDS:
            prefix = f"{source.destination}/" if source.destination else ""
            outside = next((path for path in mod.files if not path.startswith(prefix)), None)
            if outside is not None:
                raise PluginError(f"Mod file {outside} is outside archive destination {source.destination}")
            return {path: path.removeprefix(prefix) for path in mod.files}
        case kind:
            raise PluginError(f"Unsupported mod source kind: {kind}")


@contextmanager
def _open_mod_archive(source: ModSource) -> Generator[zips.ZipFile, None, None]:
    with mod_sources.open_archive(source) as archive_path, zips.open_zip(archive_path, "Mod archive is invalid") as archive:
        yield archive


def _archive_member(archive: zips.ZipFile, name: str) -> zips.ZipInfo:
    try:
        info = archive.getinfo(name)
    except KeyError as error:
        raise PluginError(f"Mod archive is missing {name}") from error
    if info.is_dir():
        raise PluginError(f"Mod archive entry is not a file: {name}")
    return info


def read_source_file(mod: ModDefinition, relative_path: str, max_bytes: int, description: str) -> bytes:
    """Read one installed file's original content from the mod source."""
    source_path = source_file_map(mod)[relative_path]
    if mod.source.kind in ZIP_SOURCE_KINDS:
        with _open_mod_archive(mod.source) as archive:
            return zips.read_file(archive, _archive_member(archive, source_path), max_bytes, description)
    with mod_sources.open_repository_file(mod.source, source_path) as stream:
        return streams.read_limited(stream, max_bytes, description)


def _stage_archive_files(
    mod: ModDefinition, source_files: dict[str, str], targets: dict[str, Path], staged_files: StagedFiles,
) -> None:
    max_file = mod_sources.MAX_MOD_FILE_SIZE_BYTES
    with _open_mod_archive(mod.source) as archive:
        members = {path: _archive_member(archive, name) for path, name in source_files.items()}
        total_size = 0
        for info in members.values():
            if info.file_size > max_file:
                raise PluginError(f"Mod file exceeds the {sizes.label(max_file)} limit")
            total_size += info.file_size
            _check_total_size(total_size)
        for relative_path, info in members.items():
            target = targets[relative_path]
            staged, actual_size = zips.stage_file(archive, info, target.parent, max_file, "Mod file")
            staged_files.append((staged, target))
            total_size += actual_size - info.file_size
            _check_total_size(total_size)


def _stage_downloads(
    mod: ModDefinition, source_files: dict[str, str], targets: dict[str, Path], staged_files: StagedFiles,
) -> None:
    expected_hashes = dict(mod.source.file_hashes)
    total_size = 0
    for relative_path, source_path in source_files.items():
        target = targets[relative_path]
        with mod_sources.open_repository_file(mod.source, source_path) as response:
            staged, size = files.stage_stream(response, target.parent, mod_sources.MAX_MOD_FILE_SIZE_BYTES, "Mod file")
        staged_files.append((staged, target))
        expected = expected_hashes.get(relative_path)
        if expected and files.sha256(staged) != expected:
            raise PluginError(f"Mod file {relative_path} does not match the catalog checksum")
        total_size += size
        _check_total_size(total_size)


def stage_files(
    mod: ModDefinition, source_files: dict[str, str], targets: dict[str, Path], staged_files: StagedFiles,
) -> None:
    """Stage every mod file beside its target, appending to staged_files as it goes."""
    stage = _stage_archive_files if mod.source.kind in ZIP_SOURCE_KINDS else _stage_downloads
    stage(mod, source_files, targets, staged_files)
