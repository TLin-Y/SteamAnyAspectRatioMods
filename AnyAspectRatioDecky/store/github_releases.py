import json
import re

from helper.errors import PluginError
from store.mod_sources import repository_path
from utils import sizes
from utils.network import read_url_limited

MAX_RELEASE_METADATA_BYTES = 2 * sizes.MIB
_VERSION_PATTERN = re.compile(r"^v?(\d+(?:\.\d+)*)$")


def parse_version(value: str) -> tuple[int, ...] | None:
    match = _VERSION_PATTERN.match(value.strip())
    if match is None:
        return None
    parts = [int(part) for part in match.group(1).split(".")]
    while len(parts) > 1 and parts[-1] == 0:
        parts.pop()
    return tuple(parts)


def latest_release(repository_url: str) -> tuple[str, dict[str, object], str]:
    repository = repository_path(repository_url)
    api_url = f"https://api.github.com/repos/{repository}/releases/latest"
    try:
        release = json.loads(
            read_url_limited(api_url, MAX_RELEASE_METADATA_BYTES, "Release information")
        )
    except ValueError as error:
        raise PluginError("The repository returned unreadable release information") from error
    if not isinstance(release, dict):
        raise PluginError("The repository returned unreadable release information")

    latest_text = str(release.get("tag_name") or "")
    latest = parse_version(latest_text)
    if latest is None:
        raise PluginError(f"Latest release tag '{latest_text}' is not a version number")
    return repository, release, latest_text
