import io
import unittest
from pathlib import Path
from unittest.mock import Mock
from zipfile import ZipFile


def write_zip(path: Path, files: dict[str, bytes]) -> None:
    with ZipFile(path, "w") as archive:
        for name, content in files.items():
            archive.writestr(name, content)


def response(content: bytes) -> Mock:
    stream = io.BytesIO(content)
    mock = Mock()
    mock.__enter__ = Mock(return_value=stream)
    mock.__exit__ = Mock(return_value=False)
    return mock


class BackendEntryPointTests(unittest.TestCase):
    def test_every_runtime_module_compiles(self) -> None:
        import py_compile

        root = Path(__file__).resolve().parent.parent
        modules = [root / "main.py", *root.glob("scanner/*.py"), *root.glob("store/*.py")]
        for module in modules:
            with self.subTest(module=module.name):
                py_compile.compile(str(module), doraise=True)


if __name__ == "__main__":
    unittest.main()
