import struct
import zlib
from pathlib import Path

from scanner.steam_library_scanner import InstalledApp

SHORTCUTS_RELATIVE_PATH = ("config", "shortcuts.vdf")
_TYPE_MAP, _TYPE_STRING, _TYPE_INT32, _TYPE_END = 0, 1, 2, 8


def _read_cstring(data: bytes, index: int) -> tuple[str, int]:
    end = data.index(b"\x00", index)
    return data[index:end].decode("utf-8", errors="replace"), end + 1


def parse_binary_vdf(data: bytes) -> dict[str, object]:
    def parse_map(index: int) -> tuple[dict[str, object], int]:
        result: dict[str, object] = {}
        while True:
            kind = data[index]
            index += 1
            if kind == _TYPE_END:
                return result, index
            key, index = _read_cstring(data, index)
            if kind == _TYPE_MAP:
                result[key], index = parse_map(index)
            elif kind == _TYPE_STRING:
                result[key], index = _read_cstring(data, index)
            elif kind == _TYPE_INT32:
                result[key] = struct.unpack_from("<I", data, index)[0]
                index += 4
            else:
                raise ValueError(f"Unsupported binary VDF type {kind}")

    parsed, _ = parse_map(0)
    return parsed


def _unquote(value: str) -> str:
    value = value.strip()
    return value[1:-1] if len(value) >= 2 and value[0] == value[-1] == '"' else value


def _shortcut_app_id(fields: dict[str, object], exe: str, name: str) -> int:
    stored = fields.get("appid")
    if isinstance(stored, int):
        return stored
    return zlib.crc32(f"{exe}{name}".encode("utf-8")) | 0x80000000


def parse_shortcuts(data: bytes) -> list[InstalledApp]:
    root = parse_binary_vdf(data)
    entries = next(
        (value for key, value in root.items() if key.lower() == "shortcuts"), {}
    )
    apps: list[InstalledApp] = []
    if not isinstance(entries, dict):
        return apps
    for entry in entries.values():
        if not isinstance(entry, dict):
            continue
        fields = {key.lower(): value for key, value in entry.items()}
        name = fields.get("appname")
        raw_exe = fields.get("exe")
        if not isinstance(name, str) or not isinstance(raw_exe, str):
            continue
        exe = _unquote(raw_exe)
        start_dir = fields.get("startdir")
        folder = _unquote(start_dir) if isinstance(start_dir, str) else ""
        game_path = folder or str(Path(exe).parent)
        apps.append(
            InstalledApp(
                app_id=str(_shortcut_app_id(fields, raw_exe, name)),
                name=name,
                install_dir=Path(game_path).name,
                library_path="",
                game_path=game_path if Path(game_path).is_dir() else None,
            )
        )
    return apps


def scan_non_steam_shortcuts(steam_roots: list[Path]) -> list[InstalledApp]:
    apps: list[InstalledApp] = []
    seen: set[tuple[str, str]] = set()
    for root in steam_roots:
        for shortcuts_file in sorted(
            (root / "userdata").glob(f"*/{'/'.join(SHORTCUTS_RELATIVE_PATH)}")
        ):
            try:
                found = parse_shortcuts(shortcuts_file.read_bytes())
            except (OSError, ValueError, IndexError, struct.error):
                continue
            for app in found:
                key = (app.app_id, app.game_path or "")
                if key not in seen:
                    seen.add(key)
                    apps.append(app)
    return apps
