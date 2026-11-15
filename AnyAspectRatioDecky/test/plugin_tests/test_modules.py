import py_compile
import unittest

from support.sandbox import PLUGIN_ROOT


class RuntimeModuleTests(unittest.TestCase):
    def test_every_runtime_module_compiles(self) -> None:
        packages = [init.parent for init in PLUGIN_ROOT.glob("*/__init__.py")]
        for module in [PLUGIN_ROOT / "main.py", *(path for package in packages for path in package.glob("*.py"))]:
            with self.subTest(module=module.relative_to(PLUGIN_ROOT).as_posix()):
                py_compile.compile(str(module), doraise=True)
