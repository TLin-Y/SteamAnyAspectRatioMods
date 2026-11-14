import sys
import unittest
from pathlib import Path
from unittest import mock

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PLUGIN_ROOT))
sys.path.insert(0, str(PLUGIN_ROOT / "scripts"))

from export_store_repo import (  # noqa: E402
    BUILD_FLAVOR,
    SELF_UPDATE_ON,
    store_path,
)
from scanner import mod_catalog  # noqa: E402


class StoreLayoutTests(unittest.TestCase):
    def test_python_packages_go_to_py_modules(self) -> None:
        self.assertEqual("py_modules/scanner/mod_catalog.py", store_path("scanner/mod_catalog.py"))
        self.assertEqual("py_modules/helper/__init__.py", store_path("helper/__init__.py"))

    def test_runtime_data_goes_to_defaults(self) -> None:
        self.assertEqual("defaults/catalog.json", store_path("catalog.json"))
        self.assertEqual("defaults/THIRD_PARTY_NOTICES.md", store_path("THIRD_PARTY_NOTICES.md"))

    def test_entry_points_and_frontend_stay_at_root(self) -> None:
        self.assertEqual("main.py", store_path("main.py"))
        self.assertEqual("plugin.json", store_path("plugin.json"))
        self.assertEqual("src/index.tsx", store_path("src/index.tsx"))

    def test_build_flavor_switch_is_where_the_export_expects_it(self) -> None:
        self.assertEqual("py_modules/" + "helper/build_flavor.py", BUILD_FLAVOR)
        text = (PLUGIN_ROOT / "helper" / "build_flavor.py").read_text(encoding="utf-8")
        self.assertEqual(1, text.count(SELF_UPDATE_ON))

    def test_bundled_catalog_is_found_at_the_plugin_root_in_both_layouts(self) -> None:
        root = Path("/plugins/AnyAspectRatio")
        for module_file in (
            root / "scanner" / "mod_catalog.py",
            root / "py_modules" / "scanner" / "mod_catalog.py",
        ):
            with self.subTest(module_file=module_file), mock.patch.object(
                mod_catalog, "__file__", str(module_file)
            ):
                self.assertEqual(
                    (root / "catalog.json").resolve(), mod_catalog.bundled_catalog_path()
                )


if __name__ == "__main__":
    unittest.main()
