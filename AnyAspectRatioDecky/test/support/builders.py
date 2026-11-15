import io
from dataclasses import replace
from functools import cache
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

from scanner.mod_catalog import ModDefinition, ModSource, ModSourceFile, SourceKind, load_mod_catalog
from scanner.steam_library_scanner import InstalledApp

REPOSITORY = "https://github.com/example/mods"
RAW = "https://raw.githubusercontent.com/example/mods/main"


def zip_bytes(entries: dict[str, bytes | str]) -> bytes:
    content = io.BytesIO()
    with ZipFile(content, "w", ZIP_DEFLATED) as archive:
        for name, value in entries.items():
            archive.writestr(name, value)
    return content.getvalue()


def write_zip(path: Path, entries: dict[str, bytes | str]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(zip_bytes(entries))
    return path


def archive_source(path: str = "fixes/test.zip", destination: str = "", sha256: str = "") -> ModSource:
    return ModSource(SourceKind.ARCHIVE, path, destination, repository_url=REPOSITORY, repository_ref="main", sha256=sha256)


def directory_source(path: str) -> ModSource:
    return ModSource(SourceKind.DIRECTORY, path, repository_url=REPOSITORY, repository_ref="main")


def files_source(mapping: dict[str, str], hashes: dict[str, str] | None = None) -> ModSource:
    """mapping: installed path -> repository path."""
    return ModSource(
        SourceKind.FILES,
        files=tuple(ModSourceFile(path, destination) for destination, path in mapping.items()),
        repository_url=REPOSITORY,
        repository_ref="main",
        file_hashes=tuple(sorted((hashes or {}).items())),
    )


def mod(source: ModSource, *files: str, **changes: object) -> ModDefinition:
    base = ModDefinition(
        mod_id="test-mod", name="Test mod", steam_app_id="123", install_dir="Test Game", executable="game.exe",
        source=source, files=files, wine_dll_overrides=(),
    )
    return replace(base, **changes)


def loose_mod(mapping: dict[str, str], hashes: dict[str, str] | None = None, **changes: object) -> ModDefinition:
    return mod(files_source(mapping, hashes), *mapping, **changes)


@cache
def _catalog() -> dict[str, ModDefinition]:
    return {entry.mod_id: entry for entry in load_mod_catalog()}


def catalog_mod(mod_id: str) -> ModDefinition:
    return _catalog()[mod_id]


def app_for(entry: ModDefinition, game: Path, name: str = "Test") -> InstalledApp:
    return InstalledApp(entry.steam_app_id, name, entry.install_dir, "", str(game))
