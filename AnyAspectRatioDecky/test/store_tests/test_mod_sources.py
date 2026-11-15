import hashlib
import shutil

from support.builders import RAW, archive_source, directory_source, mod, write_zip, zip_bytes
from support.fakes import fetched, offline, serving
from support.sandbox import GameTest

from store.mod_sources import fetch_mod_readme, mod_readme_path, set_archive_cache_dir, set_pack_dir
from utils.network import OfflineModeError


def archive_mod(content: bytes | None = None, *files: str):
    sha256 = hashlib.sha256(content).hexdigest() if content is not None else ""
    return mod(archive_source("pkg/m.zip", sha256=sha256), *(files or ("mod.dll",)))


class ModSourceTests(GameTest):
    def setUp(self) -> None:
        super().setUp()
        self.cache = self.root / "cache"
        set_archive_cache_dir(self.cache)

    def cached(self) -> list:
        return list(self.cache.glob("*.zip"))

    def test_readme_comes_from_the_top_folder_of_a_nested_source(self) -> None:
        nested = mod(directory_source("Ninja Gaiden Sigma 2/nested/mod"), "Game/fix.dll")
        self.assertEqual("Ninja Gaiden Sigma 2/README.md", mod_readme_path(nested))
        with serving(b"# Mod\n\nRead me") as network:
            self.assertEqual("# Mod\n\nRead me", fetch_mod_readme(nested))
        self.assertEqual([f"{RAW}/Ninja%20Gaiden%20Sigma%202/README.md"], fetched(network))

    def test_unhashed_archive_is_fetched_again_on_every_apply(self) -> None:
        with serving(zip_bytes({"mod.dll": "remote"})) as network:
            for _ in range(2):
                self.apply(archive_mod())
                (self.game / "mod.dll").unlink()
        self.assertEqual((2, 1), (network.call_count, len(self.cached())))

    def test_a_new_checksum_downloads_the_new_archive_and_a_wrong_one_is_refused(self) -> None:
        versions = {label: zip_bytes({"mod.dll": label}) for label in ("old", "new")}
        served = {"data": versions["old"]}
        with serving(lambda url: served["data"]) as network:
            for label in ("old", "new"):
                served["data"] = versions[label]
                self.apply(archive_mod(versions[label]))
                self.assert_files({"mod.dll": label})
                (self.game / "mod.dll").unlink()
            self.assertEqual((2, 1), (network.call_count, len(self.cached())))
            served["data"] = versions["old"]
            with self.assertRaises(ValueError):
                self.apply(mod(archive_source("pkg/m.zip", sha256="0" * 64), "mod.dll"))

    def test_outdated_pack_or_corrupt_cache_never_replaces_the_current_archive(self) -> None:
        current = zip_bytes({"mod.dll": "new"})
        pack = self.root / "pack"
        write_zip(pack / "pkg/m.zip", {"mod.dll": "old"})
        set_pack_dir(pack)
        with offline(), self.assertRaisesRegex(OfflineModeError, "outdated mod archive"):
            self.apply(archive_mod(current))
        self.assert_files({"mod.dll": None})

        with serving(current) as network:
            self.apply(archive_mod(current))
            self.assert_files({"mod.dll": "new"})
            (cached,) = self.cached()
            cached.write_bytes(b"corrupt")
            (self.game / "mod.dll").unlink()
            self.apply(archive_mod(current))
        self.assert_files({"mod.dll": "new"})
        self.assertEqual(2, network.call_count)

    def test_failed_apply_keeps_a_verified_cached_archive(self) -> None:
        current = zip_bytes({"bin/mod.dll": "new"})
        with serving(current) as network:
            self.apply(archive_mod(current, "bin/mod.dll"))
            shutil.rmtree(self.game / "bin")
            self.write({"bin": ""})  # staging beside the target fails while the archive is open
            with self.assertRaises(OSError):
                self.apply(archive_mod(current, "bin/mod.dll"))
            self.assertEqual(1, len(self.cached()))
            (self.game / "bin").unlink()
            self.apply(archive_mod(current, "bin/mod.dll"))
        self.assertEqual(1, network.call_count)
        self.assert_files({"bin/mod.dll": "new"})
