from support.builders import write_zip
from support.sandbox import SandboxTest

from utils import zips


class ZipsTests(SandboxTest):
    def setUp(self) -> None:
        super().setUp()
        self.archive = write_zip(self.root / "mod.zip", {
            "bin/": b"", "bin/mod.asi": b"asi", "__MACOSX/._mod.asi": b"junk", "Thumbs.db": b"junk",
        })

    def test_file_names_drop_folders_and_os_junk(self) -> None:
        self.assertEqual(["bin/mod.asi"], zips.file_names(self.archive))

    def test_damaged_zip_raises_the_callers_message(self) -> None:
        self.write({"broken.zip": "not a zip"})
        self.assertFalse(zips.is_zip(self.root / "broken.zip"))
        with self.assertRaisesRegex(ValueError, "^Mod archive is invalid$"):
            with zips.open_zip(self.root / "broken.zip", "Mod archive is invalid"):
                pass

    def test_reading_and_extracting_respect_the_limit(self) -> None:
        with zips.open_zip(self.archive, "invalid") as archive:
            info = archive.getinfo("bin/mod.asi")
            self.assertEqual(b"asi", zips.read_file(archive, info, 3, "Mod file"))
            with self.assertRaisesRegex(ValueError, "exceeds"):
                zips.read_file(archive, info, 2, "Mod file")
            self.assertEqual(3, zips.extract_file(archive, info, self.root / "out/mod.asi", 3, "Mod file"))
        self.assert_files({"out/mod.asi": "asi"})
