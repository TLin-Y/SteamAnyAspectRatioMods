import os
from dataclasses import asdict, dataclass, fields, replace
from datetime import UTC, datetime
from pathlib import Path

from helper import errors
from helper.errors import PluginError
from helper.validator import Validator
from utils import folders, text_files

DEFAULT_CATALOG_PATH = "AnyAspectRatioDecky/catalog.json"


@dataclass(frozen=True)
class Settings:
    catalog_repository_url: str = ""
    catalog_ref: str = ""
    catalog_path: str = ""
    local_archive_dir: str = ""
    offline_mode: bool = False
    ini_resolution_width: int = 1280
    ini_resolution_height: int = 800
    ini_resolution_update_pending: bool = False
    # Folders outside Steam: each one, and each folder directly inside it, is checked for catalogued games.
    custom_game_dirs: tuple[str, ...] = ()

    def source_key(self) -> tuple[str, str, str]:
        return (self.catalog_repository_url, self.catalog_ref, self.catalog_path)


def validate_settings(settings: Settings) -> Settings:
    for label, dimension in (
        ("INI resolution width", settings.ini_resolution_width),
        ("INI resolution height", settings.ini_resolution_height),
    ):
        if not Validator.Settings.resolution(dimension):
            raise PluginError(f"{label} must be an integer between 1 and {Validator.Settings.MAX_RESOLUTION}")

    url = settings.catalog_repository_url.strip().rstrip("/")
    if url.endswith(".git"):
        url = url[: -len(".git")]
    if url and not Validator.GitHub.repository(url):
        raise PluginError("Repository must look like https://github.com/<owner>/<repo>")

    ref = settings.catalog_ref.strip()
    if ref and not Validator.GitHub.ref(ref):
        raise PluginError("Branch, tag, or commit contains unsupported characters")

    catalog_path = settings.catalog_path.strip().strip("/")
    if catalog_path and (
        not Validator.Paths.is_relative(catalog_path) or not Validator.Files.has_extension(catalog_path, ".json")
    ):
        raise PluginError("Catalog path must be a relative path to a .json file")

    local_dir = settings.local_archive_dir.strip()
    if local_dir:
        local_dir = _existing_folder(local_dir, "Local mod folder")
    return replace(
        settings,
        catalog_repository_url=url,
        catalog_ref=ref,
        catalog_path=catalog_path,
        local_archive_dir=local_dir,
    )


MAX_CUSTOM_GAME_DIRS = 32


def _existing_folder(folder: str, label: str) -> str:
    if "\0" in folder or not os.path.isabs(folder):
        raise PluginError(f"{label} must be an absolute path")
    if not Path(folder).is_dir():
        raise PluginError(f"{label} does not exist")
    return str(Path(folder))


def validate_custom_game_dir(folder: str) -> str:
    return _existing_folder(folder.strip(), "Game folder")


def read_json(path: Path) -> object | None:
    try:
        return text_files.load_json(path)
    except (OSError, ValueError):
        return None


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat()


class PluginStore:
    """Persistent plugin data: settings, cached catalog, cached archives, scan results, logs."""

    def __init__(self, data_dir: Path) -> None:
        self.data_dir = data_dir
        self.settings_path = data_dir / "settings.json"
        self.cache_dir = data_dir / "cache"
        self.catalog_path = self.cache_dir / "catalog.json"
        self.catalog_meta_path = self.cache_dir / "catalog.meta.json"
        self.archive_cache_dir = self.cache_dir / "archives"
        self.pack_dir = data_dir / "pack"
        self.author_downloads_dir = data_dir / "author_downloads"
        self.local_index_path = self.cache_dir / "local_archives.json"
        self.last_scan_path = self.cache_dir / "last_scan.json"
        self.applied_hashes_path = self.data_dir / "applied_hashes.json"
        self.matched_games_path = self.data_dir / "matched_games.json"
        self.log_dir = data_dir / "logs"
        self.log_path = self.log_dir / "plugin.log"

    def ensure_directories(self) -> None:
        for directory in (self.cache_dir, self.archive_cache_dir, self.log_dir):
            directory.mkdir(parents=True, exist_ok=True)

    def load_settings(self) -> Settings:
        document = read_json(self.settings_path)
        if not isinstance(document, dict):
            return Settings()
        field_types = {item.name: item.default for item in fields(Settings)}
        folders_value = document.pop("custom_game_dirs", None)
        values = {
            key: value
            for key, value in document.items()
            if key in field_types and type(value) is type(field_types[key])
        }
        if isinstance(folders_value, list) and all(isinstance(folder, str) for folder in folders_value):
            values["custom_game_dirs"] = tuple(folders_value)
        for key in ("ini_resolution_width", "ini_resolution_height"):
            if key in values and not Validator.Settings.resolution(values[key]):
                del values[key]
        return Settings(**values)

    def save_settings(self, settings: Settings) -> None:
        text_files.write_json_atomic(self.settings_path, asdict(settings))

    def load_applied_hashes(self) -> dict[str, dict[str, str]]:
        if not self.applied_hashes_path.exists():
            return {}
        with errors.wrap("Unable to read applied mod hashes"):
            document = text_files.load_json(self.applied_hashes_path)
        if not isinstance(document, dict) or not all(
            isinstance(path, str)
            and isinstance(mods, dict)
            and all(
                isinstance(mod_id, str)
                and isinstance(digest, str)
                and Validator.Hash.sha256(digest)
                for mod_id, digest in mods.items()
            )
            for path, mods in document.items()
        ):
            raise PluginError("Applied mod hashes are invalid")
        return document

    def set_applied_hash(self, game_path: str, mod_id: str, digest: str | None) -> None:
        hashes = self.load_applied_hashes()
        if digest is not None:
            if not Validator.Hash.sha256(digest):
                raise PluginError("Invalid applied mod SHA-256")
            hashes.setdefault(game_path, {})[mod_id] = digest
        elif game_path in hashes:
            hashes[game_path].pop(mod_id, None)
            if not hashes[game_path]:
                del hashes[game_path]
        else:
            return
        text_files.write_json_atomic(self.applied_hashes_path, hashes)

    def load_matched_games(self) -> dict[str, dict[str, str]]:
        if not self.matched_games_path.exists():
            return {}
        with errors.wrap("Unable to read matched games"):
            document = text_files.load_json(self.matched_games_path)
        if not isinstance(document, dict) or not all(
            isinstance(group, str)
            and isinstance(choice, dict)
            and isinstance(choice.get("mod_id"), str)
            and isinstance(choice.get("game_path"), str)
            for group, choice in document.items()
        ):
            raise PluginError("Matched games are invalid")
        return document

    def set_matched_game(self, group: str, mod_id: str, game_path: str) -> None:
        choices = self.load_matched_games()
        choices[group] = {"mod_id": mod_id, "game_path": game_path}
        text_files.write_json_atomic(self.matched_games_path, choices)

    def remove_matched_game(self, mod_id: str, game_path: str) -> None:
        choices = self.load_matched_games()
        remaining = {
            group: choice for group, choice in choices.items()
            if choice["mod_id"] != mod_id or choice["game_path"] != game_path
        }
        if remaining != choices:
            text_files.write_json_atomic(self.matched_games_path, remaining)

    def clear_matched_game(self, group: str, mod_id: str, game_path: str) -> None:
        choices = self.load_matched_games()
        choice = choices.get(group)
        if not choice or choice["mod_id"] != mod_id or choice["game_path"] != game_path:
            raise PluginError("Saved game selection has changed. Scan again and retry.")
        del choices[group]
        text_files.write_json_atomic(self.matched_games_path, choices)

    def catalog_meta(self) -> dict[str, object] | None:
        meta = read_json(self.catalog_meta_path)
        return meta if isinstance(meta, dict) else None

    def clear_archive_cache(self) -> None:
        folders.remove_tree(self.archive_cache_dir)
        self.archive_cache_dir.mkdir(parents=True, exist_ok=True)

    def clear_catalog_cache(self) -> None:
        self.catalog_path.unlink(missing_ok=True)
        self.catalog_meta_path.unlink(missing_ok=True)

    def clear_pack(self) -> None:
        folders.remove_tree(self.pack_dir)

    def clear_last_scan(self) -> None:
        self.last_scan_path.unlink(missing_ok=True)

    def clear_local_index(self) -> None:
        self.local_index_path.unlink(missing_ok=True)

    def clear_local_data(self) -> None:
        """Delete the pack and every cache; settings and logs are kept."""
        self.clear_pack()
        folders.remove_tree(self.cache_dir)
        self.ensure_directories()

    def clear_author_downloads(self) -> None:
        folders.remove_tree(self.author_downloads_dir)
        self.clear_last_scan()

    def pack_stats(self) -> dict[str, int]:
        file_count, size_bytes = folders.stats(self.pack_dir)
        return {"file_count": file_count, "size_bytes": size_bytes}

    def disk_usage(self) -> int:
        return folders.stats(self.data_dir)[1]

    def save_last_scan(self, report: dict[str, object]) -> None:
        text_files.write_json_atomic(self.last_scan_path, {"scanned_at": utc_now(), "report": report})

    def load_last_scan(self) -> dict[str, object] | None:
        document = read_json(self.last_scan_path)
        if isinstance(document, dict) and isinstance(document.get("report"), dict):
            return document
        return None
