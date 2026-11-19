"""Facts about this computer for the desktop header and the resolution choices in Settings."""
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from functools import cache
from pathlib import Path

from scanner import steam_layout

_RESOLUTION = re.compile(r"^\s*(\d{3,5})\s*x\s*(\d{3,5})\s*$")


def _run(*command: str) -> str:
    try:
        return subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return ""


def _read(path: str) -> str:
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace").strip()
    except OSError:
        return ""


@cache
def device_name() -> str:
    if sys.platform == "darwin":
        try:
            (hardware,) = json.loads(_run("system_profiler", "-json", "SPHardwareDataType"))["SPHardwareDataType"]
            chip = hardware.get("chip_type") or hardware.get("cpu_type")
            return f"{hardware['machine_name']} ({chip})" if chip else hardware["machine_name"]
        except (ValueError, KeyError, TypeError):
            return platform.node()
    if sys.platform == "win32":
        bios = _windows_values(r"HARDWARE\DESCRIPTION\System\BIOS", "SystemManufacturer", "SystemProductName",
                               "BaseBoardManufacturer", "BaseBoardProduct")
        cpu = _windows_values(r"HARDWARE\DESCRIPTION\System\CentralProcessor\0", "ProcessorNameString")
        return _with_cpu(_model(*bios[:2]) or _model(*bios[2:]) or platform.node(), cpu[0])
    cpu = next((line.partition(":")[2] for line in _read("/proc/cpuinfo").splitlines()
                if line.startswith("model name")), "")
    model = _model(_read("/sys/class/dmi/id/sys_vendor"), _read("/sys/class/dmi/id/product_name"))
    return _with_cpu(model or platform.node(), cpu)


# Text that self-built PCs leave in their firmware instead of a real model name.
_PLACEHOLDERS = ("system product name", "system manufacturer", "to be filled", "default string", "not applicable")


def _model(maker: str, model: str) -> str:
    maker, model = maker.strip(), model.strip()
    if not model or any(text in model.lower() for text in _PLACEHOLDERS):
        return ""
    if not maker or any(text in maker.lower() for text in _PLACEHOLDERS) or model.startswith(maker):
        return model
    return f"{maker} {model}"


def _with_cpu(model: str, cpu: str) -> str:
    cpu = " ".join(cpu.split())
    return f"{model} ({cpu})" if cpu else model


def _windows_values(key_path: str, *names: str) -> list[str]:
    import winreg

    values = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path) as key:
            for name in names:
                try:
                    values.append(str(winreg.QueryValueEx(key, name)[0]))
                except OSError:
                    values.append("")
    except OSError:
        values = [""] * len(names)
    return values


@cache
def os_name() -> str:
    if sys.platform == "darwin":
        return f"macOS {platform.mac_ver()[0]}"
    if sys.platform == "win32":
        build = sys.getwindowsversion().build
        return f"Windows {'11' if build >= 22000 else '10'} ({build})"
    for line in _read("/etc/os-release").splitlines():
        if line.startswith("PRETTY_NAME="):
            return line.partition("=")[2].strip().strip('"')
    return f"Linux {platform.release()}"


# Linux filesystems that hold user data; everything else in /proc/mounts is virtual or system.
_LINUX_DATA_FILESYSTEMS = {"ext4", "ext3", "btrfs", "xfs", "f2fs", "vfat", "exfat", "ntfs", "ntfs3", "fuseblk", "zfs"}


def _mount_points() -> list[str]:
    if sys.platform == "win32":
        return [str(drive) for drive in os.listdrives()] if hasattr(os, "listdrives") else [
            f"{letter}:\\" for letter in "CDEFGHIJKLMNOPQRSTUVWXYZ" if Path(f"{letter}:\\").exists()
        ]
    if sys.platform == "darwin":
        volumes = sorted(str(path) for path in Path("/Volumes").glob("*") if path.is_dir() and not path.is_symlink())
        return ["/", *volumes]
    mounts = []
    for line in _read("/proc/mounts").splitlines():
        parts = line.split()
        if len(parts) > 2 and parts[2] in _LINUX_DATA_FILESYSTEMS and not parts[1].startswith(("/boot", "/snap")):
            mounts.append(parts[1].replace("\\040", " "))
    return mounts or ["/"]


def disks() -> list[dict[str, object]]:
    """Every mounted data disk with its space; one entry per physical volume."""
    found, seen = [], set()
    for mount in _mount_points():
        try:
            usage = shutil.disk_usage(mount)
        except OSError:
            continue  # An empty card reader or a disconnected network drive.
        identity = (usage.total, usage.free)
        if identity in seen:
            continue  # macOS shows the system volume twice (/ and its Data volume).
        seen.add(identity)
        found.append({"path": mount, "free_bytes": usage.free, "total_bytes": usage.total})
    return found


def game_counts(cached_scan: dict | None) -> dict[str, int] | None:
    """Supported game folders (one per folder, whatever the number of mods) out of the installed Steam apps."""
    try:
        report = cached_scan["report"]
        supported = len({game["game_path"] for game in report["supported_games"] if game.get("game_path")})
        return {"supported": supported, "installed": int(report["steam_scan"]["apps_scanned"])}
    except (TypeError, KeyError, ValueError):
        return None


def parse_resolution(text: str) -> tuple[int, int] | None:
    match = _RESOLUTION.match(text or "")
    return (int(match[1]), int(match[2])) if match else None


def virtual_desktops(home: Path | None = None) -> list[dict[str, object]]:
    """Each Wine bottle that runs games in a virtual desktop, with that desktop's size."""
    from launch_setup import bottle_for

    home = Path.home() if home is None else home
    found = []
    for root in steam_layout.system_roots(sys.platform, home):
        try:
            bottle = bottle_for(root, home)
        except Exception:  # A bottle tool that is missing just has nothing to report.
            continue
        if bottle is None:
            continue
        desktop = bottle.read(r"HKCU\Software\Wine\Explorer").get("desktop")
        if not desktop:
            continue
        size = parse_resolution(bottle.read(r"HKCU\Software\Wine\Explorer\Desktops").get(desktop.lower(), ""))
        if size:
            found.append({"label": f"{bottle.prefix.name} virtual desktop", "width": size[0], "height": size[1]})
    return found
