"""Decky Plugin Store build: the plugin does not update itself, the store delivers updates."""

from pathlib import Path

from helper.errors import PluginError

STORE_UPDATES = "Plugin updates are delivered through the Decky Plugin Store."


def read_installed_version(plugin_dir: Path) -> str:
    raise PluginError(STORE_UPDATES)


def check_for_update(repository_url: str, installed_version: str, asset_pattern: object = None) -> dict[str, object]:
    raise PluginError(STORE_UPDATES)


def install_update(
    repository_url: str,
    download_url: str,
    plugin_dir: Path,
    fallback_dir: Path,
) -> dict[str, object]:
    raise PluginError(STORE_UPDATES)
