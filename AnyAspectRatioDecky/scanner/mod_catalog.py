import json
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
    MOD_CATALOG_INVALID_APP_ID_MESSAGE,
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
    MOD_FILES_KEY,
    MOD_ID_KEY,
    MOD_INI_FILE_KEY,
    MOD_INSTALL_DIRECTORY_KEY,
    MOD_MANUAL_KEY,
    MOD_NAME_KEY,
    MOD_SOURCE_ARCHIVE,
    MOD_SOURCE_DESTINATION_KEY,
    MOD_SOURCE_DIRECTORY,
    MOD_SOURCE_FILES,
    MOD_SOURCE_FILES_KEY,
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


@dataclass(frozen=True)
class ModCredit:
    name: str
    url: str
    support: tuple[tuple[str, str], ...] = ()


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
    """Group mods by author, most mods first."""
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
            authors.items(), key=lambda item: (-item[1], item[0].name.lower())
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
    )


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
    if ini_file is not None and ini_file not in seen_files:
        raise ValueError(MOD_CATALOG_INI_NOT_IN_FILES_MESSAGE.format(mod_id=mod_id))
    if source.kind == MOD_SOURCE_FILES and {
        source_file.destination for source_file in source.files
    } != seen_files:
        raise ValueError(MOD_CATALOG_INVALID_SOURCE_FILES_MESSAGE.format(mod_id=mod_id))
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
    return Path(__file__).resolve().parents[1] / MOD_CATALOG_FILENAME


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
