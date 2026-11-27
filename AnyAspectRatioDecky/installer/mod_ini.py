import math
from pathlib import Path

from helper.errors import PluginError
from installer import game_folder, staging
from scanner.mod_catalog import PIXELS_STRING_KINDS, IniResolutionPair, IniValueKind, ModDefinition
from utils import files, folders, ini, sizes, text_files

MAX_INI_SIZE_BYTES = 256 * sizes.KIB


def _ini_path(mod: ModDefinition, game_path: str) -> Path:
    if mod.ini_file is None:
        raise PluginError("This mod does not define an INI configuration file")
    path = folders.resolve_inside(game_folder.root(game_path), mod.ini_file)
    game_folder.reject_non_file(path, "Mod INI path")
    return path


def _key(name: str) -> bytes:
    return name.lower().encode("ascii")


def _with_decimals_of(current: bytes, number: int) -> bytes:
    _, dot, decimals = current.partition(b".")
    return str(number).encode("ascii") + dot + b"0" * len(decimals)


def _number_replacements(
    lines: list[bytes], rows: tuple[ini.Value, ...], numbers: tuple[int, ...], whole_numbers: bool,
) -> list[tuple[ini.Value, bytes]]:
    current = [row.read(lines) for row in rows]
    if not all(ini.is_decimal(value) for value in current):
        return []
    if whole_numbers and not all(value.isdigit() for value in current):
        return []
    if any(float(value) <= 0 for value in current):
        return []
    return [(row, _with_decimals_of(value, number)) for row, value, number in zip(rows, current, numbers)]


def rewrite_ini_resolution(content: bytes, width: int, height: int, pairs: tuple[IniResolutionPair, ...]) -> bytes:
    lines = content.splitlines(keepends=True)
    sections = ini.values_by_section(lines)
    divisor = math.gcd(width, height)
    replacements: list[tuple[ini.Value, bytes]] = []
    for pair in pairs:
        values = sections.get(_key(pair.section), {})
        width_rows, height_rows = values.get(_key(pair.width), []), values.get(_key(pair.height), [])
        match pair.kind, width_rows, height_rows:
            case kind, [row], _ if kind in PIXELS_STRING_KINDS:
                quote = ini.resolution_quote(row.read(lines))
                if quote is None and kind == IniValueKind.PIXELS_STRING_AUTO:
                    quote = ini.auto_quote(row.read(lines))
                if quote is not None:
                    replacements.append((row, quote + f"{width}x{height}".encode("ascii") + quote))
            case IniValueKind.RATIO, [width_row], [height_row]:
                ratio = (width // divisor, height // divisor)
                replacements += _number_replacements(lines, (width_row, height_row), ratio, whole_numbers=False)
            case IniValueKind.PIXELS, [width_row], [height_row]:
                replacements += _number_replacements(lines, (width_row, height_row), (width, height), whole_numbers=True)

    for row, value in {row.line: (row, value) for row, value in replacements}.values():
        line = lines[row.line]
        lines[row.line] = line[:row.start] + value + line[row.end:]
    return b"".join(lines)


def adjust_staged(
    mod: ModDefinition, staged_ini: Path, ini_resolution: tuple[int, int], warnings: list[str] | None,
) -> None:
    try:
        content = files.read_limited(staged_ini, MAX_INI_SIZE_BYTES, "Mod INI")
        adjusted = rewrite_ini_resolution(content, *ini_resolution, mod.ini_resolution)
        if adjusted != content:
            files.write_bytes_atomic(staged_ini, adjusted)
    except (OSError, ValueError):
        if warnings is not None:
            warnings.append(f"Could not adjust {mod.ini_file}; the original INI was kept.")


def is_modified(mod: ModDefinition, ini_path: Path, ini_resolution: tuple[int, int] | None) -> bool:
    try:
        original = staging.read_source_file(mod, mod.ini_file, MAX_INI_SIZE_BYTES, "Mod INI source")
        if ini_resolution is not None:
            original = rewrite_ini_resolution(original, *ini_resolution, mod.ini_resolution)
        return files.read_limited(ini_path, MAX_INI_SIZE_BYTES, "Mod INI file") != original
    except (OSError, ValueError, KeyError):
        # Original unavailable (offline, bad cache): keep the user's INI safe by asking.
        return True


def read_mod_ini(mod: ModDefinition, game_path: str) -> tuple[str, bool]:
    ini_path = _ini_path(mod, game_path)
    if not ini_path.exists():
        return "", False
    content = files.read_limited(ini_path, MAX_INI_SIZE_BYTES, "Mod INI file")
    try:
        return content.decode(text_files.TEXT_ENCODING), True
    except UnicodeDecodeError as error:
        raise PluginError("Mod INI file is not valid UTF-8 text") from error


def write_mod_ini(mod: ModDefinition, game_path: str, content: str, *, expected_content: bytes | None = None) -> bool:
    if not isinstance(content, str):
        raise PluginError("Mod INI content must be text")
    encoded_content = content.encode(text_files.TEXT_ENCODING)
    if len(encoded_content) > MAX_INI_SIZE_BYTES:
        raise PluginError(f"Mod INI content exceeds the {sizes.label(MAX_INI_SIZE_BYTES)} editor limit")
    ini_path = _ini_path(mod, game_path)

    def unchanged_since_read() -> None:
        if expected_content is None:
            return
        if not ini_path.is_file():
            raise PluginError("Mod INI no longer exists")
        if files.read_limited(ini_path, MAX_INI_SIZE_BYTES + 1, "Mod INI file") != expected_content:
            raise PluginError("Mod INI changed during the update; try again")

    return files.write_bytes_atomic(ini_path, encoded_content, keep_mode=True, before_replace=unchanged_since_read)


def update_existing_ini_resolution(mod: ModDefinition, game_path: str, width: int, height: int) -> tuple[bool, bool]:
    if not mod.ini_resolution:
        raise PluginError("This mod has no catalogued resolution fields")
    content, exists = read_mod_ini(mod, game_path)
    if not exists:
        raise PluginError("Mod INI no longer exists")
    original = content.encode(text_files.TEXT_ENCODING)
    adjusted = rewrite_ini_resolution(original, width, height, mod.ini_resolution)
    if adjusted == original:
        return False, True
    return True, write_mod_ini(mod, game_path, adjusted.decode(text_files.TEXT_ENCODING), expected_content=original)
