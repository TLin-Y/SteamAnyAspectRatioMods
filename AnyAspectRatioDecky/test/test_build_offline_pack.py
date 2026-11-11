import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from build_offline_pack import VERSION_PATTERN, _partition_paths  # noqa: E402


class OfflinePackBuildTests(unittest.TestCase):
    def test_partitions_paths_under_the_size_limit_with_catalog_overhead(self) -> None:
        mib = 1024 * 1024
        sizes = {"a.zip": 280 * mib, "b.zip": 280 * mib, "c.zip": 280 * mib}

        parts = _partition_paths(list(sizes), sizes, catalog_size=1 * mib)

        self.assertEqual([["a.zip", "b.zip"], ["c.zip"]], parts)
        for part in parts:
            uncompressed_size = 1 * mib + sum(sizes[path] for path in part)
            self.assertLessEqual(
                uncompressed_size + 16 * mib,
                800_000_000,
            )

    def test_rejects_an_individual_file_too_large_for_any_part(self) -> None:
        with self.assertRaisesRegex(ValueError, "too large to fit"):
            _partition_paths(
                ["large.zip"],
                {"large.zip": 770 * 1024 * 1024},
                catalog_size=1,
            )

    def test_version_pattern_allows_release_versions_and_dev(self) -> None:
        for version in ("dev", "1.8.0", "v1.8.0", "1.8"):
            with self.subTest(version=version):
                self.assertIsNotNone(VERSION_PATTERN.fullmatch(version))

    def test_version_pattern_rejects_path_components_and_filename_injection(self) -> None:
        for version in ("../outside", r"..\outside", "1.8.0/../../outside", "1.8.0.zip"):
            with self.subTest(version=version):
                self.assertIsNone(VERSION_PATTERN.fullmatch(version))


if __name__ == "__main__":
    unittest.main()
