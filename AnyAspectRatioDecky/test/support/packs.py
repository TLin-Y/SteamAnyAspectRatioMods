import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from installer.apply import apply_mod_files
from scanner.mod_catalog import ModDefinition, load_mod_catalog
from store.mod_sources import fetch_mod_readme, set_pack_dir
from store.offline_pack import archive_names, install_pack
from store.plugin_store import PluginStore, Settings
from support.builders import write_zip, zip_bytes
from support.catalog import catalog_document
from support.fakes import no_network, offline
from support.sandbox import SandboxTest

CATALOG_MEMBER = "repo-main/AnyAspectRatioDecky/catalog.json"
README_MEMBER = "repo-main/Dark Souls III/README.md"
DS3_FILES = ("d3d11.dll", "dinput8.dll", "DS3_ARFix.asi", "DS3_ARFix.ini")
PACK_FILES = 3  # catalog, the Dark Souls III archive and its README


@dataclass(frozen=True)
class Pack:
    path: Path
    mod_path: str
    members: dict[str, bytes]


def build_pack(folder: Path) -> Pack:
    """A repository-layout pack with a one-mod catalog (Dark Souls III), its archive, README and unrelated source."""
    document = catalog_document()
    entry = next(entry for entry in document["mods"] if entry["id"] == "dark-souls-iii")
    document["mods"] = [entry]
    archive = zip_bytes({name: name for name in DS3_FILES})
    entry["source"]["sha256"] = hashlib.sha256(archive).hexdigest()
    members = {
        CATALOG_MEMBER: json.dumps(document).encode(),
        f"repo-main/{entry['source']['path']}": archive,
        README_MEMBER: b"# DS3",
        "repo-main/compiler/dllmain.cpp": b"source",
    }
    return Pack(write_zip(folder / "repo.zip", members), entry["source"]["path"], members)


def split_pack(pack: Pack, folder: Path, version: str = "1.8.0") -> tuple[Path, ...]:
    """The pack as two release parts: the mod archive in the first, the README in the second (each with the catalog)."""
    groups = ({f"repo-main/{pack.mod_path}"}, {README_MEMBER})
    return tuple(
        write_zip(folder / name, {CATALOG_MEMBER: pack.members[CATALOG_MEMBER],
                                  **{member: pack.members[member] for member in group}})
        for name, group in zip(archive_names(version, 2), groups)
    )


class StoreTest(SandboxTest):
    """self.store is a fresh plugin data folder."""

    def setUp(self) -> None:
        super().setUp()
        self.store = PluginStore(self.root / "data")
        self.store.ensure_directories()


class PackTest(StoreTest):
    """self.pack is a pack zip ready to import into self.store."""

    def setUp(self) -> None:
        super().setUp()
        self.pack = build_pack(self.root)
        set_pack_dir(self.store.pack_dir)

    def install(self, selection: Path | tuple[Path, ...] | None = None) -> dict[str, object]:
        return install_pack(self.store, Settings(), selection or self.pack.path)

    def pack_files(self) -> int:
        return self.store.pack_stats()["file_count"]

    def packed_mod(self) -> ModDefinition:
        return load_mod_catalog(self.store.catalog_path)[0]

    def assert_works_offline(self, mod: ModDefinition | None = None) -> None:
        mod = mod or self.packed_mod()
        game = self.folder("game")
        with offline(), no_network():
            self.assertEqual([], apply_mod_files(mod, str(game)))
            self.assertEqual("# DS3", fetch_mod_readme(mod))
        self.assertTrue((game / "Game/dinput8.dll").is_file())
