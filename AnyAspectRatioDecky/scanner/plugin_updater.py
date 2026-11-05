import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse
from zipfile import BadZipFile, ZipFile

from scanner.mod_installer import (
    _repository_parts,
    copy_limited,
    open_url,
    read_url_limited,
)

PLUGIN_NAME = "AnyAspectRatio"
MAX_RELEASE_METADATA_BYTES = 2 * 1024 * 1024
MAX_PLUGIN_ZIP_BYTES = 64 * 1024 * 1024
MAX_PLUGIN_UNPACKED_BYTES = 128 * 1024 * 1024
RESTART_TIMEOUT_SECONDS = 15
_VERSION_PATTERN = re.compile(r"^v?(\d+(?:\.\d+)*)$")
_ASSET_PATTERN = re.compile(rf"^{PLUGIN_NAME}-v?(\d+(?:\.\d+)*)\.zip$")


def read_installed_version(plugin_dir: Path) -> str:
    try:
        manifest = json.loads((plugin_dir / "plugin.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return ""
    version = manifest.get("version") if isinstance(manifest, dict) else None
    return version if isinstance(version, str) else ""


def parse_version(value: str) -> tuple[int, ...] | None:
    match = _VERSION_PATTERN.match(value.strip())
    if match is None:
        return None
    parts = [int(part) for part in match.group(1).split(".")]
    while len(parts) > 1 and parts[-1] == 0:
        parts.pop()
    return tuple(parts)


def check_for_update(repository_url: str, installed_version: str) -> dict[str, object]:
    """Look up the latest release of the repository and report whether it is newer."""
    repository = _repository_parts(repository_url)
    api_url = f"https://api.github.com/repos/{repository}/releases/latest"
    try:
        release = json.loads(
            read_url_limited(api_url, MAX_RELEASE_METADATA_BYTES, "Release information")
        )
    except ValueError as error:
        raise ValueError("The repository returned unreadable release information") from error
    if not isinstance(release, dict):
        raise ValueError("The repository returned unreadable release information")

    latest_text = str(release.get("tag_name") or "")
    latest = parse_version(latest_text)
    if latest is None:
        raise ValueError(f"Latest release tag '{latest_text}' is not a version number")

    download_prefix = f"https://github.com/{repository}/releases/download/"
    download_url = ""
    for asset in release.get("assets") or []:
        if not isinstance(asset, dict):
            continue
        url = str(asset.get("browser_download_url") or "")
        name = str(asset.get("name") or "")
        if _ASSET_PATTERN.match(name) and url.startswith(download_prefix):
            download_url = url
            break

    installed = parse_version(installed_version)
    return {
        "installed_version": installed_version,
        "latest_version": latest_text.lstrip("v"),
        # An unknown installed version is treated as outdated so a broken install can be repaired.
        "update_available": installed is None or latest > installed,
        "download_url": download_url,
        "release_url": str(release.get("html_url") or ""),
    }


def _plugin_root_in_archive(names: list[str]) -> str:
    """Return the archive folder holding plugin.json ("" when it is at the top level)."""
    roots = [
        PurePosixPath(name).parent.as_posix()
        for name in names
        if PurePosixPath(name).name == "plugin.json"
    ]
    roots = ["" if root == "." else root for root in roots if root.count("/") == 0]
    if len(roots) != 1:
        raise ValueError("The downloaded file is not an AnyAspectRatio plugin zip")
    return roots[0]


def _extract_plugin_zip(archive: Path, plugin_dir: Path) -> str:
    try:
        with ZipFile(archive) as zip_file:
            members = [info for info in zip_file.infolist() if not info.is_dir()]
            root = _plugin_root_in_archive([info.filename for info in members])
            prefix = f"{root}/" if root else ""
            if sum(info.file_size for info in members) > MAX_PLUGIN_UNPACKED_BYTES:
                raise ValueError("The plugin zip is larger than expected")
            manifest = json.loads(zip_file.read(f"{prefix}plugin.json"))
            if manifest.get("name") != PLUGIN_NAME:
                raise ValueError("The downloaded file is not an AnyAspectRatio plugin zip")

            plugin_root = plugin_dir.resolve()
            planned: list[tuple[object, Path]] = []
            for info in members:
                if not info.filename.startswith(prefix):
                    continue
                relative = PurePosixPath(info.filename[len(prefix):])
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError("The plugin zip contains an unsafe path")
                target = plugin_root.joinpath(*relative.parts)
                if plugin_root not in target.resolve().parents:
                    raise ValueError("The plugin zip contains an unsafe path")
                planned.append((info, target))

            for info, target in planned:
                target.parent.mkdir(parents=True, exist_ok=True)
                with zip_file.open(info) as source, target.open("wb") as output:
                    copy_limited(source, output, MAX_PLUGIN_UNPACKED_BYTES, "Plugin file")
            return str(manifest.get("version") or "")
    except BadZipFile as error:
        raise ValueError("The downloaded file is not a valid zip") from error


def restart_plugin_loader() -> bool:
    try:
        result = subprocess.run(
            ["systemctl", "restart", "plugin_loader"],
            check=False,
            timeout=RESTART_TIMEOUT_SECONDS,
            capture_output=True,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0


def install_update(
    repository_url: str,
    download_url: str,
    plugin_dir: Path,
    fallback_dir: Path,
) -> dict[str, object]:
    """Download the release zip and overlay it onto the plugin folder.

    When the plugin folder cannot be written (Decky runs plugins without root unless
    asked), the zip is saved to fallback_dir so it can be installed from Decky's
    developer settings instead.
    """
    repository = _repository_parts(repository_url)
    if not download_url.startswith(f"https://github.com/{repository}/releases/download/"):
        raise ValueError("The update must come from the configured repository's releases")

    with tempfile.TemporaryDirectory(prefix="aar-update-") as scratch:
        archive = Path(scratch) / Path(urlparse(download_url).path).name
        with open_url(download_url) as response, archive.open("wb") as output:
            copy_limited(response, output, MAX_PLUGIN_ZIP_BYTES, "Plugin update")
        try:
            version = _extract_plugin_zip(archive, plugin_dir)
        except PermissionError:
            fallback_dir.mkdir(parents=True, exist_ok=True)
            saved = fallback_dir / archive.name
            shutil.copyfile(archive, saved)
            return {
                "installed": False,
                "saved_zip": str(saved),
                "file_name": saved.name,
                "size_bytes": saved.stat().st_size,
            }
    return {"installed": True, "version": version, "restarted": restart_plugin_loader()}
