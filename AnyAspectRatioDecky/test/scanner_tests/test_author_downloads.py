import json
import os
from pathlib import Path

from support.builders import app_for, catalog_mod, write_zip
from support.catalog import CatalogEditTest
from support.sandbox import GameTest

from scanner.author_downloads import (
    applied_download,
    archive_root,
    downloads_folder,
    find_downloads,
    import_download,
    keep_download,
    scan_download_folders,
    stored_path,
)
from scanner.steam_library_scanner import SteamScanReport
from scanner.supported_game_scanner import add_author_downloads, match_supported_games

FFX = "final-fantasy-x-x2-hd-remaster-suwsf"
FOLDER = "Final Fantasy XX-2 HD Remaster 2560x1080 64x27 SUWSF"
INI = '[UserSettings]\nResolution="auto"\nEnabled=true\n'


def _release(path: Path, folder: str = FOLDER, ini: str = INI) -> Path:
    return write_zip(path, {
        f"{folder}/d3d11.dll": "dll", f"{folder}/SUWSF.asi": "asi", f"{folder}/SUWSF.ini": ini,
        f"{folder}/LICENSE": "license",
    })


class ArchiveRootTests(GameTest):
    def test_finds_the_one_folder_holding_every_file(self) -> None:
        wanted = ["a.dll", "b.ini"]
        self.assertEqual("", archive_root(["a.dll", "b.ini", "readme.txt"], wanted))
        self.assertEqual("Mod v2/", archive_root(["Mod v2/a.dll", "Mod v2/b.ini"], wanted))
        self.assertEqual("x/", archive_root(["x/a.dll", "x/b.ini", "x/old/a.dll", "x/old/b.ini"], wanted))
        self.assertIsNone(archive_root(["16x9/a.dll", "16x9/b.ini", "21x9/a.dll", "21x9/b.ini"], wanted))
        self.assertIsNone(archive_root(["x/a.dll", "y/b.ini"], wanted))
        self.assertIsNone(archive_root(["xa.dll", "xb.ini"], wanted))

    def test_downloads_folder_follows_the_desktop_setting(self) -> None:
        home = self.root
        self.assertEqual(home / "Downloads", downloads_folder(home, "darwin", {}))
        self.assertEqual(home / "Downloads", downloads_folder(home, "linux", {}))
        self.write({".config/user-dirs.dirs": '# comment\nXDG_DOWNLOAD_DIR="$HOME/Téléchargements"\n'}, home)
        self.assertEqual(home / "Téléchargements", downloads_folder(home, "linux", {}))
        self.write({"other/user-dirs.dirs": 'XDG_DOWNLOAD_DIR="/mnt/dl"\n'}, home)
        self.assertEqual(Path("/mnt/dl"), downloads_folder(home, "linux", {"XDG_CONFIG_HOME": str(home / "other")}))


class AuthorDownloadTests(GameTest):
    def setUp(self) -> None:
        super().setUp()
        self.ffx = catalog_mod(FFX)
        self.downloads = self.folder("Downloads")
        self.kept = self.root / "kept"

    def find(self):
        return find_downloads([self.ffx], scan_download_folders([self.kept, self.downloads]))[FFX]

    def test_without_a_download_the_game_is_listed_but_cannot_be_applied(self) -> None:
        report = match_supported_games(SteamScanReport(1, 1, [app_for(self.ffx, self.game)], []), (self.ffx,))
        add_author_downloads(report, [], [self.downloads])
        (game,) = report.supported_games
        self.assertEqual(("", self.ffx.source.page), (game.download_file, game.download_page))
        with self.assertRaisesRegex(ValueError, "author's page"):
            keep_download(report.mods[FFX], self.kept)
        gone = _release(self.downloads / "gone.zip")
        found = self.find()
        gone.unlink()
        with self.assertRaisesRegex(ValueError, "author's page"):
            keep_download(found, self.kept)

    def test_installs_from_the_folder_inside_the_zip_and_keeps_a_copy_for_remove(self) -> None:
        download = _release(self.downloads / "sub" / "anything.zip")
        found = self.find()
        self.assertEqual((str(download), f"{FOLDER}/"), (found.source.download_path, found.source.archive_root))
        kept = keep_download(found, self.kept)
        download.unlink()
        self.apply(kept, ini_resolution=(1280, 800))
        self.assert_files({"d3d11.dll": "dll", "SUWSF.asi": "asi", "LICENSE": "license",
                           "SUWSF.ini": INI.replace('"auto"', '"1280x800"')})
        again = self.find()
        self.assertEqual(str(stored_path(self.kept, self.ffx)), again.source.download_path)
        self.assertEqual([], self.remove(again, preserve_modified_ini=None, ini_resolution=(1280, 800)))
        self.assertEqual([], [path for path in self.game.rglob("*") if path.is_file()])

    def test_the_newest_fitting_zip_wins(self) -> None:
        old = _release(self.downloads / "old.zip")
        new = _release(self.downloads / "new.zip", folder="v2", ini=INI + "; v2\n")
        write_zip(self.downloads / "unrelated.zip", {"d3d11.dll": "other"})
        (self.downloads / "broken.zip").write_bytes(b"not a zip")
        times = {old: 1_000_000, new: 2_000_000}
        for path, stamp in times.items():
            os.utime(path, (stamp, stamp))
        found = self.find()
        self.assertEqual((str(new), "v2/"), (found.source.download_path, found.source.archive_root))

    def test_remove_compares_with_the_applied_zip_not_a_newer_download(self) -> None:
        applied = keep_download(find_downloads([self.ffx], scan_download_folders([_release(
            self.root / "v1" / "v1.zip").parent]))[FFX], self.kept)
        self.apply(applied, ini_resolution=(1280, 800))
        newer = _release(self.downloads / "v2.zip", folder="v2", ini=INI + "; v2\n")
        os.utime(newer, (4_000_000_000, 4_000_000_000))
        self.assertEqual(str(newer), self.find().source.download_path)
        for_remove = applied_download(self.find(), self.kept)
        self.assertEqual((str(stored_path(self.kept, self.ffx)), f"{FOLDER}/"),
                         (for_remove.source.download_path, for_remove.source.archive_root))
        self.assertEqual([], self.remove(for_remove, preserve_modified_ini=None, ini_resolution=(1280, 800)))
        self.assertEqual(self.ffx, applied_download(self.ffx, self.root / "nothing-saved"))

    def test_a_picked_zip_must_hold_the_files(self) -> None:
        wrong = write_zip(self.root / "elsewhere" / "wrong.zip", {"d3d11.dll": "dll"})
        with self.assertRaisesRegex(ValueError, "does not hold"):
            import_download(self.ffx, wrong, self.kept)
        colliding = write_zip(self.root / "elsewhere" / "colliding.zip", {"a.txt": "1", "A.TXT": "2"})
        with self.assertRaisesRegex(ValueError, "collide"):
            import_download(self.ffx, colliding, self.kept)
        (self.root / "elsewhere" / "broken.zip").write_bytes(b"not a zip")
        with self.assertRaisesRegex(ValueError, "not a readable zip"):
            import_download(self.ffx, self.root / "elsewhere" / "broken.zip", self.kept)
        self.assertFalse(self.kept.exists())
        picked = _release(self.root / "elsewhere" / "picked.zip")
        kept = import_download(self.ffx, picked, self.kept)
        self.assertEqual(str(stored_path(self.kept, self.ffx)), kept.source.download_path)
        self.assertEqual(picked.read_bytes(), stored_path(self.kept, self.ffx).read_bytes())


class AuthorDownloadCatalogTests(CatalogEditTest):
    def test_author_downloads_load_only_from_their_own_list(self) -> None:
        shipped = json.loads((Path(__file__).resolve().parents[2] / "catalog.json").read_text(encoding="utf-8"))
        entry = next(entry for entry in shipped["author_downloads"] if entry["id"] == FFX)
        self.document["author_downloads"] = [entry]
        self.assertIn(FFX, {mod.mod_id for mod in self.load()})
        self.assert_rejected(entry["source"], "page", [""], "source.page")
        self.assert_rejected(entry["source"], "path", ["external/Someone/Mod"], "nothing in the repository")
        self.assert_rejected(entry["source"], "sha256", ["0" * 64], "nothing in the repository")
        archive = self.document["mods"][0]
        self.document["author_downloads"] = [archive]
        with self.assertRaisesRegex(ValueError, "author_downloads"):
            self.load()
        self.document["author_downloads"] = []
        self.document["mods"].append(entry)
        with self.assertRaisesRegex(ValueError, "author_downloads"):
            self.load()
