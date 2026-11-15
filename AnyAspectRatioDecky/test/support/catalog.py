import json
from collections.abc import Iterable
from functools import cache

from scanner.mod_catalog import ModDefinition, load_mod_catalog
from support.sandbox import PLUGIN_ROOT, SandboxTest


@cache
def _catalog_text() -> str:
    return (PLUGIN_ROOT / "catalog.json").read_text(encoding="utf-8")


def catalog_document() -> dict:
    """A fresh, editable copy of the shipped catalog.json."""
    return json.loads(_catalog_text())


class CatalogEditTest(SandboxTest):
    """Edit self.document (a copy of catalog.json), then load() it like the plugin does."""

    def setUp(self) -> None:
        super().setUp()
        self.document = catalog_document()

    def entry(self, mod_id: str) -> dict:
        return next(entry for entry in self.document["mods"] if entry["id"] == mod_id)

    def only(self, mod_id: str) -> dict:
        entry = self.entry(mod_id)
        self.document["mods"] = [entry]
        return entry

    def load(self) -> tuple[ModDefinition, ...]:
        path = self.root / "catalog.json"
        path.write_text(json.dumps(self.document), encoding="utf-8")
        return load_mod_catalog(path)

    def assert_rejected(self, entry: dict, field: str, values: Iterable[object], error: str = "") -> None:
        """Each value of entry[field] makes the catalog fail to load; the field is restored afterwards."""
        missing = object()
        original = entry.get(field, missing)
        for value in values:
            with self.subTest(**{field: value}):
                entry[field] = value
                with self.assertRaisesRegex(ValueError, error):
                    self.load()
        if original is missing:
            del entry[field]
        else:
            entry[field] = original
