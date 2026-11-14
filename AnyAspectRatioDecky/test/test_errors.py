import unittest

from helper import errors
from helper.errors import PluginError
from utils.network import NetworkError


class ErrorsTests(unittest.TestCase):
    def test_wrap_prefixes_failures_and_keeps_the_cause(self) -> None:
        with self.assertRaises(PluginError) as caught, errors.wrap("Unable to read matched games"):
            raise OSError("disk gone")
        self.assertEqual("Unable to read matched games: disk gone", str(caught.exception))
        self.assertIsInstance(caught.exception.__cause__, OSError)

    def test_wrap_leaves_bugs_alone(self) -> None:
        with self.assertRaises(KeyError), errors.wrap("Reading settings"):
            raise KeyError("missing")

    def test_collect_records_a_warning_and_continues(self) -> None:
        warnings: list[str] = []

        def first_line() -> str | None:
            with errors.collect(warnings, "Could not read app manifest"):
                raise PluginError("Unclosed object")
            return None

        self.assertIsNone(first_line())
        self.assertEqual(["Could not read app manifest: Unclosed object"], warnings)

    def test_network_errors_are_plugin_errors_and_os_errors(self) -> None:
        error = NetworkError("Could not reach github.com")
        self.assertIsInstance(error, PluginError)
        self.assertIsInstance(error, OSError)


if __name__ == "__main__":
    unittest.main()
