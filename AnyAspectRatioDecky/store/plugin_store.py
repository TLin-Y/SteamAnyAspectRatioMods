import json
import os
import re
import shutil
import tempfile
from dataclasses import asdict, dataclass, fields
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
    return Settings(url, ref, catalog_path, local_dir)


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
        self.log_dir = data_dir / "logs"
        self.log_path = self.log_dir / "plugin.log"

    def ensure_directories(self) -> None:
        for directory in (self.cache_dir, self.archive_cache_dir, self.log_dir):
            directory.mkdir(parents=True, exist_ok=True)

    def load_settings(self) -> Settings:
        document = read_json(self.settings_path)
        if not isinstance(document, dict):
            return Settings()
        known = {item.name for item in fields(Settings)}
        values = {
            key: value
            for key, value in document.items()
            if key in known and isinstance(value, str)
        }
        return Settings(**values)

    def save_settings(self, settings: Settings) -> None:
        write_json_atomic(self.settings_path, asdict(settings))

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
