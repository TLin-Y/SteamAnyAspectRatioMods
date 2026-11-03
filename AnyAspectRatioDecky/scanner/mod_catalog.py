import json
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse

from helper.constants import (
    MOD_CATALOG_DUPLICATE_APP_ID_MESSAGE,
    MOD_CATALOG_DUPLICATE_FILE_MESSAGE,
    MOD_CATALOG_DUPLICATE_ID_MESSAGE,
    MOD_CATALOG_APP_ID_PATTERN,
    MOD_CATALOG_FILENAME,
    MOD_CATALOG_ID_PATTERN,
    MOD_CATALOG_INI_NOT_IN_FILES_MESSAGE,
    MOD_CATALOG_INVALID_APP_ID_MESSAGE,
    MOD_CATALOG_INVALID_ENTRY_MESSAGE,
    MOD_CATALOG_INVALID_FILE_LIST_MESSAGE,
    MOD_CATALOG_INVALID_HASH_MESSAGE,
    MOD_CATALOG_INVALID_JSON_MESSAGE,
    MOD_CATALOG_INVALID_OVERRIDE_LIST_MESSAGE,
    MOD_CATALOG_INVALID_RELATIVE_PATH_MESSAGE,
    MOD_CATALOG_INVALID_RELEASE_MESSAGE,
    MOD_CATALOG_INVALID_SCHEMA_MESSAGE,
    MOD_CATALOG_INVALID_SIZE_MESSAGE,
    MOD_CATALOG_INVALID_URL_MESSAGE,
    MOD_CATALOG_MISSING_FIELD_MESSAGE,
    MOD_CATALOG_MODS_KEY,
    MOD_CATALOG_REQUIRED_RELEASE_FIELDS,
    MOD_CATALOG_REQUIRED_STRING_FIELDS,
    MOD_CATALOG_PATH_SEPARATOR,
    MOD_CATALOG_SCHEMA_VERSION,
    MOD_CATALOG_SCHEMA_VERSION_KEY,
    MOD_CATALOG_UNSUPPORTED_SCHEMA_MESSAGE,
    MOD_CATALOG_UNSAFE_PATH_CHARACTERS,
    MOD_EXECUTABLE_KEY,
    MOD_FILES_KEY,
    MOD_ID_KEY,
    MOD_INI_FILE_KEY,
    MOD_NAME_KEY,
    MOD_RELEASE_KEY,
    MOD_RELEASE_SHA256_KEY,
    MOD_RELEASE_MAX_SIZE_KEY,
    MOD_RELEASE_SIZE_KEY,
    MOD_RELEASE_URL_KEY,
    MOD_RELEASE_VERSION_KEY,
    MOD_INSTALL_DIRECTORY_KEY,
    MOD_STEAM_APP_ID_KEY,
    MOD_WINE_OVERRIDES_KEY,
    MOD_CATALOG_SHA256_PATTERN,
    MOD_CATALOG_OVERRIDE_PATTERN,
)


@dataclass(frozen=True)
class ModDefinition:
    mod_id: str
    name: str
    steam_app_id: str
    install_dir: str
    executable: str
    release_version: str
    release_url: str
    release_sha256: str
    release_size_bytes: int
    max_size_bytes: int
    files: tuple[str, ...]
    ini_file: str
    wine_dll_overrides: tuple[str, ...]


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
        or any(
            part in ("", ".", "..")
            for part in path.split(MOD_CATALOG_PATH_SEPARATOR)
        )
    ):
        raise ValueError(
            MOD_CATALOG_INVALID_RELATIVE_PATH_MESSAGE.format(mod_id=mod_id, path=path)
        )


def _parse_mod(entry: object, index: int) -> ModDefinition:
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
    executable = entry[MOD_EXECUTABLE_KEY]
    ini_file = entry[MOD_INI_FILE_KEY]
    for path in (install_dir, executable, ini_file):
        _validate_relative_path(path, mod_id)

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
                MOD_CATALOG_DUPLICATE_FILE_MESSAGE.format(
                    mod_id=mod_id,
                    path=path,
                )
            )
        seen_files.add(path)
    if ini_file not in seen_files:
        raise ValueError(MOD_CATALOG_INI_NOT_IN_FILES_MESSAGE.format(mod_id=mod_id))
    if (
        not isinstance(raw_overrides, list)
        or not all(
            isinstance(value, str) and MOD_CATALOG_OVERRIDE_PATTERN.fullmatch(value)
            for value in raw_overrides
        )
    ):
        raise ValueError(MOD_CATALOG_INVALID_OVERRIDE_LIST_MESSAGE.format(mod_id=mod_id))

    release = entry.get(MOD_RELEASE_KEY)
    if not isinstance(release, dict):
        raise ValueError(MOD_CATALOG_INVALID_RELEASE_MESSAGE.format(mod_id=mod_id))
    for key in MOD_CATALOG_REQUIRED_RELEASE_FIELDS:
        _require_string(release, key, mod_id)
    version = release[MOD_RELEASE_VERSION_KEY]
    url = release[MOD_RELEASE_URL_KEY]
    digest = release[MOD_RELEASE_SHA256_KEY]
    parsed_url = urlparse(url)
    if parsed_url.scheme != "https" or parsed_url.hostname != "github.com":
        raise ValueError(MOD_CATALOG_INVALID_URL_MESSAGE.format(mod_id=mod_id))
    if not MOD_CATALOG_SHA256_PATTERN.fullmatch(digest):
        raise ValueError(MOD_CATALOG_INVALID_HASH_MESSAGE.format(mod_id=mod_id))
    size = release.get(MOD_RELEASE_SIZE_KEY)
    max_size = release.get(MOD_RELEASE_MAX_SIZE_KEY)
    if (
        not isinstance(size, int)
        or isinstance(size, bool)
        or not isinstance(max_size, int)
        or isinstance(max_size, bool)
        or size <= 0
        or max_size < size
    ):
        raise ValueError(MOD_CATALOG_INVALID_SIZE_MESSAGE.format(mod_id=mod_id))

    return ModDefinition(
        mod_id=mod_id,
        name=entry[MOD_NAME_KEY],
        steam_app_id=app_id,
        install_dir=install_dir,
        executable=executable,
        release_version=version,
        release_url=url,
        release_sha256=digest.lower(),
        release_size_bytes=size,
        max_size_bytes=max_size,
        files=files,
        ini_file=ini_file,
        wine_dll_overrides=tuple(raw_overrides),
    )


def load_mod_catalog(path: Path | None = None) -> tuple[ModDefinition, ...]:
    catalog_path = (
        Path(__file__).resolve().parents[1] / MOD_CATALOG_FILENAME
        if path is None
        else path
    )
    try:
        document = json.loads(catalog_path.read_text(encoding="utf-8"))
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

    mods = tuple(
        _parse_mod(entry, index)
        for index, entry in enumerate(document[MOD_CATALOG_MODS_KEY])
    )
    ids: set[str] = set()
    app_ids: set[str] = set()
    for mod in mods:
        if mod.mod_id in ids:
            raise ValueError(
                MOD_CATALOG_DUPLICATE_ID_MESSAGE.format(mod_id=mod.mod_id)
            )
        if mod.steam_app_id in app_ids:
            raise ValueError(
                MOD_CATALOG_DUPLICATE_APP_ID_MESSAGE.format(app_id=mod.steam_app_id)
            )
        ids.add(mod.mod_id)
        app_ids.add(mod.steam_app_id)
    return mods
