import unittest

from utils import ini


class IniTests(unittest.TestCase):
    def test_values_keep_their_position_after_a_bom_and_before_a_comment(self) -> None:
        lines = b"\xef\xbb\xbf[ Resolution ]\r\n  Width =\t1280.0  ; pixels\r\n1Bad=1\nHeight=800\r".splitlines(keepends=True)
        sections = ini.values_by_section(lines)
        self.assertEqual([b"resolution"], list(sections))
        self.assertEqual({b"width", b"height"}, set(sections[b"resolution"]))
        (width,), (height,) = sections[b"resolution"][b"width"], sections[b"resolution"][b"height"]
        self.assertEqual((b"1280.0", b"800"), (width.read(lines), height.read(lines)))

    def test_number_and_resolution_formats(self) -> None:
        for value, expected in ((b"1280", True), (b"16.00", True), (b"", False), (b".5", False), (b"5.", False),
                                (b"1.2.3", False), (b"-5", False), ("١".encode(), False)):
            with self.subTest(value=value):
                self.assertIs(expected, ini.is_decimal(value))
        for value, expected in ((b"1280x800", b""), (b'"1280x800"', b'"'), (b"'1920x1080'", b"'"),
                                (b'"1280x800', None), (b"0x800", None), (b"1280X800", None), (b"auto", None)):
            with self.subTest(value=value):
                self.assertEqual(expected, ini.resolution_quote(value))
        for value, expected in ((b"auto", b""), (b'"AUTO"', b'"'), (b"'auto'", b"'"), (b'"auto', None),
                                (b"automatic", None), (b"1280x800", None)):
            with self.subTest(value=value):
                self.assertEqual(expected, ini.auto_quote(value))
