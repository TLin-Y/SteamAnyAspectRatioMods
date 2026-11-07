import json
import os
import re
import shutil
import tempfile
from dataclasses import asdict, dataclass, fields, replace
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse

DEFAULT_CATALOG_PATH = "AnyAspectRatioDecky/catalog.json"
REF_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._/-]{0,99}\Z")
REPOSITORY_SEGMENT = re.compile(r"[A-Za-z0-9._-]+\Z")


@dataclass(frozen=True)
class Settings:
    """User overrides; an empty string means "use the catalog/bundled default"."""

    catalog_repository_url: str = ""
    catalog_ref: str = ""
    catalog_path: str = ""
    local_archive_dir: str = ""
    offline_mode: bool = False

    def source_key(self) -> tuple[str, str, str]:
        return (self.catalog_repository_url, self.catalog_ref, self.catalog_path)


def validate_settings(settings: Settings) -> Settings:
    url = settings.catalog_repository_url.strip().rstrip("/")
    if url.endswith(".git"):
        url = url[: -len(".git")]
    if url:
        parsed = urlparse(url)
        segments = parsed.path.strip("/").split("/")
        if (
            parsed.scheme != "https"
            or parsed.hostname != "github.com"
            or parsed.query
            or parsed.fragment
            or len(segments) != 2
            or not all(REPOSITORY_SEGMENT.fullmatch(segment) for segment in segments)
        ):
            raise ValueError("Repository must look like https://github.com/<owner>/<repo>")

    ref = settings.catalog_ref.strip()
    if ref and (not REF_PATTERN.fullmatch(ref) or ".." in ref or ref.endswith("/")):
        raise ValueError("Branch, tag, or commit contains unsupported characters")

    catalog_path = settings.catalog_path.strip().strip("/")
    if catalog_path and (
        "\\" in catalog_path
        or ":" in catalog_path
        or any(part in ("", ".", "..") for part in catalog_path.split("/"))
        or not PurePosixPath(catalog_path).name.lower().endswith(".json")
    ):
        raise ValueError("Catalog path must be a relative path to a .json file")

    local_dir = settings.local_archive_dir.strip()
    if local_dir:
        if "\0" in local_dir or not os.path.isabs(local_dir):
            raise ValueError("Local mod folder must be an absolute path")
        if not Path(local_dir).is_dir():
            raise ValueError("Local mod folder does not exist")
        local_dir = str(Path(local_dir))
    return replace(
        settings,
        catalog_repository_url=url,
        catalog_ref=ref,
        catalog_path=catalog_path,
        local_archive_dir=local_dir,
    )


def write_json_atomic(path: Path, document: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(dir=path.parent, suffix=".tmp")
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(document, stream, indent=2)
        os.replace(temporary_name, path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def read_json(path: Path) -> object | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


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
        values = {
            key: value
            for key, value in document.items()
            if key in field_types and isinstance(value, type(field_types[key]))
        }
        return Settings(**values)

    def save_settings(self, settings: Settings) -> None:
        write_json_atomic(self.settings_path, asdict(settings))

    def load_applied_hashes(self) -> dict[str, dict[str, str]]:
        try:
            document = json.loads(self.applied_hashes_path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return {}
        except (OSError, ValueError) as error:
            raise ValueError(f"Unable to read applied mod hashes: {error}") from error
        if not isinstance(document, dict) or not all(
            isinstance(path, str)
            and isinstance(mods, dict)
            and all(
                isinstance(mod_id, str)
                and isinstance(digest, str)
                and re.fullmatch(r"[0-9a-f]{64}", digest) is not None
                for mod_id, digest in mods.items()
            )
            for path, mods in document.items()
        ):
            raise ValueError("Applied mod hashes are invalid")
        return document

    def set_applied_hash(self, game_path: str, mod_id: str, digest: str | None) -> None:
        hashes = self.load_applied_hashes()
        if digest is not None:
            if re.fullmatch(r"[0-9a-f]{64}", digest) is None:
                raise ValueError("Invalid applied mod SHA-256")
            hashes.setdefault(game_path, {})[mod_id] = digest
        elif game_path in hashes:
            hashes[game_path].pop(mod_id, None)
            if not hashes[game_path]:
                del hashes[game_path]
        else:
            return
        write_json_atomic(self.applied_hashes_path, hashes)

    def load_matched_games(self) -> dict[str, dict[str, str]]:
        try:
            document = json.loads(self.matched_games_path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return {}
        except (OSError, ValueError) as error:
            raise ValueError(f"Unable to read matched games: {error}") from error
        if not isinstance(document, dict) or not all(
            isinstance(group, str)
            and isinstance(choice, dict)
            and isinstance(choice.get("mod_id"), str)
            and isinstance(choice.get("game_path"), str)
            for group, choice in document.items()
        ):
            raise ValueError("Matched games are invalid")
        return document

    def set_matched_game(self, group: str, mod_id: str, game_path: str) -> None:
        choices = self.load_matched_games()
        choices[group] = {"mod_id": mod_id, "game_path": game_path}
        write_json_atomic(self.matched_games_path, choices)

    def remove_matched_game(self, mod_id: str, game_path: str) -> None:
        choices = self.load_matched_games()
        remaining = {
            group: choice for group, choice in choices.items()
            if choice["mod_id"] != mod_id or choice["game_path"] != game_path
        }
        if remaining != choices:
            write_json_atomic(self.matched_games_path, remaining)

    def clear_matched_game(self, group: str, mod_id: str, game_path: str) -> None:
        choices = self.load_matched_games()
        choice = choices.get(group)
        if not choice or choice["mod_id"] != mod_id or choice["game_path"] != game_path:
            raise ValueError("Saved game selection has changed. Scan again and retry.")
        del choices[group]
        write_json_atomic(self.matched_games_path, choices)

    def catalog_meta(self) -> dict[str, object] | None:
        meta = read_json(self.catalog_meta_path)
        return meta if isinstance(meta, dict) else None

    def clear_archive_cache(self) -> None:
        shutil.rmtree(self.archive_cache_dir, ignore_errors=True)
        self.archive_cache_dir.mkdir(parents=True, exist_ok=True)

    def clear_catalog_cache(self) -> None:
        self.catalog_path.unlink(missing_ok=True)
        self.catalog_meta_path.unlink(missing_ok=True)

    def clear_pack(self) -> None:
        shutil.rmtree(self.pack_dir, ignore_errors=True)

    def clear_local_data(self) -> None:
        """Delete the pack and every cache; settings and logs are kept."""
        self.clear_pack()
        shutil.rmtree(self.cache_dir, ignore_errors=True)
        self.ensure_directories()

    def pack_stats(self) -> dict[str, int]:
        files = [path for path in self.pack_dir.rglob("*") if path.is_file()]
        return {"file_count": len(files), "size_bytes": sum(p.stat().st_size for p in files)}

    def save_last_scan(self, report: dict[str, object]) -> None:
        write_json_atomic(self.last_scan_path, {"scanned_at": utc_now(), "report": report})

    def load_last_scan(self) -> dict[str, object] | None:
        document = read_json(self.last_scan_path)
        if isinstance(document, dict) and isinstance(document.get("report"), dict):
            return document
        return None
