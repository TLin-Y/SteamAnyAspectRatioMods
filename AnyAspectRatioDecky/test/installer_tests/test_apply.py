import hashlib
import ssl
from unittest.mock import patch

from support.builders import RAW, archive_source, directory_source, loose_mod, mod, zip_bytes
from support.fakes import fetched, serving
from support.sandbox import GameTest

FIX = {"fix.dll": "source/fix.dll", "fix.ini": "source/fix.ini"}


class ApplyTests(GameTest):
    def test_archive_installs_catalogued_files_over_verified_https_and_keeps_others(self) -> None:
        fix = mod(archive_source(destination="Game"), "Game/fix.dll", "Game/fix.ini")
        self.write({"Game/fix.dll": "old dll", "Game/user.txt": "keep"})
        with serving(zip_bytes({"fix.dll": b"new dll", "fix.ini": b"new ini"})) as network:
            self.assertEqual([], self.apply(fix, replace_existing_files=True))
        self.assert_files({"Game/fix.dll": "new dll", "Game/fix.ini": "new ini", "Game/user.txt": "keep"})
        self.assertEqual([f"{RAW}/fixes/test.zip"], fetched(network))
        self.assertEqual(30, network.call_args.kwargs["timeout"])
        self.assertEqual(ssl.CERT_REQUIRED, network.call_args.kwargs["context"].verify_mode)

    def test_directory_and_loose_file_sources_are_copied(self) -> None:
        cases = (
            (mod(directory_source("mods/test"), "bin/fix.dll"),
             {"mods/test/bin/fix.dll": b"directory"}, {"bin/fix.dll": "directory"}),
            (loose_mod({"bin/fix.dll": "assets/fix.dll", "settings.ini": "assets/settings.ini"}),
             {"assets/fix.dll": b"individual dll", "assets/settings.ini": b"individual ini"},
             {"bin/fix.dll": "individual dll", "settings.ini": "individual ini"}),
        )
        for fix, served, installed in cases:
            with self.subTest(kind=fix.source.kind), serving(served):
                self.apply(fix, replace_existing_files=True)
                self.assert_files(installed)

    def test_missing_archive_member_changes_nothing(self) -> None:
        self.write({"fix.dll": "keep old"})
        with serving(zip_bytes({"fix.dll": b"new"})), self.assertRaisesRegex(ValueError, "missing fix.ini"):
            self.apply(mod(archive_source(), "fix.dll", "fix.ini"))
        self.assert_files({"fix.dll": "keep old", "fix.ini": None})

    def test_different_existing_file_is_asked_about_and_identical_one_is_not(self) -> None:
        fix = loose_mod(FIX)
        self.write({"fix.dll": "another mod's binary"})
        with serving({"source/fix.dll": b"catalog binary", "source/fix.ini": b"catalog configuration"}):
            self.assertEqual(["fix.dll"], self.apply(fix))
            self.assert_files({"fix.dll": "another mod's binary", "fix.ini": None})
            self.assertEqual(["fix.dll"], self.apply(fix, replace_existing_files=False))
            self.assert_files({"fix.dll": "another mod's binary", "fix.ini": "catalog configuration"})
            self.write({"fix.dll": "catalog binary"})
            self.assertEqual([], self.apply(fix))

    def test_modified_ini_is_asked_about_before_anything_is_written(self) -> None:
        fix = loose_mod(FIX, ini_file="fix.ini")
        self.write({"fix.ini": "user configuration"})
        with serving({"source/fix.dll": b"mod binary", "source/fix.ini": b"default configuration"}):
            self.assertEqual(["fix.ini"], self.apply(fix))
            self.assert_files({"fix.dll": None, "fix.ini": "user configuration"})
            self.assertEqual(["fix.ini"], self.apply(fix, replace_existing_files=False))
            self.assert_files({"fix.dll": "mod binary", "fix.ini": "user configuration"})
            self.assertEqual([], self.apply(fix, replace_existing_files=True))
            self.assert_files({"fix.dll": "mod binary", "fix.ini": "default configuration"})

    def test_size_limits_stop_the_apply_before_any_file_is_written(self) -> None:
        archived = mod(archive_source(), "fix.dll")
        archive = zip_bytes({"fix.dll": b"payload"})
        cases = (
            ("store.mod_sources.MAX_MOD_DOWNLOAD_SIZE_BYTES", 4, archived, archive, "archive download exceeds"),
            ("store.mod_sources.MAX_MOD_FILE_SIZE_BYTES", 4, archived, archive, "Mod file exceeds"),
            ("store.mod_sources.MAX_MOD_FILE_SIZE_BYTES", 4, loose_mod({"fix.dll": "source/fix.dll"}), b"payload",
             "Mod file exceeds"),
            ("installer.staging.MAX_MOD_TOTAL_SIZE_BYTES", 3,
             loose_mod({"first.dll": "source/first.dll", "second.dll": "source/second.dll"}), b"12", "total limit"),
        )
        for limit, size, fix, served, error in cases:
            with self.subTest(limit=limit, kind=fix.source.kind):
                with patch(limit, size), serving(served), self.assertRaisesRegex(ValueError, error):
                    self.apply(fix)
                self.assert_files(dict.fromkeys(fix.files))

    def test_loose_files_must_match_the_catalog_hashes(self) -> None:
        mapping = {"first.dll": "source/first.dll", "second.dll": "source/second.dll"}
        good = hashlib.sha256(b"payload").hexdigest()
        with serving(b"payload"):
            with self.assertRaisesRegex(ValueError, "second.dll does not match the catalog checksum"):
                self.apply(loose_mod(mapping, hashes={"second.dll": "0" * 64}))
            self.assert_files({"first.dll": None, "second.dll": None})
            self.assertEqual([], self.apply(loose_mod(mapping, hashes=dict.fromkeys(mapping, good))))
        self.assert_files({"first.dll": "payload", "second.dll": "payload"})
