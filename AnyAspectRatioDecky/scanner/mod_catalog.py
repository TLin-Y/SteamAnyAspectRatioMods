import json
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse

from helper.constants import (
    MOD_CATALOG_APP_ID_PATTERN,
    MOD_CATALOG_DUPLICATE_FILE_MESSAGE,
    MOD_CATALOG_DUPLICATE_ID_MESSAGE,
    MOD_CATALOG_FILENAME,
    MOD_CATALOG_ID_PATTERN,
    MOD_CATALOG_INI_NOT_IN_FILES_MESSAGE,
    MOD_CATALOG_CREDIT_SUPPORT_KEY,
    MOD_CATALOG_CREDITS_KEY,
    MOD_CATALOG_DEFAULT_CREDITS_KEY,
    MOD_CATALOG_INVALID_CREDITS_MESSAGE,
    MOD_CATALOG_PINNED_AUTHOR,
    MOD_CATALOG_INVALID_APP_ID_MESSAGE,
    MOD_CATALOG_INVALID_BACKUP_FILE_MESSAGE,
    MOD_CATALOG_INVALID_ENTRY_MESSAGE,
    MOD_CATALOG_INVALID_FILE_LIST_MESSAGE,
    MOD_CATALOG_INVALID_JSON_MESSAGE,
    MOD_CATALOG_INVALID_OVERRIDE_LIST_MESSAGE,
    MOD_CATALOG_INVALID_RELATIVE_PATH_MESSAGE,
    MOD_CATALOG_INVALID_SCHEMA_MESSAGE,
    MOD_CATALOG_INVALID_SOURCE_KIND_MESSAGE,
    MOD_CATALOG_INVALID_SOURCE_FILES_MESSAGE,
    MOD_CATALOG_INVALID_SOURCE_MESSAGE,
    MOD_CATALOG_INVALID_REPOSITORY_MESSAGE,
    MOD_CATALOG_MISSING_FIELD_MESSAGE,
    MOD_CATALOG_MODS_KEY,
    MOD_CATALOG_PATH_SEPARATOR,
    MOD_CATALOG_REQUIRED_STRING_FIELDS,
    MOD_CATALOG_SCHEMA_VERSION,
    MOD_CATALOG_SCHEMA_VERSION_KEY,
    MOD_CATALOG_SOURCE_REPOSITORY_KEY,
    MOD_CATALOG_SOURCE_REPOSITORY_REF_KEY,
    MOD_CATALOG_SOURCE_REPOSITORY_URL_KEY,
    MOD_CATALOG_UNSUPPORTED_SCHEMA_MESSAGE,
    MOD_CATALOG_UNSAFE_PATH_CHARACTERS,
    MOD_EXECUTABLE_KEY,
    MOD_EXECUTABLE_ALTERNATIVES_KEY,
    MOD_FILES_KEY,
    MOD_BACKUP_FILE_KEY,
    MOD_BACKUPS_KEY,
    MOD_OFFLINE_ONLY_KEY,
    MOD_LAUNCHER_REPLACEMENT_KEY,
    MOD_ID_KEY,
    MOD_INI_FILE_KEY,
    MOD_INSTALL_DIRECTORY_KEY,
    MOD_MANUAL_KEY,
    MOD_NAME_KEY,
    MOD_DISPLAY_TAGS_KEY,
    MOD_DISPLAY_TAG_PATTERN,
    MOD_SOURCE_ARCHIVE,
    MOD_SOURCE_DESTINATION_KEY,
    MOD_SOURCE_DIRECTORY,
    MOD_SOURCE_FILES,
    MOD_SOURCE_FILES_KEY,
    MOD_SOURCE_SHA256_KEY,
    MOD_SOURCE_KINDS,
    MOD_SOURCE_KEY,
    MOD_SOURCE_KIND_KEY,
    MOD_SOURCE_PATH_KEY,
    MOD_STEAM_APP_ID_KEY,
    MOD_WINE_OVERRIDES_KEY,
    MOD_CATALOG_OVERRIDE_PATTERN,
)


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


def _parse_credits(raw: object) -> tuple[ModCredit, ...]:
    if raw is None:
        return ()
    if not isinstance(raw, list):
        raise ValueError(MOD_CATALOG_INVALID_CREDITS_MESSAGE)
    credits = []
    for item in raw:
        item = item if isinstance(item, dict) else {}
        name = item.get("name")
        url = item.get("url")
        support = item.get(MOD_CATALOG_CREDIT_SUPPORT_KEY, [])
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
            raise ValueError(MOD_CATALOG_INVALID_CREDITS_MESSAGE)
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
                item[0].name != MOD_CATALOG_PINNED_AUTHOR,
                -item[1],
                item[0].name.lower(),
            )
        )
    ]


def _require_string(entry: dict[str, object], key: str, mod_id: str) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(
            MOD_CATALOG_MISSING_FIELD_MESSAGE.format(
                mod_id=mod_id or "unknown",
                field=key,
            )
        )
    return value


def _validate_relative_path(path: str, mod_id: str) -> None:
    parsed = PurePosixPath(path)
    if (
        not path
        or parsed.is_absolute()
        or any(character in path for character in MOD_CATALOG_UNSAFE_PATH_CHARACTERS)
        or any(part in ("", ".", "..") for part in path.split(MOD_CATALOG_PATH_SEPARATOR))
    ):
        raise ValueError(
            MOD_CATALOG_INVALID_RELATIVE_PATH_MESSAGE.format(mod_id=mod_id, path=path)
        )


def _parse_source(
    entry: dict[str, object],
    mod_id: str,
    repository_url: str,
    repository_ref: str,
) -> ModSource:
    raw_source = entry.get(MOD_SOURCE_KEY)
    if not isinstance(raw_source, dict):
        raise ValueError(MOD_CATALOG_INVALID_SOURCE_MESSAGE.format(mod_id=mod_id))

    kind = raw_source.get(MOD_SOURCE_KIND_KEY)
    if not isinstance(kind, str) or kind not in MOD_SOURCE_KINDS:
        raise ValueError(MOD_CATALOG_INVALID_SOURCE_KIND_MESSAGE.format(mod_id=mod_id))

    path = None
    destination = raw_source.get(MOD_SOURCE_DESTINATION_KEY, "")
    if not isinstance(destination, str):
        raise ValueError(MOD_CATALOG_INVALID_SOURCE_MESSAGE.format(mod_id=mod_id))
    if destination:
        _validate_relative_path(destination, mod_id)

    sha256 = raw_source.get(MOD_SOURCE_SHA256_KEY, "")
    if not isinstance(sha256, str) or (sha256 and not _is_sha256(sha256)):
        raise ValueError(MOD_CATALOG_INVALID_SOURCE_MESSAGE.format(mod_id=mod_id))

    source_files: tuple[ModSourceFile, ...] = ()
    if kind in (MOD_SOURCE_ARCHIVE, MOD_SOURCE_DIRECTORY):
        path = _require_string(raw_source, MOD_SOURCE_PATH_KEY, mod_id)
        _validate_relative_path(path, mod_id)
    else:
        raw_files = raw_source.get(MOD_SOURCE_FILES_KEY)
        if not isinstance(raw_files, list) or not raw_files:
            raise ValueError(MOD_CATALOG_INVALID_SOURCE_MESSAGE.format(mod_id=mod_id))
        parsed_files = []
        for source_file in raw_files:
            if not isinstance(source_file, dict):
                raise ValueError(MOD_CATALOG_INVALID_SOURCE_MESSAGE.format(mod_id=mod_id))
            source_path = _require_string(source_file, MOD_SOURCE_PATH_KEY, mod_id)
            target_path = _require_string(
                source_file,
                MOD_SOURCE_DESTINATION_KEY,
                mod_id,
            )
            _validate_relative_path(source_path, mod_id)
            _validate_relative_path(target_path, mod_id)
            parsed_files.append(ModSourceFile(source_path, target_path))
        source_files = tuple(parsed_files)

    return ModSource(
        kind=kind,
        path=path,
        destination=destination,
        files=source_files,
        repository_url=repository_url,
        repository_ref=repository_ref,
        sha256=sha256,
    )


def _is_sha256(value: str) -> bool:
    return len(value) == 64 and all(character in "0123456789abcdef" for character in value)


def _parse_mod(
    entry: object,
    index: int,
    repository_url: str,
    repository_ref: str,
    default_credits: tuple[ModCredit, ...] = (),
) -> ModDefinition:
    if not isinstance(entry, dict):
        raise ValueError(MOD_CATALOG_INVALID_ENTRY_MESSAGE.format(index=index))

    mod_id = entry.get(MOD_ID_KEY)
    mod_id = mod_id if isinstance(mod_id, str) else "unknown"
    if not MOD_CATALOG_ID_PATTERN.fullmatch(mod_id):
        raise ValueError(MOD_CATALOG_INVALID_ENTRY_MESSAGE.format(index=index))
    for key in MOD_CATALOG_REQUIRED_STRING_FIELDS:
        _require_string(entry, key, mod_id)

    app_id = entry[MOD_STEAM_APP_ID_KEY]
    if not MOD_CATALOG_APP_ID_PATTERN.fullmatch(app_id):
        raise ValueError(MOD_CATALOG_INVALID_APP_ID_MESSAGE.format(mod_id=mod_id))

    install_dir = entry[MOD_INSTALL_DIRECTORY_KEY]
    manual = entry.get(MOD_MANUAL_KEY, False)
    if not isinstance(manual, bool):
        raise ValueError(
            MOD_CATALOG_MISSING_FIELD_MESSAGE.format(mod_id=mod_id, field=MOD_MANUAL_KEY)
        )
    # A manual mod is only listed and explained, so its executable may be unknown.
    executable = (
        entry.get(MOD_EXECUTABLE_KEY, "") if manual
        else _require_string(entry, MOD_EXECUTABLE_KEY, mod_id)
    )
    if not isinstance(executable, str):
        raise ValueError(
            MOD_CATALOG_MISSING_FIELD_MESSAGE.format(mod_id=mod_id, field=MOD_EXECUTABLE_KEY)
        )
    _validate_relative_path(install_dir, mod_id)
    if executable:
        _validate_relative_path(executable, mod_id)
    alternatives = entry.get(MOD_EXECUTABLE_ALTERNATIVES_KEY, [])
    if not isinstance(alternatives, list) or not all(
        isinstance(path, str) and path for path in alternatives
    ):
        raise ValueError(
            MOD_CATALOG_MISSING_FIELD_MESSAGE.format(
                mod_id=mod_id, field=MOD_EXECUTABLE_ALTERNATIVES_KEY
            )
        )
    seen_executables = {executable} if executable else set()
    for path in alternatives:
        _validate_relative_path(path, mod_id)
        if path in seen_executables:
            raise ValueError(
                MOD_CATALOG_DUPLICATE_FILE_MESSAGE.format(mod_id=mod_id, path=path)
            )
        seen_executables.add(path)
    source = _parse_source(entry, mod_id, repository_url, repository_ref)

    ini_file = entry.get(MOD_INI_FILE_KEY)
    if ini_file is not None and (not isinstance(ini_file, str) or not ini_file):
        raise ValueError(
            MOD_CATALOG_MISSING_FIELD_MESSAGE.format(
                mod_id=mod_id,
                field=MOD_INI_FILE_KEY,
            )
        )
    if ini_file is not None:
        _validate_relative_path(ini_file, mod_id)

    # Single "WIDTHxHEIGHT" keys have their own list: plugins up to 1.8.0 reject an unknown
    # ini_resolution kind (refusing the whole catalog) but ignore unknown entry keys.
    raw_resolution = entry.get("ini_resolution", [])
    raw_strings = entry.get("ini_resolution_strings", [])
    for raw in (raw_resolution, raw_strings):
        if not isinstance(raw, list) or (raw and (not ini_file or manual)):
            raise ValueError(f"Mod {mod_id} has invalid ini_resolution metadata")
    resolution_pairs: list[IniResolutionPair] = []
    seen_resolution_keys: set[tuple[str, str]] = set()
    candidates = [(pair, ("width", "height")) for pair in raw_resolution]
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
        if len(keys) != len(names) or keys & seen_resolution_keys:
            raise ValueError(f"Mod {mod_id} has duplicate ini_resolution keys")
        seen_resolution_keys.update(keys)
        if len(names) == 1:
            resolution_pairs.append(IniResolutionPair(pair["section"], pair["key"], pair["key"], "pixels_string"))
        else:
            resolution_pairs.append(IniResolutionPair(pair["section"], pair["width"], pair["height"], pair["kind"]))

    raw_files = entry.get(MOD_FILES_KEY)
    raw_overrides = entry.get(MOD_WINE_OVERRIDES_KEY)
    if (
        not isinstance(raw_files, list)
        or not raw_files
        or not all(isinstance(path, str) and path for path in raw_files)
    ):
        raise ValueError(MOD_CATALOG_INVALID_FILE_LIST_MESSAGE.format(mod_id=mod_id))
    files = tuple(raw_files)
    seen_files: set[str] = set()
    for path in files:
        _validate_relative_path(path, mod_id)
        if path in seen_files:
            raise ValueError(
                MOD_CATALOG_DUPLICATE_FILE_MESSAGE.format(mod_id=mod_id, path=path)
            )
        seen_files.add(path)
    backup_file = entry.get(MOD_BACKUP_FILE_KEY)
    if backup_file is not None:
        if (
            not isinstance(backup_file, str)
            or len(files) != 1
        ):
            raise ValueError(
                MOD_CATALOG_INVALID_BACKUP_FILE_MESSAGE.format(mod_id=mod_id)
            )
        _validate_relative_path(backup_file, mod_id)
        if (
            PurePosixPath(files[0]).parent != PurePosixPath(backup_file).parent
            or backup_file.casefold() in {path.casefold() for path in seen_files | seen_executables}
        ):
            raise ValueError(
                MOD_CATALOG_INVALID_BACKUP_FILE_MESSAGE.format(mod_id=mod_id)
            )
    raw_backups = entry.get(MOD_BACKUPS_KEY, [])
    if not isinstance(raw_backups, list) or (raw_backups and (backup_file is not None or manual)):
        raise ValueError(f"Mod {mod_id} has invalid backups metadata")
    backups = []
    seen_backup_targets: set[str] = set()
    seen_backup_paths: set[str] = set()
    reserved_paths = {path.casefold() for path in seen_files | seen_executables}
    reserved_paths.update(
        str(PurePosixPath(path).with_suffix(".log")).casefold()
        for path in files if PurePosixPath(path).suffix.lower() == ".asi"
    )
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
            target not in seen_files
            or target == ini_file
            or target.casefold() in seen_backup_targets
            or backup.casefold() in seen_backup_paths | reserved_paths
            or PurePosixPath(target).parent != PurePosixPath(backup).parent
        ):
            raise ValueError(f"Mod {mod_id} has invalid backups paths")
        seen_backup_targets.add(target.casefold())
        seen_backup_paths.add(backup.casefold())
        backups.append(ModBackup(target, backup))
    if ini_file is not None and ini_file not in seen_files:
        raise ValueError(MOD_CATALOG_INI_NOT_IN_FILES_MESSAGE.format(mod_id=mod_id))
    if source.kind == MOD_SOURCE_FILES and {
        source_file.destination for source_file in source.files
    } != seen_files:
        raise ValueError(MOD_CATALOG_INVALID_SOURCE_FILES_MESSAGE.format(mod_id=mod_id))
    offline_only = entry.get(MOD_OFFLINE_ONLY_KEY, False)
    if not isinstance(offline_only, bool):
        raise ValueError(f"Mod {mod_id} offline_only must be a boolean")
    raw_display_tags = entry.get(MOD_DISPLAY_TAGS_KEY, [])
    if (
        not isinstance(raw_display_tags, list)
        or not all(
            isinstance(tag, str) and MOD_DISPLAY_TAG_PATTERN.fullmatch(tag)
            for tag in raw_display_tags
        )
        or len(set(raw_display_tags)) != len(raw_display_tags)
    ):
        raise ValueError(f"Mod {mod_id} has invalid display_tags")
    launcher_replacement = None
    raw_launcher = entry.get(MOD_LAUNCHER_REPLACEMENT_KEY)
    if raw_launcher is not None:
        if not isinstance(raw_launcher, dict) or set(raw_launcher) != {
            "source", "target", "backup"
        }:
            raise ValueError(f"Mod {mod_id} has invalid launcher_replacement metadata")
        for key in ("source", "target", "backup"):
            value = _require_string(raw_launcher, key, mod_id)
            _validate_relative_path(value, mod_id)
        launcher_replacement = LauncherReplacement(**raw_launcher)
        launcher_paths = {
            launcher_replacement.source,
            launcher_replacement.target,
            launcher_replacement.backup,
        }
        if (
            len(launcher_paths) != 3
            or not offline_only
            or manual
            or backup_file is not None
            or backups
            or launcher_replacement.source not in (seen_files | seen_executables)
            or launcher_replacement.target in seen_files
            or launcher_replacement.target == executable
            or launcher_replacement.backup in (seen_files | seen_executables)
            or PurePosixPath(launcher_replacement.target).parent
            != PurePosixPath(launcher_replacement.backup).parent
        ):
            raise ValueError(f"Mod {mod_id} has invalid launcher_replacement paths")
    if (
        not isinstance(raw_overrides, list)
        or not all(
            isinstance(value, str) and MOD_CATALOG_OVERRIDE_PATTERN.fullmatch(value)
            for value in raw_overrides
        )
    ):
        raise ValueError(MOD_CATALOG_INVALID_OVERRIDE_LIST_MESSAGE.format(mod_id=mod_id))

    return ModDefinition(
        mod_id=mod_id,
        name=entry[MOD_NAME_KEY],
        steam_app_id=app_id,
        install_dir=install_dir,
        executable=executable,
        source=source,
        files=files,
        ini_file=ini_file,
        wine_dll_overrides=tuple(raw_overrides),
        credits=_parse_credits(entry.get(MOD_CATALOG_CREDITS_KEY)) or default_credits,
        manual=manual,
        executable_alternatives=tuple(alternatives),
        backup_file=backup_file,
        offline_only=offline_only,
        launcher_replacement=launcher_replacement,
        display_tags=tuple(raw_display_tags),
        ini_resolution=tuple(resolution_pairs),
        backups=tuple(backups),
    )


def _read_catalog_document(path: Path) -> tuple[dict[str, object], str, str]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(MOD_CATALOG_INVALID_JSON_MESSAGE.format(error=error)) from error

    if not isinstance(document, dict) or not isinstance(
        document.get(MOD_CATALOG_MODS_KEY),
        list,
    ):
        raise ValueError(MOD_CATALOG_INVALID_SCHEMA_MESSAGE)
    schema_version = document.get(MOD_CATALOG_SCHEMA_VERSION_KEY)
    if (
        not isinstance(schema_version, int)
        or isinstance(schema_version, bool)
        or schema_version != MOD_CATALOG_SCHEMA_VERSION
    ):
        raise ValueError(
            MOD_CATALOG_UNSUPPORTED_SCHEMA_MESSAGE.format(version=schema_version)
        )

    repository = document.get(MOD_CATALOG_SOURCE_REPOSITORY_KEY)
    if not isinstance(repository, dict):
        raise ValueError(MOD_CATALOG_INVALID_REPOSITORY_MESSAGE)
    repository_url = repository.get(MOD_CATALOG_SOURCE_REPOSITORY_URL_KEY)
    repository_ref = repository.get(MOD_CATALOG_SOURCE_REPOSITORY_REF_KEY)
    if (
        not isinstance(repository_url, str)
        or urlparse(repository_url).scheme != "https"
        or not isinstance(repository_ref, str)
        or not repository_ref
    ):
        raise ValueError(MOD_CATALOG_INVALID_REPOSITORY_MESSAGE)
    return document, repository_url, repository_ref


def bundled_catalog_path() -> Path:
    """catalog.json at the plugin root; store builds keep this package under py_modules/."""
    package_root = Path(__file__).resolve().parents[1]
    plugin_root = package_root.parent if package_root.name == "py_modules" else package_root
    return plugin_root / MOD_CATALOG_FILENAME


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

    default_credits = _parse_credits(document.get(MOD_CATALOG_DEFAULT_CREDITS_KEY))

    mods = tuple(
        _parse_mod(entry, index, repository_url, repository_ref, default_credits)
        for index, entry in enumerate(document[MOD_CATALOG_MODS_KEY])
    )
    ids: set[str] = set()
    for mod in mods:
        if mod.mod_id in ids:
            raise ValueError(MOD_CATALOG_DUPLICATE_ID_MESSAGE.format(mod_id=mod.mod_id))
        ids.add(mod.mod_id)
    return mods
