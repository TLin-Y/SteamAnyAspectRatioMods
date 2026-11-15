import os
import zlib
from pathlib import Path

from scanner import steam_layout
from scanner.steam_library_scanner import InstalledApp
from utils.steam_vdf import parse_binary_vdf


def _unquote(value: str) -> str:
    value = value.strip()
    return value[1:-1] if len(value) >= 2 and value[0] == value[-1] == '"' else value


def _shortcut_app_id(fields: dict[str, object], exe: str, name: str) -> int:
    stored = fields.get("appid")
    if isinstance(stored, int):
        return stored
    # Older Steam stores no appid, it derives one as CRC32(exe + name) with the top bit set (non-Steam game).
    return zlib.crc32(f"{exe}{name}".encode()) | 0x80000000 # bare constant applied for non-steam ID


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
                shortcut_executable=os.path.abspath(exe) if Path(exe).is_absolute() else None,
            )
        )

    return apps


def scan_non_steam_shortcuts(steam_roots: list[Path]) -> list[InstalledApp]:
    apps: list[InstalledApp] = []
    seen: set[tuple[str, str]] = set()
    
    for root in steam_roots:
        for shortcuts_file in steam_layout.shortcut_files(root):
            try:
                found = parse_shortcuts(shortcuts_file.read_bytes())
            except (OSError, ValueError):
                continue
            for app in found:
                key = (app.app_id, app.game_path or "")
                if key not in seen:
                    seen.add(key)
                    apps.append(app)
    return apps
