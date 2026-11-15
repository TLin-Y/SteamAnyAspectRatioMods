import ast
import json
import unittest
from pathlib import Path
from unittest import mock

from export_store_repo import (
    BUILD_FLAVOR,
    FORBIDDEN_PYTHON,
    PY_PACKAGES,
    SELF_UPDATE_OFF,
    SELF_UPDATE_ON,
    UPDATER,
    UPDATER_STUB,
    store_path,
    verify_export,
)
from support.sandbox import PLUGIN_ROOT, SandboxTest

from scanner import mod_catalog


class StoreLayoutTests(unittest.TestCase):
    def test_every_python_package_is_exported(self) -> None:
        packages = {init.parent.name for init in PLUGIN_ROOT.glob("*/__init__.py")}
        self.assertEqual(packages, set(PY_PACKAGES))

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


def functions(path: Path) -> dict[str, list[str]]:
    tree = ast.parse(path.read_text(encoding="utf-8"))
    return {
        node.name: [arg.arg for arg in node.args.args]
        for node in tree.body if isinstance(node, ast.FunctionDef)
    }


class UpdaterStubTests(unittest.TestCase):
    def test_stub_replaces_every_updater_function_main_uses(self) -> None:
        tree = ast.parse((PLUGIN_ROOT / "main.py").read_text(encoding="utf-8"))
        imported = {
            alias.name for node in tree.body
            if isinstance(node, ast.ImportFrom) and node.module == "installer.plugin_updater"
            for alias in node.names
        }
        real = functions(PLUGIN_ROOT / "installer" / "plugin_updater.py")
        stub = functions(UPDATER_STUB)
        self.assertTrue(imported)
        for name in imported:
            with self.subTest(name=name):
                self.assertEqual(real[name], stub[name])

    def test_no_module_besides_main_imports_the_updater(self) -> None:
        users = [
            path.relative_to(PLUGIN_ROOT).as_posix()
            for package in PY_PACKAGES
            for path in (PLUGIN_ROOT / package).rglob("*.py")
            if "plugin_updater" in path.read_text(encoding="utf-8")
        ]
        self.assertEqual([], users)

    def test_stub_passes_the_store_checks(self) -> None:
        text = UPDATER_STUB.read_text(encoding="utf-8")
        self.assertEqual([], [word for word in FORBIDDEN_PYTHON if word in text])


class VerifyExportTests(SandboxTest):
    def setUp(self) -> None:
        super().setUp()
        self.write({
            "LICENSE": (PLUGIN_ROOT.parent / "LICENSE").read_text(encoding="utf-8")
            + (PLUGIN_ROOT / "LICENSE-Decky-Plugin-Template.txt").read_text(encoding="utf-8"),
            "package.json": json.dumps({"version": "1.8.1", "scripts": {"build": "rollup -c"}}),
            "plugin.json": json.dumps({"name": "AnyAspectRatio", "version": "1.8.1"}),
            "pnpm-lock.yaml": "lockfileVersion: '9.0'\n",
            BUILD_FLAVOR: SELF_UPDATE_OFF + "\n",
            UPDATER: UPDATER_STUB.read_bytes(),
            "py_modules/utils/files.py": "import os\n",
            "node_modules/x/addon.so": "",
        })

    def assert_rejected(self, tree: dict, problem: str) -> None:
        self.write(tree)
        with self.assertRaises(SystemExit) as raised:
            verify_export(self.root)
        self.assertIn(problem, str(raised.exception))

    def test_a_clean_export_passes(self) -> None:
        self.assertEqual("1.8.1", verify_export(self.root))

    def test_self_update_code_is_rejected(self) -> None:
        self.assert_rejected({BUILD_FLAVOR: SELF_UPDATE_ON + "\n"}, "must say")
        self.assert_rejected({UPDATER: "import subprocess\n"}, "contains 'subprocess'")

    def test_binaries_and_unexpected_folders_are_rejected(self) -> None:
        self.assert_rejected({"defaults/mod.dll": ""}, "binary or archive file: defaults/mod.dll")
        self.assert_rejected({"assets/icon.png": ""}, "assets/ must not exist")
        self.assert_rejected({"scripts/x.py": ""}, "unexpected top-level entries")

    def test_manifest_problems_are_rejected(self) -> None:
        self.assert_rejected({"plugin.json": json.dumps({"version": "1.8.0"})}, "!= plugin.json version")
        self.assert_rejected({"package.json": json.dumps({"version": "1.8.1", "remote_binary": []})},
                             "remote_binary")
        self.assert_rejected({"pnpm-lock.yaml": "lockfileVersion: '6.0'\n"}, "lockfileVersion")


if __name__ == "__main__":
    unittest.main()
