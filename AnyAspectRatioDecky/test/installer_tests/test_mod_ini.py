from unittest.mock import patch

from support.builders import archive_source, catalog_mod, loose_mod, mod, zip_bytes
from support.fakes import serving
from support.sandbox import GameTest

from installer.mod_ini import (
    read_mod_ini,
    rewrite_ini_resolution,
    update_existing_ini_resolution,
    write_mod_ini,
)
from scanner.mod_catalog import IniResolutionPair

PIXELS_AND_RATIO = (
    IniResolutionPair("Resolution", "Width", "Height", "pixels"),
    IniResolutionPair("TargetResolution", "TargetWidth", "TargetHeight", "pixels"),
    IniResolutionPair("Aspect", "AspectWidth", "AspectHeight", "ratio"),
)


class ResolutionTests(GameTest):
    def test_apply_writes_only_the_catalogued_pixel_and_ratio_fields(self) -> None:
        fix = mod(archive_source(), "fix.ini", ini_file="fix.ini", ini_resolution=PIXELS_AND_RATIO)
        original = (
            b"[Resolution]\r\nWidth = 1280\r\nHeight = 800 ; keep comment\r\n"
            b"\r\n[TargetResolution]\nTargetWidth=1280\nTargetHeight=800\n"
            b"\n[Ratio]\nTargetWidth=16.0\nTargetHeight=10.0\n"
            b"\n[SecondaryTarget]\nTargetWidth=3840\nTargetHeight=2160\n"
            b"\n[Unset]\nWidth=0\nHeight=0\n"
            b"\n[Replacement]\nReplaceWidth=1280\nReplaceHeight=720\n"
            b"\n[Aspect]\nAspectWidth=16\nAspectHeight=10\n"
        )
        expected = (
            original.replace(b"Width = 1280\r\n", b"Width = 1920\r\n", 1)
            .replace(b"Height = 800 ; keep", b"Height = 1080 ; keep", 1)
            .replace(b"TargetWidth=1280\nTargetHeight=800", b"TargetWidth=1920\nTargetHeight=1080", 1)
            .replace(b"AspectHeight=10", b"AspectHeight=9", 1)
        )
        with serving(zip_bytes({"fix.ini": original})):
            self.apply(fix, replace_existing_files=True, ini_resolution=(1920, 1080))
            self.assert_files({"fix.ini": expected})
            self.assertEqual([], self.remove(fix, ini_resolution=(1920, 1080)))
        self.assert_files({"fix.ini": None})

    def test_ratio_update_keeps_keys_decimals_and_every_other_byte(self) -> None:
        ori = catalog_mod("ori-and-the-will-of-the-wisps")
        original = b"\xef\xbb\xbf[AspectRatio]\r\nTargetWidth = 16.0 ; width\r\nTargetHeight = 10.0\r\n[FOV]\r\nFOVScale = 1.0\r\n"
        self.write({ori.ini_file: original})
        for (width, height), ratio in (((1920, 1080), (b"16.0", b"9.0")), ((1280, 800), (b"8.0", b"5.0"))):
            with self.subTest(resolution=(width, height)):
                self.assertEqual((True, True), update_existing_ini_resolution(ori, str(self.game), width, height))
                self.assert_files({ori.ini_file: original.replace(b"TargetWidth = 16.0", b"TargetWidth = " + ratio[0])
                                   .replace(b"TargetHeight = 10.0", b"TargetHeight = " + ratio[1])})

    def test_rewrite_changes_nothing_but_a_single_valid_declared_value(self) -> None:
        string = (IniResolutionPair("UserSettings", "Resolution", "Resolution", "pixels_string"),)
        custom = (IniResolutionPair("Custom Resolution", "Width", "Height", "pixels"),)
        other = b"[Patch:X]\nResolution=1x1\n"
        cases = (
            (catalog_mod("ffxvifix").ini_resolution, b"[Fix Resolution]\r\nWindowedResX = 0\r\nWindowedResY = 0\r\n", None),
            (custom, b"[Custom Resolution]\nWidth=1280\nWidth=invalid\nHeight=800\nTargetWidth=3840\nTargetHeight=2160\n",
             None),
            (string, b'[UserSettings]\nResolution="1280x800" ; comment\r\n' + other,
             b'[UserSettings]\nResolution="1920x1080" ; comment\r\n' + other),
            (string, b"[UserSettings]\nResolution=1280x800\n" + other, b"[UserSettings]\nResolution=1920x1080\n" + other),
            (string, b'[UserSettings]\nResolution="auto"\n' + other, None),
            (string, b'[UserSettings]\nResolution="1280x800\n' + other, None),
        )
        for pairs, before, after in cases:
            with self.subTest(before=before):
                self.assertEqual(after or before, rewrite_ini_resolution(before, 1920, 1080, pairs))

    def test_update_never_overwrites_a_concurrent_edit(self) -> None:
        fix = mod(archive_source(), "fix.ini", ini_file="fix.ini", ini_resolution=PIXELS_AND_RATIO)
        self.write({"fix.ini": "[Resolution]\nWidth=1280\nHeight=800\n"})
        edited = "[Resolution]\nWidth=1024\nHeight=768\n"
        with patch("utils.files.os.chmod", side_effect=lambda *_: self.write({"fix.ini": edited})):
            with self.assertRaisesRegex(ValueError, "changed during"):
                update_existing_ini_resolution(fix, str(self.game), 1920, 1080)
        self.assert_files({"fix.ini": edited})

    def test_apply_keeps_the_original_ini_when_the_adjustment_fails(self) -> None:
        fix = mod(archive_source(), "fix.ini", ini_file="fix.ini", ini_resolution=PIXELS_AND_RATIO)
        original = b"[Resolution]\nWidth=1280\nHeight=800\n"
        failures = (
            patch("installer.mod_ini.rewrite_ini_resolution", side_effect=ValueError("unsupported format")),
            patch("utils.files.write_bytes_atomic", side_effect=PermissionError("read-only staging filesystem")),
        )
        for failure in failures:
            with self.subTest(failure=failure.attribute), serving(zip_bytes({"fix.ini": original})), failure:
                warnings: list[str] = []
                self.apply(fix, replace_existing_files=True, ini_resolution=(1920, 1080),
                           ini_adjustment_warnings=warnings)
                self.assertEqual(["Could not adjust fix.ini; the original INI was kept."], warnings)
                self.assert_files({"fix.ini": original})


class IniEditorTests(GameTest):
    def test_reads_and_atomically_writes_the_catalogued_ini(self) -> None:
        fix = loose_mod({"config/fix.ini": "source/fix.ini"}, ini_file="config/fix.ini")
        self.assertEqual(("", False), read_mod_ini(fix, str(self.game)))
        self.assertTrue(write_mod_ini(fix, str(self.game), "[Fix]\nEnabled=1\n"))
        with patch("utils.files.os.chmod", side_effect=PermissionError("unsupported")):
            self.assertFalse(write_mod_ini(fix, str(self.game), "[Fix]\nEnabled=0\n"))
        self.assertEqual(("[Fix]\nEnabled=0\n", True), read_mod_ini(fix, str(self.game)))
        self.assert_no_leftovers()

    def test_rejects_an_oversized_ini_and_a_mod_without_one(self) -> None:
        fix = loose_mod({"fix.ini": "source/fix.ini"}, ini_file="fix.ini")
        with self.assertRaisesRegex(ValueError, "256 KiB"):
            write_mod_ini(fix, str(self.game), "x" * (256 * 1024 + 1))
        self.write({"fix.ini": b"x" * (256 * 1024 + 1)})
        with self.assertRaisesRegex(ValueError, "256 KiB"):
            read_mod_ini(fix, str(self.game))
        with self.assertRaisesRegex(ValueError, "does not define"):
            read_mod_ini(loose_mod({"fix.dll": "source/fix.dll"}), str(self.game))
