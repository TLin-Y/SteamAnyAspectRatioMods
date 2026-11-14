import math
from contextlib import ExitStack
from pathlib import Path

from helper.errors import PluginError
from scanner.mod_catalog import (
    ZIP_SOURCE_KINDS,
    IniResolutionPair,
    IniValueKind,
    ModDefinition,
    SourceKind,
    mod_log_names,
)
from store import mod_sources
from utils import files, folders, ini, sizes, streams, text_files, zips

MAX_MOD_TOTAL_SIZE_BYTES = 512 * sizes.MIB
MAX_INI_SIZE_BYTES = 256 * sizes.KIB
MAX_MOD_LOG_SIZE_BYTES = 256 * sizes.KIB

class MissingModBackupError(PluginError):
    """A replacement file cannot be safely restored because its original backup is missing."""


def _check_total_size(total: int) -> None:
    if total > MAX_MOD_TOTAL_SIZE_BYTES:
        raise PluginError(
            f"Mod files exceed the {sizes.label(MAX_MOD_TOTAL_SIZE_BYTES)} total limit"
        )


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
        with mod_sources.open_archive(source) as archive_path, zips.open_zip(archive_path, "Mod archive is invalid") as archive:
            info = _archive_member(archive, source_path)
            return zips.read_file(archive, info, MAX_INI_SIZE_BYTES, "Mod INI source")
    with mod_sources.open_repository_file(source, source_path) as stream:
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
    with mod_sources.open_archive(mod.source) as archive_path, zips.open_zip(archive_path, "Mod archive is invalid") as archive:
        members = {path: _archive_member(archive, name) for path, name in source_files.items()}
        total_size = 0
        for info in members.values():
            if info.file_size > mod_sources.MAX_MOD_FILE_SIZE_BYTES:
                raise PluginError(f"Mod file exceeds the {sizes.label(mod_sources.MAX_MOD_FILE_SIZE_BYTES)} limit")
            total_size += info.file_size
            _check_total_size(total_size)
        for relative_path, info in members.items():
            target = targets[relative_path]
            staged, actual_size = zips.stage_file(archive, info, target.parent, mod_sources.MAX_MOD_FILE_SIZE_BYTES, "Mod file")
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
        with mod_sources.open_repository_file(mod.source, source_path) as response:
            staged, size = files.stage_stream(response, targets[relative_path].parent, mod_sources.MAX_MOD_FILE_SIZE_BYTES, "Mod file")
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
    sections = ini.values_by_section(lines)

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
            row = rows[0]
            quote = ini.resolution_quote(row.read(lines))
            if quote is None:
                continue
            replacements[row.line] = (row.start, row.end, quote + f"{width}x{height}".encode("ascii") + quote)
            continue
        width_rows = values.get(pair.width.lower().encode("ascii"), [])
        height_rows = values.get(pair.height.lower().encode("ascii"), [])
        if len(width_rows) != 1 or len(height_rows) != 1:
            continue
        width_row, height_row = width_rows[0], height_rows[0]
        width_value, height_value = width_row.read(lines), height_row.read(lines)
        if not all(ini.is_decimal(value) for value in (width_value, height_value)):
            continue
        if pair.kind == IniValueKind.PIXELS and (not width_value.isdigit() or not height_value.isdigit()):
            continue
        if float(width_value) <= 0 or float(height_value) <= 0:
            continue
        dimensions = (ratio_width, ratio_height) if pair.kind == IniValueKind.RATIO else (width, height)
        for row, dimension in zip((width_row, height_row), dimensions):
            old_value = row.read(lines)
            value = str(dimension).encode("ascii")
            if b"." in old_value:
                value += b"." + b"0" * len(old_value.split(b".", 1)[1])
            replacements[row.line] = (row.start, row.end, value)

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
        raise PluginError(f"Mod INI content exceeds the {sizes.label(MAX_INI_SIZE_BYTES)} editor limit")

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
