import unittest

from utils.steam_vdf import VdfParseError, parse_binary_vdf, parse_vdf


class SteamVdfTests(unittest.TestCase):
    def test_text_vdf_nests_objects(self) -> None:
        parsed = parse_vdf(
            '"libraryfolders" { "0" { "path" "/home/deck/Steam" } '
            '"1" { "path" "/run/media/deck/SD Card" "apps" { "123" "1" } } }'
        )
        self.assertEqual("/home/deck/Steam", parsed["libraryfolders"]["0"]["path"])
        self.assertEqual("1", parsed["libraryfolders"]["1"]["apps"]["123"])
        with self.assertRaises(VdfParseError):
            parse_vdf('"AppState" { "appid" "123"')

    def test_binary_vdf_reads_maps_strings_and_unsigned_numbers(self) -> None:
        data = b"\x00shortcuts\x00\x000\x00\x01AppName\x00Game\x00\x02appid\x00\x01\x00\x00\x80\x08\x08\x08"
        self.assertEqual({"shortcuts": {"0": {"AppName": "Game", "appid": 0x80000001}}}, parse_binary_vdf(data))
        for broken in (data[:-3], data[:-8], b"\x01name", b"\x07key\x00\x08"):
            with self.subTest(data=broken), self.assertRaises(VdfParseError):
                parse_binary_vdf(broken)
