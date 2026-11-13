import gzip
import json
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

import steam_pics  # noqa: E402
import verify_catalog_steam as verify  # noqa: E402

APPINFO = """
"appinfo"
{
	"appid"		"2054970"
	"common" { "name" "Dragon's Dogma 2" "type" "Game" }
	"config"
	{
		"installdir"		"Dragons Dogma 2"
		"launch"
		{
			"0" { "executable" "DD2.exe" "type" "default" "config" { "oslist" "windows" } }
			"1" { "executable" "\\\\Tools\\\\Setup.exe" }
			"2" { "executable" "dd2.sh" "config" { "oslist" "linux" } }
		}
	}
}
"""


def mod(**overrides):
    entry = {"id": "dd2", "steam_app_id": "2054970", "install_dir": "Dragons Dogma 2", "executable": "DD2.exe"}
    entry.update(overrides)
    return entry


class KeyValuesTest(unittest.TestCase):
    def test_parses_nested_blocks_and_escapes(self):
        app = steam_pics.parse_keyvalues(APPINFO)["appinfo"]
        self.assertEqual(app["config"]["installdir"], "Dragons Dogma 2")
        self.assertEqual(app["config"]["launch"]["1"]["executable"], "\\Tools\\Setup.exe")

    def test_windows_launches_skip_other_platforms_and_normalize(self):
        app = steam_pics.parse_keyvalues(APPINFO)["appinfo"]
        self.assertEqual(verify.windows_launches(app), ["DD2.exe", "Tools/Setup.exe"])


class ProtocolTest(unittest.TestCase):
    def test_protobuf_round_trip(self):
        data = steam_pics.Proto().varint(1, 300).fixed64(2, 1 << 60).bytes(3, b"abc").out
        parsed = steam_pics.fields(data)
        self.assertEqual(parsed[1], [300])
        self.assertEqual(parsed[2], [1 << 60])
        self.assertEqual(parsed[3], [b"abc"])

    def test_multi_message_is_unpacked_and_unzipped(self):
        inner = steam_pics._message(751, b"h", b"body")
        packed = struct.pack("<I", len(inner)) + inner
        multi = steam_pics.Proto().varint(1, len(packed)).bytes(2, gzip.compress(packed)).out
        outer = steam_pics._message(steam_pics.EMSG_MULTI, b"", multi)
        self.assertEqual(steam_pics.messages(outer), [(751, b"h", b"body")])


class CheckModTest(unittest.TestCase):
    def setUp(self):
        self.app = steam_pics.parse_keyvalues(APPINFO)["appinfo"]

    def test_matching_entry_passes(self):
        self.assertEqual(verify.check_mod(mod(), self.app, {}), [])
        self.assertEqual(verify.check_mod(mod(executable="tools\\setup.exe"), self.app, {}), [])
        self.assertEqual(verify.check_mod(mod(executable=None), self.app, {}), [])

    def test_unknown_app_fails(self):
        self.assertIn("no public app", verify.check_mod(mod(), None, {})[0])

    def test_install_dir_must_match_exactly(self):
        problems = verify.check_mod(mod(install_dir="dragons dogma 2"), self.app, {})
        self.assertEqual(len(problems), 1)
        self.assertIn("install_dir", problems[0])

    def test_alternative_executable_counts(self):
        entry = mod(executable="Game/Real.exe", executable_alternatives=["DD2.exe"])
        self.assertEqual(verify.check_mod(entry, self.app, {}), [])

    def test_exception_applies_only_while_steam_launch_list_is_unchanged(self):
        entry = mod(executable="Game/Real.exe")
        self.assertEqual(len(verify.check_mod(entry, self.app, {})), 1)
        current = {"dd2": {"steam_launch": ["Tools/Setup.exe", "DD2.exe"], "reason": "launcher"}}
        self.assertEqual(verify.check_mod(entry, self.app, current), [])
        stale = {"dd2": {"steam_launch": ["Old.exe"], "reason": "launcher"}}
        self.assertIn("changed", verify.check_mod(entry, self.app, stale)[0])

    def test_unneeded_and_orphan_exceptions_are_reported(self):
        exceptions = {"dd2": {"steam_launch": [], "reason": ""}, "gone": {"steam_launch": [], "reason": ""}}
        failures, notes = verify.verify([mod()], {2054970: self.app}, exceptions)
        self.assertEqual(failures, {})
        self.assertEqual(len(notes), 2)


class ExceptionsFileTest(unittest.TestCase):
    def test_exceptions_name_catalog_mods_and_give_reasons(self):
        catalog = json.loads(verify.CATALOG_PATH.read_text(encoding="utf-8"))
        ids = {entry["id"] for entry in catalog["mods"]}
        for mod_id, exception in verify.load_exceptions().items():
            self.assertIn(mod_id, ids)
            self.assertTrue(exception.get("reason"), mod_id)
            self.assertIsInstance(exception.get("steam_launch"), list, mod_id)


if __name__ == "__main__":
    unittest.main()
