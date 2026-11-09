import hashlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch
from zipfile import ZipFile

from scanner import InstalledApp, SteamScanReport, match_supported_games
from scanner.local_archives import build_local_mods, scan_local_archives
from scanner.mod_catalog import ModSource, load_mod_catalog
from scanner.mod_installer import (
    OfflineModeError,
    apply_mod_files,
    open_url,
    set_archive_cache_dir,
    set_offline_mode,
    set_pack_dir,
)
from store import (
    PluginStore,
    Settings,
    load_active_catalog,
    refresh_catalog_on_start,
    update_dependencies,
    validate_settings,
)
from store.dependencies import _with_bundled_resolution_metadata


def write_zip(path: Path, files: dict[str, bytes]) -> None:
    with ZipFile(path, "w") as archive:
        for name, content in files.items():
            archive.writestr(name, content)


def response(content: bytes) -> Mock:
    stream = io.BytesIO(content)
    mock = Mock()
    mock.__enter__ = Mock(return_value=stream)
    mock.__exit__ = Mock(return_value=False)
    return mock


class SettingsTests(unittest.TestCase):
    def test_legacy_official_catalog_uses_only_hash_matched_bundled_metadata(self) -> None:
        from dataclasses import replace

        mod = next(m for m in load_mod_catalog() if m.mod_id == "ori-and-the-will-of-the-wisps")
        legacy = replace(mod, ini_resolution=())
        self.assertEqual(mod.ini_resolution, _with_bundled_resolution_metadata((legacy,), Settings())[0].ini_resolution)
        changed = replace(legacy, source=replace(legacy.source, sha256="0" * 64))
        self.assertEqual((), _with_bundled_resolution_metadata((changed,), Settings())[0].ini_resolution)
        self.assertEqual((), _with_bundled_resolution_metadata(
            (legacy,), Settings(catalog_repository_url="https://github.com/example/other")
        )[0].ini_resolution)

    def test_accepts_defaults_and_github_repositories(self) -> None:
        self.assertEqual(Settings(), validate_settings(Settings()))
        settings = validate_settings(
            Settings("https://github.com/owner/repo.git/", "feature/x", "a/b.json", "")
        )
        self.assertEqual("https://github.com/owner/repo", settings.catalog_repository_url)

    def test_rejects_unsafe_values(self) -> None:
        for bad in (
            Settings(catalog_repository_url="http://github.com/o/r"),
            Settings(catalog_repository_url="https://example.com/o/r"),
            Settings(catalog_repository_url="https://github.com/o"),
            Settings(catalog_ref="a..b"),
            Settings(catalog_path="../catalog.json"),
            Settings(catalog_path="catalog.txt"),
            Settings(local_archive_dir="relative/dir"),
            Settings(local_archive_dir="/definitely/not/a/folder"),
        ):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate_settings(bad)

    def test_settings_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = PluginStore(Path(directory))
            self.assertEqual(Settings(), store.load_settings())
            store.save_settings(
                Settings(
                    catalog_ref="dev",
                    ini_resolution_width=1920,
                    ini_resolution_height=1080,
                )
            )
            loaded = store.load_settings()
            self.assertEqual("dev", loaded.catalog_ref)
            self.assertEqual((1920, 1080), (
                loaded.ini_resolution_width,
                loaded.ini_resolution_height,
            ))

    def test_rejects_invalid_ini_resolution_dimensions(self) -> None:
        for width, height in ((0, 800), (1280, -1), (16385, 800), (True, 800)):
            with self.subTest(width=width, height=height), self.assertRaises(ValueError):
                validate_settings(
                    Settings(
                        ini_resolution_width=width,
                        ini_resolution_height=height,
                    )
                )


class DependencyUpdateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = PluginStore(Path(self.directory.name))
        self.store.ensure_directories()
        self.catalog_bytes = (
            Path(__file__).resolve().parents[1] / "catalog.json"
        ).read_bytes()

    def test_uses_bundled_catalog_until_updated(self) -> None:
        source = load_active_catalog(self.store, Settings())
        self.assertEqual("bundled", source.origin)

    def test_update_caches_catalog_and_reports_changes(self) -> None:
        document = json.loads(self.catalog_bytes)
        document["mods"] = document["mods"][:2]
        remote = json.dumps(document).encode()
        stale_archive = self.store.archive_cache_dir / "old.zip"
        stale_archive.write_bytes(b"x")

        with patch("scanner.mod_installer.urlopen", return_value=response(remote)) as opener:
            result = update_dependencies(self.store, Settings(catalog_ref="dev"))
        self.assertIn("/dev/AnyAspectRatioDecky/catalog.json", opener.call_args.args[0])
        self.assertTrue(result["catalog_changed"])
        self.assertEqual(2, result["mod_count"])
        self.assertFalse(stale_archive.exists())

        source = load_active_catalog(self.store, Settings(catalog_ref="dev"))
        self.assertEqual(("cache", 2), (source.origin, len(source.mods)))
        self.assertEqual("dev", source.mods[0].source.repository_ref)
        # A cache fetched for another source must not be used.
        self.assertEqual("bundled", load_active_catalog(self.store, Settings()).origin)

        with patch("scanner.mod_installer.urlopen", return_value=response(remote)):
            again = update_dependencies(self.store, Settings(catalog_ref="dev"))
        self.assertFalse(again["catalog_changed"])

    def test_invalid_remote_catalog_keeps_previous_cache(self) -> None:
        with patch("scanner.mod_installer.urlopen", return_value=response(self.catalog_bytes)):
            update_dependencies(self.store, Settings())
        with patch("scanner.mod_installer.urlopen", return_value=response(b"{not json")):
            with self.assertRaises(ValueError):
                update_dependencies(self.store, Settings())
        self.assertEqual("cache", load_active_catalog(self.store, Settings()).origin)
        self.assertEqual([], list(self.store.cache_dir.glob("*.tmp")))

    def test_startup_refresh_fetches_and_clears_archives_only_on_change(self) -> None:
        stale = self.store.archive_cache_dir / "old.zip"
        stale.write_bytes(b"x")
        with patch("scanner.mod_installer.urlopen", return_value=response(self.catalog_bytes)):
            self.assertTrue(refresh_catalog_on_start(self.store, Settings()))
        self.assertFalse(stale.exists())
        self.assertEqual("cache", load_active_catalog(self.store, Settings()).origin)

        stale.write_bytes(b"x")
        with patch("scanner.mod_installer.urlopen", return_value=response(self.catalog_bytes)):
            self.assertFalse(refresh_catalog_on_start(self.store, Settings()))
        self.assertTrue(stale.exists())

    def test_startup_refresh_failure_keeps_working_catalog(self) -> None:
        with patch("scanner.mod_installer.urlopen", side_effect=OSError("offline")):
            with self.assertRaises(OSError):
                refresh_catalog_on_start(self.store, Settings())
        self.assertEqual("bundled", load_active_catalog(self.store, Settings()).origin)

    def test_startup_refresh_replaces_an_imported_offline_pack_catalog(self) -> None:
        with patch("scanner.mod_installer.urlopen", return_value=response(self.catalog_bytes)):
            update_dependencies(self.store, Settings())
        meta = json.loads(self.store.catalog_meta_path.read_text(encoding="utf-8"))
        meta["source_url"] = "offline pack"
        meta["sha256"] = "0" * 64
        self.store.catalog_meta_path.write_text(json.dumps(meta), encoding="utf-8")
        with patch("scanner.mod_installer.urlopen", return_value=response(self.catalog_bytes)):
            self.assertTrue(refresh_catalog_on_start(self.store, Settings()))
        meta = json.loads(self.store.catalog_meta_path.read_text(encoding="utf-8"))
        self.assertNotEqual("offline pack", meta["source_url"])

    def test_corrupt_cached_catalog_falls_back_with_warning(self) -> None:
        with patch("scanner.mod_installer.urlopen", return_value=response(self.catalog_bytes)):
            update_dependencies(self.store, Settings())
        self.store.catalog_path.write_text("{}", encoding="utf-8")
        source = load_active_catalog(self.store, Settings())
        self.assertEqual("bundled", source.origin)
        self.assertIsNotNone(source.warning)


class BackendEntryPointTests(unittest.TestCase):
    def test_every_runtime_module_compiles(self) -> None:
        import py_compile

        root = Path(__file__).resolve().parent.parent
        modules = [root / "main.py", *root.glob("scanner/*.py"), *root.glob("store/*.py")]
        for module in modules:
            with self.subTest(module=module.name):
                py_compile.compile(str(module), doraise=True)


class NetworkErrorTests(unittest.TestCase):
    def test_url_failures_become_plain_network_errors(self) -> None:
        import socket
        from urllib.error import HTTPError, URLError

        from scanner.mod_installer import NetworkError, open_url

        cases = {
            HTTPError("https://x/y", 404, "Not Found", {}, None): "HTTP 404",
            URLError(socket.gaierror(-2, "Name or service not known")): "No internet",
            URLError(TimeoutError("timed out")): "Timed out",
        }
        for error, expected in cases.items():
            with self.subTest(expected=expected):
                with patch("scanner.mod_installer.urlopen", side_effect=error):
                    with self.assertRaises(NetworkError) as raised:
                        open_url("https://raw.githubusercontent.com/a/b")
                self.assertIn(expected, str(raised.exception))
                self.assertIn("raw.githubusercontent.com", str(raised.exception))


class OfflineModeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.store = PluginStore(Path(self.directory.name))
        self.store.ensure_directories()
        self.addCleanup(set_offline_mode, False)

    def test_flag_is_saved_and_survives_validation(self) -> None:
        self.store.save_settings(Settings(offline_mode=True))
        self.assertTrue(self.store.load_settings().offline_mode)
        self.assertTrue(validate_settings(Settings(offline_mode=True)).offline_mode)

    def test_non_boolean_flag_in_the_file_is_ignored(self) -> None:
        self.store.settings_path.write_text('{"offline_mode": "yes"}', encoding="utf-8")
        self.assertFalse(self.store.load_settings().offline_mode)

    def test_no_network_connection_is_attempted(self) -> None:
        set_offline_mode(True)
        with patch("scanner.mod_installer.urlopen", side_effect=AssertionError("network used")):
            with self.assertRaises(OfflineModeError):
                open_url("https://raw.githubusercontent.com/o/r/main/a.zip")
            with self.assertRaises(OSError):
                update_dependencies(self.store, Settings(offline_mode=True))
            self.assertIsNone(refresh_catalog_on_start(self.store, Settings(offline_mode=True)))

    def test_turning_it_off_restores_network_access(self) -> None:
        set_offline_mode(True)
        set_offline_mode(False)
        with patch("scanner.mod_installer.urlopen", return_value=response(b"x")) as opener:
            open_url("https://raw.githubusercontent.com/o/r/main/a.zip")
        opener.assert_called_once()


class LocalArchiveTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.archives = self.root / "zips"
        self.archives.mkdir()
        self.game = self.root / "steamapps" / "common" / "Cool Game"
        (self.game / "bin").mkdir(parents=True)
        (self.game / "bin" / "CoolGame.exe").write_bytes(b"exe")
        (self.game / "bin" / "UnityCrashHandler64.exe").write_bytes(b"x")
        self.app = InstalledApp("42424", "Cool Game", "Cool Game", str(self.root), str(self.game))
        self.addCleanup(set_archive_cache_dir, None)

    def test_scan_matches_by_name_installs_next_to_executable(self) -> None:
        write_zip(
            self.archives / "Cool-Game-v1.2.0.zip",
            {"dinput8.dll": b"dll", "fix.ini": b"ini", "__MACOSX/x": b"junk"},
        )
        write_zip(self.archives / "Other Game.zip", {"a.txt": b"a"})
        (self.archives / "broken.zip").write_bytes(b"not a zip")
        index = self.root / "index.json"

        archives, warnings = scan_local_archives(str(self.archives), index)
        self.assertEqual(["Cool-Game-v1.2.0", "Other Game"], [a.name for a in archives])
        self.assertTrue(any("broken.zip" in warning for warning in warnings))
        self.assertTrue(index.is_file())

        mods, match_warnings = build_local_mods(archives, [self.app], set())
        self.assertEqual(1, len(mods))
        mod = mods[0]
        self.assertEqual("local-42424", mod.mod_id)
        self.assertEqual("bin", mod.source.destination)
        self.assertEqual(("bin/dinput8.dll", "bin/fix.ini"), mod.files)
        self.assertEqual(("dinput8=n,b",), mod.wine_dll_overrides)
        self.assertTrue(any("Other Game" in warning for warning in match_warnings))

    def test_index_is_reused_when_archive_is_unchanged(self) -> None:
        path = self.archives / "Cool Game.zip"
        write_zip(path, {"a.dll": b"1"})
        index = self.root / "index.json"
        scan_local_archives(str(self.archives), index)
        with patch("scanner.local_archives._list_archive_files") as listing:
            archives, _ = scan_local_archives(str(self.archives), index)
        listing.assert_not_called()
        self.assertEqual(1, len(archives))

    def test_matches_app_id_in_file_name_and_catalog_wins(self) -> None:
        write_zip(self.archives / "anything [42424].zip", {"a.dll": b"1"})
        archives, _ = scan_local_archives(str(self.archives))
        mods, _ = build_local_mods(archives, [self.app], set())
        self.assertEqual(1, len(mods))
        mods, warnings = build_local_mods(archives, [self.app], {"42424"})
        self.assertEqual(0, len(mods))
        self.assertTrue(any("catalog mod" in warning for warning in warnings))

    def test_rejects_archives_with_unsafe_entries(self) -> None:
        write_zip(self.archives / "Evil.zip", {"../escape.dll": b"1"})
        archives, warnings = scan_local_archives(str(self.archives))
        self.assertEqual([], archives)
        self.assertTrue(any("unsafe" in warning for warning in warnings))

    def test_apply_extracts_local_archive_and_shows_in_scan_report(self) -> None:
        write_zip(self.archives / "Cool Game.zip", {"mod.dll": b"new", "data/x.bin": b"x"})
        archives, _ = scan_local_archives(str(self.archives))
        mods, _ = build_local_mods(archives, [self.app], set())
        steam = SteamScanReport(1, 1, [self.app], [])
        report = match_supported_games(steam, load_mod_catalog(), mods)
        game = next(g for g in report.supported_games if g.mod_id == "local-42424")
        self.assertEqual("local_archive", game.source_kind)
        self.assertEqual("not_detected", game.file_status)

        self.assertEqual([], apply_mod_files(mods[0], str(self.game)))
        self.assertEqual(b"new", (self.game / "bin" / "mod.dll").read_bytes())
        self.assertEqual(b"x", (self.game / "bin" / "data" / "x.bin").read_bytes())

    def test_unhashed_remote_archive_is_refetched_between_applies(self) -> None:
        cache = self.root / "cache"
        set_archive_cache_dir(cache)
        archive_path = self.root / "remote.zip"
        write_zip(archive_path, {"mod.dll": b"remote"})
        mod = load_mod_catalog()[0]
        mod = mod.__class__(
            mod_id="m",
            name="m",
            steam_app_id="1",
            install_dir="x",
            executable="x.exe",
            source=ModSource(
                "archive", "pkg/m.zip", "", (), "https://github.com/o/r", "main"
            ),
            files=("mod.dll",),
            ini_file=None,
            wine_dll_overrides=(),
        )
        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda *a, **k: response(archive_path.read_bytes()),
        ) as opener:
            apply_mod_files(mod, str(self.game))
            (self.game / "mod.dll").unlink()
            apply_mod_files(mod, str(self.game))
        self.assertEqual(2, opener.call_count)
        self.assertEqual(1, len(list(cache.glob("*.zip"))))

    def test_replaced_archive_is_downloaded_again_when_checksum_changes(self) -> None:
        cache = self.root / "cache"
        set_archive_cache_dir(cache)
        versions = {}
        for label in ("old", "new"):
            path = self.root / f"{label}.zip"
            write_zip(path, {"mod.dll": label.encode()})
            versions[label] = (path.read_bytes(), hashlib.sha256(path.read_bytes()).hexdigest())

        def make_mod(sha256: str):
            return load_mod_catalog()[0].__class__(
                mod_id="m",
                name="m",
                steam_app_id="1",
                install_dir="x",
                executable="x.exe",
                source=ModSource(
                    "archive", "pkg/m.zip", "", (), "https://github.com/o/r", "main", sha256
                ),
                files=("mod.dll",),
                ini_file=None,
                wine_dll_overrides=(),
            )

        served = {"data": versions["old"][0]}
        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda *a, **k: response(served["data"]),
        ) as opener:
            apply_mod_files(make_mod(versions["old"][1]), str(self.game))
            self.assertEqual(b"old", (self.game / "mod.dll").read_bytes())
            (self.game / "mod.dll").unlink()
            served["data"] = versions["new"][0]
            apply_mod_files(make_mod(versions["new"][1]), str(self.game))
            self.assertEqual(b"new", (self.game / "mod.dll").read_bytes())
            self.assertEqual(2, opener.call_count)
            self.assertEqual(1, len(list(cache.glob("*.zip"))))

            served["data"] = versions["old"][0]
            with self.assertRaises(ValueError):
                apply_mod_files(make_mod("0" * 64), str(self.game))

    def test_outdated_pack_and_corrupt_cache_do_not_replace_current_archive(self) -> None:
        cache = self.root / "cache"
        pack = self.root / "pack"
        packed_zip = pack / "pkg" / "m.zip"
        packed_zip.parent.mkdir(parents=True)
        write_zip(packed_zip, {"mod.dll": b"old"})
        current_zip = self.root / "current.zip"
        write_zip(current_zip, {"mod.dll": b"new"})
        content = current_zip.read_bytes()
        mod = load_mod_catalog()[0].__class__(
            mod_id="m", name="m", steam_app_id="1", install_dir="x", executable="x.exe",
            source=ModSource(
                "archive", "pkg/m.zip", "", (), "https://github.com/o/r", "main",
                hashlib.sha256(content).hexdigest(),
            ),
            files=("mod.dll",), ini_file=None, wine_dll_overrides=(),
        )
        set_pack_dir(pack)
        set_archive_cache_dir(cache)
        self.addCleanup(set_pack_dir, None)
        self.addCleanup(set_offline_mode, False)
        set_offline_mode(True)
        with self.assertRaisesRegex(OfflineModeError, "outdated mod archive"):
            apply_mod_files(mod, str(self.game))
        self.assertFalse((self.game / "mod.dll").exists())

        set_offline_mode(False)
        with patch(
            "scanner.mod_installer.urlopen", side_effect=lambda *a, **k: response(content)
        ) as opener:
            apply_mod_files(mod, str(self.game))
            self.assertEqual(b"new", (self.game / "mod.dll").read_bytes())
            cached, = cache.glob("*.zip")
            cached.write_bytes(b"corrupt")
            (self.game / "mod.dll").unlink()
            apply_mod_files(mod, str(self.game))
            self.assertEqual(b"new", (self.game / "mod.dll").read_bytes())
            self.assertEqual(2, opener.call_count)


if __name__ == "__main__":
    unittest.main()
