import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts import audit_depot_manifests as audit


class DepotManifestAuditTests(unittest.TestCase):
    def test_reads_realistic_paths_and_spaces(self) -> None:
        contents = (
            "Content Manifest for Depot 123\n"
            "Total number of files  : 2\n"
            " Size Chunks File SHA                                 Flags Name\n"
            "  123 1 0123456789abcdef0123456789abcdef01234567 0 "
            "Game Folder\\Binaries\\Game.exe\n"
            "  123 1 0123456789abcdef0123456789abcdef01234567 0 data.txt\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.txt"
            path.write_text(contents, encoding="utf-8")
            self.assertEqual(["Game Folder/Binaries/Game.exe"], audit.manifest_executables(path))
            path.write_text(contents.replace("files  : 2", "files  : 3"), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Incomplete manifest"):
                audit.manifest_executables(path)

    def test_audit_distinguishes_complete_partial_and_case(self) -> None:
        mod = {
            "id": "test", "name": "Test", "steam_app_id": "1", "install_dir": "Game",
            "executable": "bin/Game.exe", "files": ["bin/Fix.asi"],
            "source": {"kind": "archive", "path": "test.zip"},
        }
        complete = {"status": "complete", "unavailable_depots": []}
        partial = {"status": "partial", "unavailable_depots": ["Depot unavailable"]}
        self.assertEqual("present", audit.audit_mod(mod, partial, ["bin/Game.exe"])["executable_status"])
        self.assertEqual("case mismatch", audit.audit_mod(mod, complete, ["bin/game.exe"])["executable_status"])
        self.assertEqual("not found in accessible manifests", audit.audit_mod(mod, complete, [])["executable_status"])
        self.assertEqual("unknown (missing or incomplete manifests)",
                         audit.audit_mod(mod, partial, [])["executable_status"])
        self.assertEqual("unknown (missing or incomplete manifests)",
                         audit.audit_mod(mod, None, [])["executable_status"])
        self.assertEqual(["bin/Fix.asi"], audit.audit_mod(mod, complete, [])["asi_targets"])

    def test_partial_fetch_is_recorded_without_saving_transcript(self) -> None:
        class FakeProcess:
            stdout = io.StringIO("Depot 42 is not available from this account.\nSECRET TOKEN\n")

            def wait(self):
                return 0

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(audit, "DOWNLOAD_ROOT", root), \
                 contextlib.redirect_stdout(io.StringIO()):
                # The downloader writes text manifests into the supplied run directory.
                def process(command, **kwargs):
                    target = Path(command[command.index("-dir") + 1])
                    (target / "manifest_1_2.txt").write_text("manifest", encoding="utf-8")
                    return FakeProcess()

                with patch.object(audit.subprocess, "Popen", side_effect=process):
                    result = audit.fetch_app("123", None)
            self.assertEqual("partial", result["status"])
            self.assertEqual(["Depot 42 is not available from this account."],
                             result["unavailable_depots"])
            self.assertNotIn("SECRET TOKEN", (root / "manifests" / "123" / "latest.json").read_text())


if __name__ == "__main__":
    unittest.main()
