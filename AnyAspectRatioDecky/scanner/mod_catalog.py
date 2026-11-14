import json
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse

_APP_ID_PATTERN = re.compile(r"\d+\Z")
_MOD_ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]{0,63}\Z")
_WINE_OVERRIDE_PATTERN = re.compile(r"[a-z0-9_,-]+=[a-z,]+\Z")
_REQUIRED_FIELDS = ("id", "name", "steam_app_id", "install_dir")
UNSAFE_PATH_CHARACTERS = ("\\", ":", "\0")
_DISPLAY_TAG_PATTERN = re.compile(r"[a-z][a-z0-9_-]{0,31}\Z")
_SOURCE_KINDS = ("archive", "directory", "files")



@dataclass(frozen=True)
class ModSourceFile:
    path: str
    destination: str


@dataclass(frozen=True)
class ModSource:
    kind: str
    path: str | None
    destination: str
    files: tuple[ModSourceFile, ...]
    repository_url: str
    repository_ref: str
    sha256: str = ""
    # files/directory sources: SHA-256 per installed path, checked after each download.
    file_hashes: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True)
class ModCredit:
    name: str
    url: str
    support: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True)
class LauncherReplacement:
    source: str
    target: str
    backup: str


@dataclass(frozen=True)
class IniResolutionPair:
    section: str
    width: str
    height: str
    kind: str


@dataclass(frozen=True)
class ModBackup:
    target: str
    backup: str


@dataclass(frozen=True)
class ModDefinition:
    mod_id: str
    name: str
    steam_app_id: str
    install_dir: str
    executable: str
    source: ModSource
    files: tuple[str, ...]
    ini_file: str | None
    wine_dll_overrides: tuple[str, ...]
    credits: tuple[ModCredit, ...] = ()
    manual: bool = False
    executable_alternatives: tuple[str, ...] = ()
    backup_file: str | None = None
    offline_only: bool = False
    launcher_replacement: LauncherReplacement | None = None
    display_tags: tuple[str, ...] = ()
    ini_resolution: tuple[IniResolutionPair, ...] = ()
    backups: tuple[ModBackup, ...] = ()

    @property
    def file_backups(self) -> tuple[ModBackup, ...]:
        if self.backup_file is not None:
            return (ModBackup(self.files[0], self.backup_file),)
        return self.backups


def _https_url(value: object) -> bool:
    return isinstance(value, str) and urlparse(value).scheme == "https"


_INVALID_CREDITS = (
    "Catalog credits must be a list of objects with a name, an optional https url, "
    "and optional support links (label and https url)"
)


def _parse_credits(raw: object) -> tuple[ModCredit, ...]:
    if raw is None:
        return ()
    if not isinstance(raw, list):
        raise ValueError(_INVALID_CREDITS)
    credits = []
    for item in raw:
        item = item if isinstance(item, dict) else {}
        name = item.get("name")
        url = item.get("url")
        support = item.get("support", [])
        if (
            not isinstance(name, str)
            or not name.strip()
            or (url is not None and not _https_url(url))
            or not isinstance(support, list)
            or not all(
                isinstance(link, dict)
                and isinstance(link.get("label"), str)
                and link["label"].strip()
                and _https_url(link.get("url"))
                for link in support
            )
        ):
            raise ValueError(_INVALID_CREDITS)
        credits.append(
            ModCredit(
                name.strip(),
                url or "",
                tuple((link["label"].strip(), link["url"]) for link in support),
            )
        )
    return tuple(credits)


def summarize_credits(mods: tuple[ModDefinition, ...]) -> list[dict[str, object]]:
    """Group mods by author, the repository owner first, then most mods first."""
    authors: dict[ModCredit, int] = {}
    for mod in mods:
        for credit in mod.credits:
            authors[credit] = authors.get(credit, 0) + 1
    return [
        {
            "name": credit.name,
            "url": credit.url,
            "support": [{"label": label, "url": url} for label, url in credit.support],
            "mod_count": count,
        }
        for credit, count in sorted(
            authors.items(), key=lambda item: (
                item[0].name != "TLin-Y",
                -item[1],
                item[0].name.lower(),
            )
        )
    ]


def _require_string(entry: dict[str, object], key: str, mod_id: str) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(
            f'Mod catalog entry {mod_id or "unknown"} is missing {key}'
        )
    return value


def _validate_relative_path(path: str, mod_id: str) -> None:
    parsed = PurePosixPath(path)
    if (
        not path
        or parsed.is_absolute()
        or any(character in path for character in UNSAFE_PATH_CHARACTERS)
        or any(part in ("", ".", "..") for part in path.split("/"))
    ):
        raise ValueError(
            f"Mod catalog entry {mod_id} has an unsafe relative path: {path}"
        )


def _parse_source(
    entry: dict[str, object],
    mod_id: str,
    repository_url: str,
    repository_ref: str,
) -> ModSource:
    raw_source = entry.get("source")
    if not isinstance(raw_source, dict):
        raise ValueError(f"Mod catalog entry {mod_id} has invalid source metadata")

    kind = raw_source.get("kind")
    if not isinstance(kind, str) or kind not in _SOURCE_KINDS:
        raise ValueError(f"Mod catalog entry {mod_id} has an unsupported source kind")

    path = None
    destination = raw_source.get("destination", "")
    if not isinstance(destination, str):
        raise ValueError(f"Mod catalog entry {mod_id} has invalid source metadata")
    if destination:
        _validate_relative_path(destination, mod_id)

    sha256 = raw_source.get("sha256", "")
    if not isinstance(sha256, str) or (sha256 and not _is_sha256(sha256)):
        raise ValueError(f"Mod catalog entry {mod_id} has invalid source metadata")

    source_files: tuple[ModSourceFile, ...] = ()
    if kind in ("archive", "directory"):
        path = _require_string(raw_source, "path", mod_id)
        _validate_relative_path(path, mod_id)
    else:
        raw_files = raw_source.get("files")
        if not isinstance(raw_files, list) or not raw_files:
            raise ValueError(f"Mod catalog entry {mod_id} has invalid source metadata")
        parsed_files = []
        for source_file in raw_files:
            if not isinstance(source_file, dict):
                raise ValueError(f"Mod catalog entry {mod_id} has invalid source metadata")
            source_path = _require_string(source_file, "path", mod_id)
            target_path = _require_string(
                source_file,
                "destination",
                mod_id,
            )
            _validate_relative_path(source_path, mod_id)
            _validate_relative_path(target_path, mod_id)
            parsed_files.append(ModSourceFile(source_path, target_path))
        source_files = tuple(parsed_files)

    raw_hashes = raw_source.get("file_hashes", {})
    if (
        not isinstance(raw_hashes, dict)
        or (raw_hashes and kind not in ("files", "directory"))
        or not all(isinstance(value, str) and _is_sha256(value) for value in raw_hashes.values())
    ):
        raise ValueError(f"Mod catalog entry {mod_id} has invalid source metadata")

    return ModSource(
        kind=kind,
        path=path,
        destination=destination,
        files=source_files,
        repository_url=repository_url,
        repository_ref=repository_ref,
        sha256=sha256,
        file_hashes=tuple(sorted(raw_hashes.items())),
    )


def _is_sha256(value: str) -> bool:
    return len(value) == 64 and all(character in "0123456789abcdef" for character in value)


def _unique_paths(paths: list[str], mod_id: str, seen: set[str]) -> None:
    """Validate relative paths and add them to seen; a repeat is a catalog error."""
    for path in paths:
        _validate_relative_path(path, mod_id)
        if path in seen:
            raise ValueError(f"Mod catalog entry {mod_id} has duplicate file: {path}")
        seen.add(path)


def _parse_executables(entry: dict, mod_id: str, manual: bool) -> tuple[str, tuple[str, ...], set[str]]:
    # A manual mod is only listed and explained, so its executable may be unknown.
    executable = entry.get("executable", "") if manual else _require_string(entry, "executable", mod_id)
    if not isinstance(executable, str):
        raise ValueError(f"Mod catalog entry {mod_id} is missing executable")
    _validate_relative_path(entry["install_dir"], mod_id)
    if executable:
        _validate_relative_path(executable, mod_id)
    alternatives = entry.get("executable_alternatives", [])
    if not isinstance(alternatives, list) or not all(isinstance(path, str) and path for path in alternatives):
        raise ValueError(f"Mod catalog entry {mod_id} is missing executable_alternatives")
    seen = {executable} if executable else set()
    _unique_paths(alternatives, mod_id, seen)
    return executable, tuple(alternatives), seen


def _parse_ini_resolution(
    entry: dict, mod_id: str, ini_file: str | None, manual: bool,
) -> tuple[IniResolutionPair, ...]:
    # Single "WIDTHxHEIGHT" keys have their own list: plugins up to 1.8.0 reject an unknown
    # ini_resolution kind (refusing the whole catalog) but ignore unknown entry keys.
    raw_pairs = entry.get("ini_resolution", [])
    raw_strings = entry.get("ini_resolution_strings", [])
    for raw in (raw_pairs, raw_strings):
        if not isinstance(raw, list) or (raw and (not ini_file or manual)):
            raise ValueError(f"Mod {mod_id} has invalid ini_resolution metadata")
    pairs: list[IniResolutionPair] = []
    seen_keys: set[tuple[str, str]] = set()
    candidates = [(pair, ("width", "height")) for pair in raw_pairs]
    candidates += [(pair, ("key",)) for pair in raw_strings]
    for pair, names in candidates:
        fields = {"section", *names} | ({"kind"} if len(names) == 2 else set())
        if (
            not isinstance(pair, dict)
            or set(pair) != fields
            or not all(isinstance(pair[key], str) for key in pair)
            or not re.fullmatch(r"[A-Za-z0-9 _.-]{1,80}", pair["section"], re.ASCII)
            or not all(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,79}", pair[name], re.ASCII) for name in names)
            or pair.get("kind", "pixels") not in ("pixels", "ratio")
        ):
            raise ValueError(f"Mod {mod_id} has invalid ini_resolution metadata")
        keys = {(pair["section"].casefold(), pair[name].casefold()) for name in names}
        if len(keys) != len(names) or keys & seen_keys:
            raise ValueError(f"Mod {mod_id} has duplicate ini_resolution keys")
        seen_keys.update(keys)
        if len(names) == 1:
            pairs.append(IniResolutionPair(pair["section"], pair["key"], pair["key"], "pixels_string"))
        else:
            pairs.append(IniResolutionPair(pair["section"], pair["width"], pair["height"], pair["kind"]))
    return tuple(pairs)


def _parse_backup_file(entry: dict, mod_id: str, files: tuple[str, ...], reserved: set[str]) -> str | None:
    """One-file replacement: the original is renamed to backup_file beside it."""
    backup_file = entry.get("backup_file")
    if backup_file is None:
        return None
    if not isinstance(backup_file, str) or len(files) != 1:
        raise ValueError(f"Mod catalog entry {mod_id} has an invalid backup_file")
    _validate_relative_path(backup_file, mod_id)
    if (
        PurePosixPath(files[0]).parent != PurePosixPath(backup_file).parent
        or backup_file.casefold() in {path.casefold() for path in reserved}
    ):
        raise ValueError(f"Mod catalog entry {mod_id} has an invalid backup_file")
    return backup_file


def _parse_backups(
    entry: dict, mod_id: str, files: tuple[str, ...], reserved: set[str],
    ini_file: str | None, has_backup_file: bool, manual: bool,
) -> tuple[ModBackup, ...]:
    """Several replaced originals, each renamed to its backup beside the target."""
    raw_backups = entry.get("backups", [])
    if not isinstance(raw_backups, list) or (raw_backups and (has_backup_file or manual)):
        raise ValueError(f"Mod {mod_id} has invalid backups metadata")
    taken = {path.casefold() for path in reserved}
    taken.update(  # An ASI loader writes <name>.log beside each .asi plugin.
        str(PurePosixPath(path).with_suffix(".log")).casefold()
        for path in files if PurePosixPath(path).suffix.lower() == ".asi"
    )
    backups = []
    seen_targets: set[str] = set()
    for item in raw_backups:
        if (
            not isinstance(item, dict)
            or set(item) != {"target", "backup"}
            or not all(isinstance(value, str) for value in item.values())
        ):
            raise ValueError(f"Mod {mod_id} has invalid backups metadata")
        target, backup = item["target"], item["backup"]
        _validate_relative_path(target, mod_id)
        _validate_relative_path(backup, mod_id)
        if (
            target not in files
            or target == ini_file
            or target.casefold() in seen_targets
            or backup.casefold() in taken
            or PurePosixPath(target).parent != PurePosixPath(backup).parent
        ):
            raise ValueError(f"Mod {mod_id} has invalid backups paths")
        seen_targets.add(target.casefold())
        taken.add(backup.casefold())
        backups.append(ModBackup(target, backup))
    return tuple(backups)


def _parse_display_tags(entry: dict, mod_id: str) -> tuple[str, ...]:
    tags = entry.get("display_tags", [])
    if (
        not isinstance(tags, list)
        or not all(isinstance(tag, str) and _DISPLAY_TAG_PATTERN.fullmatch(tag) for tag in tags)
        or len(set(tags)) != len(tags)
    ):
        raise ValueError(f"Mod {mod_id} has invalid display_tags")
    return tuple(tags)


def _parse_launcher(
    entry: dict, mod_id: str, files: set[str], reserved: set[str], executable: str,
    *, allowed: bool,
) -> LauncherReplacement | None:
    """An offline-only mod without other backups may swap the game's launcher for one of its files."""
    raw = entry.get("launcher_replacement")
    if raw is None:
        return None
    if not isinstance(raw, dict) or set(raw) != {"source", "target", "backup"}:
        raise ValueError(f"Mod {mod_id} has invalid launcher_replacement metadata")
    for key in ("source", "target", "backup"):
        _validate_relative_path(_require_string(raw, key, mod_id), mod_id)
    launcher = LauncherReplacement(**raw)
    if (
        not allowed
        or len({launcher.source, launcher.target, launcher.backup}) != 3
        or launcher.source not in reserved
        or launcher.target in files
        or launcher.target == executable
        or launcher.backup in reserved
        or PurePosixPath(launcher.target).parent != PurePosixPath(launcher.backup).parent
    ):
        raise ValueError(f"Mod {mod_id} has invalid launcher_replacement paths")
    return launcher


def _parse_mod(
    entry: object,
    index: int,
    repository_url: str,
    repository_ref: str,
    default_credits: tuple[ModCredit, ...] = (),
) -> ModDefinition:
    if not isinstance(entry, dict):
        raise ValueError(f"Mod catalog entry {index} must be an object")
    mod_id = entry.get("id")
    mod_id = mod_id if isinstance(mod_id, str) else "unknown"
    if not _MOD_ID_PATTERN.fullmatch(mod_id):
        raise ValueError(f"Mod catalog entry {index} must be an object")
    for key in _REQUIRED_FIELDS:
        _require_string(entry, key, mod_id)
    if not _APP_ID_PATTERN.fullmatch(entry["steam_app_id"]):
        raise ValueError(f"Mod catalog entry {mod_id} has an invalid Steam app ID")
    manual = entry.get("manual", False)
    if not isinstance(manual, bool):
        raise ValueError(f"Mod catalog entry {mod_id} is missing manual")

    executable, alternatives, executables = _parse_executables(entry, mod_id, manual)
    source = _parse_source(entry, mod_id, repository_url, repository_ref)
    ini_file = entry.get("ini_file")
    if ini_file is not None:
        if not isinstance(ini_file, str) or not ini_file:
            raise ValueError(f"Mod catalog entry {mod_id} is missing ini_file")
        _validate_relative_path(ini_file, mod_id)
    ini_resolution = _parse_ini_resolution(entry, mod_id, ini_file, manual)

    raw_files = entry.get("files")
    if not isinstance(raw_files, list) or not raw_files or not all(isinstance(p, str) and p for p in raw_files):
        raise ValueError(f"Mod catalog entry {mod_id} has an invalid files list")
    files = tuple(raw_files)
    file_set: set[str] = set()
    _unique_paths(raw_files, mod_id, file_set)
    reserved = file_set | executables
    backup_file = _parse_backup_file(entry, mod_id, files, reserved)
    backups = _parse_backups(entry, mod_id, files, reserved, ini_file, backup_file is not None, manual)
    if ini_file is not None and ini_file not in file_set:
        raise ValueError(f"Mod catalog entry {mod_id} INI file must be included in files")
    if source.kind == "files" and {item.destination for item in source.files} != file_set:
        raise ValueError(f"Mod catalog entry {mod_id} source files do not match its installed file list")
    if not {path for path, _ in source.file_hashes} <= file_set:
        raise ValueError(f"Mod catalog entry {mod_id} has invalid source metadata")
    offline_only = entry.get("offline_only", False)
    if not isinstance(offline_only, bool):
        raise ValueError(f"Mod {mod_id} offline_only must be a boolean")
    display_tags = _parse_display_tags(entry, mod_id)
    launcher = _parse_launcher(
        entry, mod_id, file_set, reserved, executable,
        allowed=offline_only and not manual and backup_file is None and not backups,
    )
    overrides = entry.get("wine_dll_overrides")
    if not isinstance(overrides, list) or not all(
        isinstance(value, str) and _WINE_OVERRIDE_PATTERN.fullmatch(value) for value in overrides
    ):
        raise ValueError(f"Mod catalog entry {mod_id} has an invalid Wine DLL overrides list")

    return ModDefinition(
        mod_id=mod_id,
        name=entry["name"],
        steam_app_id=entry["steam_app_id"],
        install_dir=entry["install_dir"],
        executable=executable,
        source=source,
        files=files,
        ini_file=ini_file,
        wine_dll_overrides=tuple(overrides),
        credits=_parse_credits(entry.get("credits")) or default_credits,
        manual=manual,
        executable_alternatives=alternatives,
        backup_file=backup_file,
        offline_only=offline_only,
        launcher_replacement=launcher,
        display_tags=display_tags,
        ini_resolution=ini_resolution,
        backups=backups,
    )


def _read_catalog_document(path: Path) -> tuple[dict[str, object], str, str]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"Mod catalog is not valid JSON: {error}") from error

    if not isinstance(document, dict) or not isinstance(
        document.get("mods"),
        list,
    ):
        raise ValueError("Mod catalog must contain schema_version, source_repository, and mods")
    schema_version = document.get("schema_version")
    if (
        not isinstance(schema_version, int)
        or isinstance(schema_version, bool)
        or schema_version != 3
    ):
        raise ValueError(
            f"Unsupported mod catalog schema version: {schema_version}"
        )

    repository = document.get("source_repository")
    if not isinstance(repository, dict):
        raise ValueError("Mod catalog has invalid source repository metadata")
    repository_url = repository.get("url")
    repository_ref = repository.get("ref")
    if (
        not isinstance(repository_url, str)
        or urlparse(repository_url).scheme != "https"
        or not isinstance(repository_ref, str)
        or not repository_ref
    ):
        raise ValueError("Mod catalog has invalid source repository metadata")
    return document, repository_url, repository_ref


def bundled_catalog_path() -> Path:
    """catalog.json at the plugin root; store builds keep this package under py_modules/."""
    package_root = Path(__file__).resolve().parents[1]
    plugin_root = package_root.parent if package_root.name == "py_modules" else package_root
    return plugin_root / "catalog.json"


def read_catalog_repository(path: Path | None = None) -> tuple[str, str]:
    _, repository_url, repository_ref = _read_catalog_document(
        bundled_catalog_path() if path is None else path
    )
    return repository_url, repository_ref


def load_mod_catalog(
    path: Path | None = None,
    *,
    repository_url: str | None = None,
    repository_ref: str | None = None,
) -> tuple[ModDefinition, ...]:
    """Load a catalog; the optional repository values override its source_repository."""
    document, catalog_url, catalog_ref = _read_catalog_document(
        bundled_catalog_path() if path is None else path
    )
    repository_url = repository_url or catalog_url
    repository_ref = repository_ref or catalog_ref

    default_credits = _parse_credits(document.get("default_credits"))

    mods = tuple(
        _parse_mod(entry, index, repository_url, repository_ref, default_credits)
        for index, entry in enumerate(document["mods"])
    )
    ids: set[str] = set()
    for mod in mods:
        if mod.mod_id in ids:
            raise ValueError(f"Duplicate mod ID in catalog: {mod.mod_id}")
        ids.add(mod.mod_id)
    return mods
