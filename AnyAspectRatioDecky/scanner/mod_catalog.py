import json
from collections.abc import Iterable
from dataclasses import dataclass, replace
from enum import StrEnum
from pathlib import Path, PurePosixPath
from typing import Annotated

from helper import errors
from helper.errors import PluginError
from helper.validated_types import (
    DisplayTag,
    GitHubRepository,
    GitRef,
    HttpsUrl,
    HttpsUrlOrEmpty,
    IniKey,
    IniSection,
    ModId,
    NonBlank,
    NonEmpty,
    RelativePath,
    RelativePathOrEmpty,
    Sha256,
    Sha256OrEmpty,
    SteamAppId,
    WineOverride,
)
from helper.validator import Validator
from utils import text_files
from utils.json_codec import decode, exact_keys, json_field, one_of

CATALOG_SCHEMA_VERSION = 3
REPOSITORY_OWNER = "TLin-Y"


class SourceKind(StrEnum):
    ARCHIVE = "archive"
    DIRECTORY = "directory"
    FILES = "files"
    LOCAL_ARCHIVE = "local_archive"

ZIP_SOURCE_KINDS = frozenset((SourceKind.ARCHIVE, SourceKind.LOCAL_ARCHIVE))
_HASHED_SOURCE_KINDS = frozenset((SourceKind.DIRECTORY, SourceKind.FILES))


class IniValueKind(StrEnum):
    # Width and height keys hold pixels, e.g. Width=1280 and Height=800.
    PIXELS = "pixels"
    # Aspect ratio, e.g. 8 and 5.
    RATIO = "ratio"
    # One key holds both as text, e.g. Resolution="1280x800"
    PIXELS_STRING = "pixels_string"

def _sorted_items(mapping: dict[str, str]) -> tuple[tuple[str, str], ...]:
    return tuple(sorted(mapping.items()))


@dataclass(frozen=True, slots=True)
class ModSourceFile:
    path: RelativePath
    destination: RelativePath


@dataclass(frozen=True, slots=True)
class ModSource:
    kind: Annotated[SourceKind, one_of(SourceKind.ARCHIVE, SourceKind.DIRECTORY, SourceKind.FILES)]
    path: RelativePath | None = None
    destination: RelativePathOrEmpty = ""
    files: tuple[ModSourceFile, ...] = ()
    repository_url: str = json_field(default="", skip=True)
    repository_ref: str = json_field(default="", skip=True)
    sha256: Sha256OrEmpty = ""
    file_hashes: tuple[tuple[str, str], ...] = json_field(
        default=(), wire=dict[RelativePath, Sha256], convert=_sorted_items,
    )


@dataclass(frozen=True, slots=True)
class SupportLink:
    label: NonBlank
    url: HttpsUrl


@dataclass(frozen=True, slots=True)
class ModCredit:
    name: NonBlank
    url: HttpsUrlOrEmpty = ""
    support: tuple[SupportLink, ...] = ()


@exact_keys
@dataclass(frozen=True, slots=True)
class LauncherReplacement:
    source: RelativePath
    target: RelativePath
    backup: RelativePath


@exact_keys
@dataclass(frozen=True, slots=True)
class IniResolutionPair:
    section: IniSection
    width: IniKey
    height: IniKey
    kind: Annotated[IniValueKind, one_of(IniValueKind.PIXELS, IniValueKind.RATIO)]


@exact_keys
@dataclass(frozen=True, slots=True)
class IniResolutionString:
    """One "WIDTHxHEIGHT" key."""

    section: IniSection
    key: IniKey

    def as_pair(self) -> IniResolutionPair:
        return IniResolutionPair(self.section, self.key, self.key, IniValueKind.PIXELS_STRING)


@exact_keys
@dataclass(frozen=True, slots=True)
class ModBackup:
    target: RelativePath
    backup: RelativePath


@dataclass(frozen=True, slots=True)
class ModDefinition:
    mod_id: ModId = json_field("id")
    name: NonEmpty
    steam_app_id: SteamAppId
    install_dir: RelativePath
    source: ModSource
    files: tuple[RelativePath, ...]
    wine_dll_overrides: tuple[WineOverride, ...]
    executable: RelativePathOrEmpty = ""
    ini_file: RelativePath | None = None
    credits: tuple[ModCredit, ...] = ()
    manual: bool = False
    executable_alternatives: tuple[RelativePath, ...] = ()
    backup_file: RelativePath | None = None
    offline_only: bool = False
    launcher_replacement: LauncherReplacement | None = None
    display_tags: tuple[DisplayTag, ...] = ()
    ini_resolution: tuple[IniResolutionPair, ...] = ()
    backups: tuple[ModBackup, ...] = ()

    @property
    def file_backups(self) -> tuple[ModBackup, ...]:
        if self.backup_file is not None:
            return (ModBackup(self.files[0], self.backup_file),)
        return self.backups

    @property
    def executables(self) -> tuple[str, ...]:
        return (self.executable, *self.executable_alternatives) if self.executable else self.executable_alternatives


@dataclass(frozen=True, slots=True)
class _EntryExtras:
    ini_resolution_strings: tuple[IniResolutionString, ...] = ()


@dataclass(frozen=True, slots=True)
class SourceRepository:
    url: GitHubRepository
    ref: GitRef


@dataclass(frozen=True, slots=True)
class _CatalogDocument:
    source_repository: SourceRepository
    mods: tuple[object, ...]
    default_credits: tuple[ModCredit, ...] = ()

def _first_repeat(values: Iterable[str]) -> str | None:
    seen: set[str] = set()
    for value in values:
        if value in seen:
            return value
        seen.add(value)
    return None


def _folder(path: str) -> PurePosixPath:
    return PurePosixPath(path).parent


def _folded(paths: Iterable[str]) -> set[str]:
    return {path.casefold() for path in paths}


def _reserved_paths(mod: ModDefinition) -> set[str]:
    return set(mod.files) | set(mod.executables)


def _check_executable(mod: ModDefinition) -> None:
    if not mod.executable and not mod.manual:
        raise PluginError("executable is missing")
    if repeat := _first_repeat(mod.executables):
        raise PluginError(f"executable_alternatives repeats {repeat}")


def _check_files(mod: ModDefinition) -> None:
    if not mod.files:
        raise PluginError("files is empty")
    if repeat := _first_repeat(mod.files):
        raise PluginError(f"files repeats {repeat}")
    if mod.ini_file is not None and mod.ini_file not in mod.files:
        raise PluginError("ini_file must be listed in files")


def _check_source(mod: ModDefinition) -> None:
    source = mod.source
    is_file_list = source.kind == SourceKind.FILES

    if is_file_list:
        if source.path is not None:
            raise PluginError("source.path is only for archive and directory sources")
        if {item.destination for item in source.files} != set(mod.files):
            raise PluginError("source.files destinations must match files")
    else:
        if source.path is None:
            raise PluginError("source.path is missing")
        if source.files:
            raise PluginError("source.files is only for files sources")
        
    if source.file_hashes and source.kind not in _HASHED_SOURCE_KINDS:
        raise PluginError("source.file_hashes is only for files and directory sources")
    if unlisted := sorted({path for path, _ in source.file_hashes} - set(mod.files)):
        raise PluginError(f"source.file_hashes names {unlisted[0]}, which is not in files")


def _check_ini(mod: ModDefinition) -> None:
    if mod.ini_resolution and (mod.ini_file is None or mod.manual):
        raise PluginError("ini_resolution needs an ini_file and a mod that is not manual")
    
    seen: set[tuple[str, str]] = set()
    for pair in mod.ini_resolution:
        keys = {(pair.section.casefold(), name.casefold()) for name in (pair.width, pair.height)}
        expected = 1 if pair.kind == IniValueKind.PIXELS_STRING else 2
        if len(keys) != expected or keys & seen:
            raise PluginError(f"ini_resolution repeats a key in [{pair.section}]")
        seen |= keys


def _check_backup(mod: ModDefinition) -> None:
    reserved = _folded(_reserved_paths(mod))
    if mod.backup_file is not None and (
        len(mod.files) != 1
        or _folder(mod.files[0]) != _folder(mod.backup_file)
        or mod.backup_file.casefold() in reserved
    ):
        raise PluginError("backup_file must sit beside the mod's only file under a free name")
    if mod.backups and (mod.backup_file is not None or mod.manual):
        raise PluginError("backups cannot be combined with backup_file or a manual mod")
    
    taken = reserved | _folded(mod_log_names(mod.files))
    targets: set[str] = set()
    for item in mod.backups:
        if (
            item.target not in mod.files
            or item.target == mod.ini_file
            or item.target.casefold() in targets
            or item.backup.casefold() in taken
            or _folder(item.target) != _folder(item.backup)
        ):
            raise PluginError(
                f"backups: {item.target} must be a mod file, not the INI, backed up once beside itself under a free name"
            )
        targets.add(item.target.casefold())
        taken.add(item.backup.casefold())


def _check_launcher(mod: ModDefinition) -> None:
    """An offline mod without other backups may swap the game's launcher for one of its files."""
    launcher = mod.launcher_replacement
    if launcher is None:
        return
    if not mod.offline_only or mod.manual or mod.backup_file is not None or mod.backups:
        raise PluginError("launcher_replacement is only for offline_only mods without backups")
    
    reserved = _reserved_paths(mod)
    if (
        len({launcher.source, launcher.target, launcher.backup}) != 3
        or launcher.source not in reserved
        or launcher.target in mod.files
        or launcher.target == mod.executable
        or launcher.backup in reserved
        or _folder(launcher.target) != _folder(launcher.backup)
    ):
        raise PluginError("launcher_replacement must copy a mod file over another launcher, backed up beside it")


def _check_mod(mod: ModDefinition) -> None:
    _check_executable(mod)
    _check_files(mod)
    _check_source(mod)
    _check_ini(mod)
    _check_backup(mod)
    _check_launcher(mod)
    if repeat := _first_repeat(mod.display_tags):
        raise PluginError(f"display_tags repeats {repeat}")


def mod_log_names(files: tuple[str, ...] | list[str]) -> list[str]:
    """The <name>.log an ASI loader writes beside each .asi plugin."""
    return list(dict.fromkeys(
        str(PurePosixPath(path).with_suffix(".log"))
        for path in files
        if Validator.Files.has_extension(path, ".asi")
    ))


def summarize_credits(
    mods: tuple[ModDefinition, ...], applied_mod_ids: frozenset[str] = frozenset(),
) -> list[dict[str, object]]:
    """Group mods by author: the repository owner first, then most applied, then most mods; applied mods first."""
    authors: dict[ModCredit, list[ModDefinition]] = {}
    for mod in mods:
        for credit in mod.credits:
            authors.setdefault(credit, []).append(mod)
    return [
        {
            "name": credit.name,
            "url": credit.url,
            "support": [{"label": link.label, "url": link.url} for link in credit.support],
            "mod_count": len(owned),
            "mods": [
                {"id": mod.mod_id, "name": mod.name, "applied": mod.mod_id in applied_mod_ids}
                for mod in sorted(owned, key=lambda mod: (mod.mod_id not in applied_mod_ids, mod.name.lower()))
            ],
        }
        for credit, owned in sorted(
            authors.items(), key=lambda item: (
                item[0].name != REPOSITORY_OWNER,
                -sum(mod.mod_id in applied_mod_ids for mod in item[1]),
                -len(item[1]),
                item[0].name.lower(),
            )
        )
    ]


def _entry_label(entry: object, index: int) -> str:
    mod_id = entry.get("id") if isinstance(entry, dict) else None
    return mod_id if isinstance(mod_id, str) and mod_id else str(index)


def _parse_mod(
    entry: object,
    index: int,
    repository: SourceRepository,
    default_credits: tuple[ModCredit, ...],
) -> ModDefinition:
    with errors.wrap(f"Mod catalog entry {_entry_label(entry, index)}"):
        mod = decode(entry, ModDefinition)
        extras = decode(entry, _EntryExtras)
        mod = replace(
            mod,
            source=replace(mod.source, repository_url=repository.url, repository_ref=repository.ref),
            credits=mod.credits or default_credits,
            ini_resolution=mod.ini_resolution + tuple(item.as_pair() for item in extras.ini_resolution_strings),
        )
        _check_mod(mod)
    return mod


def _read_catalog_document(path: Path, content: bytes | None = None) -> _CatalogDocument:
    try:
        document = text_files.load_json(path) if content is None else json.loads(content.decode(text_files.TEXT_ENCODING))
    except json.JSONDecodeError as error:
        raise PluginError(f"Mod catalog is not valid JSON: {error}") from error
    version = document.get("schema_version") if isinstance(document, dict) else None
    if type(version) is not int or version != CATALOG_SCHEMA_VERSION:
        raise PluginError(f"Unsupported mod catalog schema version: {version}")
    with errors.wrap("Mod catalog"):
        return decode(document, _CatalogDocument)


def bundled_catalog_path() -> Path:
    """catalog.json at the plugin root; store builds keep this package under py_modules/."""
    package_root = Path(__file__).resolve().parents[1]
    plugin_root = package_root.parent if package_root.name == "py_modules" else package_root
    return plugin_root / "catalog.json"


def read_catalog_repository(path: Path | None = None) -> tuple[str, str]:
    repository = _read_catalog_document(bundled_catalog_path() if path is None else path).source_repository
    return repository.url, repository.ref


def load_mod_catalog(
    path: Path | None = None,
    *,
    content: bytes | None = None,
    repository_url: str | None = None,
    repository_ref: str | None = None,
) -> tuple[ModDefinition, ...]:
    document = _read_catalog_document(bundled_catalog_path() if path is None else path, content)
    repository = SourceRepository(
        repository_url or document.source_repository.url,
        repository_ref or document.source_repository.ref,
    )
    mods = tuple(
        _parse_mod(entry, index, repository, document.default_credits)
        for index, entry in enumerate(document.mods)
    )
    
    if repeat := _first_repeat(mod.mod_id for mod in mods):
        raise PluginError(f"Duplicate mod ID in catalog: {repeat}")
    return mods
