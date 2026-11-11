import io
import os
import ssl
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import ANY, Mock, patch
from zipfile import ZIP_DEFLATED, ZipFile

from scanner.mod_catalog import IniResolutionPair, LauncherReplacement, ModBackup, ModDefinition, ModSource, ModSourceFile
from scanner.mod_installer import (
    create_ssl_context,
    apply_mod_files,
    fetch_mod_readme,
    existing_mod_logs,
    mod_readme_path,
    read_mod_ini,
    read_mod_log,
    remove_mod_files,
    update_existing_ini_resolution,
    write_mod_ini,
    launcher_setup_complete,
)


class ModInstallerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.game_path = Path(self.temporary_directory.name) / "game"
        self.game_path.mkdir()
        self.repository_url = "https://github.com/example/mods"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _mod(
        self,
        kind: str,
        files: tuple[str, ...],
        *,
        source_path: str | None = None,
        destination: str = "",
        source_files: tuple[ModSourceFile, ...] = (),
        ini_file: str | None = None,
    ) -> ModDefinition:
        return ModDefinition(
            mod_id="test-mod",
            name="Test mod",
            steam_app_id="123",
            install_dir="Test Game",
            executable="game.exe",
            source=ModSource(
                kind=kind,
                path=source_path,
                destination=destination,
                files=source_files,
                repository_url=self.repository_url,
                repository_ref="main",
            ),
            files=files,
            ini_file=ini_file,
            wine_dll_overrides=(),
            ini_resolution=(
                IniResolutionPair("Resolution", "Width", "Height", "pixels"),
                IniResolutionPair("TargetResolution", "TargetWidth", "TargetHeight", "pixels"),
                IniResolutionPair("Aspect", "AspectWidth", "AspectHeight", "ratio"),
            ) if ini_file == "fix.ini" else (),
        )

    @staticmethod
    def _archive(entries: dict[str, bytes]) -> bytes:
        content = io.BytesIO()
        with ZipFile(content, "w", ZIP_DEFLATED) as archive:
            for name, value in entries.items():
                archive.writestr(name, value)
        return content.getvalue()

    def test_mod_logs_are_catalog_scoped_and_bounded(self) -> None:
        mod = self._mod("archive", ("nested/Fix.ASI", "nested/Other.asi", "loader.dll"))
        nested = self.game_path / "nested"
        nested.mkdir()
        (nested / "Fix.log").write_bytes(b"prefix" + b"x" * (256 * 1024))
        (nested / "loader.log").write_text("unrelated", encoding="utf-8")
        self.assertEqual(["nested/Fix.log"], existing_mod_logs(mod.files, str(self.game_path)))
        content, truncated = read_mod_log(mod, str(self.game_path), "nested/Fix.log")
        self.assertTrue(truncated)
        self.assertEqual("x" * (256 * 1024), content)
        with self.assertRaisesRegex(ValueError, "not associated"):
            read_mod_log(mod, str(self.game_path), "nested/loader.log")
        with self.assertRaisesRegex(ValueError, "no longer available"):
            read_mod_log(mod, str(self.game_path), "nested/Other.log")
        (nested / "Fix.log").write_bytes(b"invalid \xff text")
        self.assertEqual(("invalid \ufffd text", False), read_mod_log(mod, str(self.game_path), "nested/Fix.log"))

    def test_mod_log_symlink_is_rejected(self) -> None:
        mod = self._mod("archive", ("fix.asi",))
        with patch.object(Path, "is_symlink", autospec=True, side_effect=lambda path: path.name == "fix.log"):
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                read_mod_log(mod, str(self.game_path), "fix.log")
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                existing_mod_logs(mod.files, str(self.game_path))

    def test_fetches_readme_from_mod_source_root(self) -> None:
        mod = self._mod(
            "directory",
            ("Game/fix.dll",),
            source_path="Ninja Gaiden Sigma 2/nested/mod",
        )
        response = io.BytesIO(b"# Mod\n\nRead me")

        with patch(
            "scanner.mod_installer.urlopen",
            return_value=response,
        ) as open_url:
            readme = fetch_mod_readme(mod)

        self.assertEqual("Ninja Gaiden Sigma 2/README.md", mod_readme_path(mod))
        self.assertEqual("# Mod\n\nRead me", readme)
        open_url.assert_called_once_with(
            "https://raw.githubusercontent.com/example/mods/main/"
            "Ninja%20Gaiden%20Sigma%202/README.md",
            timeout=30,
            context=ANY,
        )

    def test_archive_apply_replaces_catalogued_files_and_preserves_others(self) -> None:
        mod = self._mod(
            "archive",
            ("Game/fix.dll", "Game/fix.ini"),
            source_path="fixes/test.zip",
            destination="Game",
        )
        self._archive_bytes = self._archive(
            {"fix.dll": b"new dll", "fix.ini": b"new ini"}
        )
        (self.game_path / "Game").mkdir()
        (self.game_path / "Game" / "fix.dll").write_bytes(b"old dll")
        (self.game_path / "Game" / "user.txt").write_text("keep", encoding="utf-8")

        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(self._archive_bytes),
        ) as open_url:
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)

        self.assertEqual(b"new dll", (self.game_path / "Game" / "fix.dll").read_bytes())
        self.assertEqual(b"new ini", (self.game_path / "Game" / "fix.ini").read_bytes())
        self.assertEqual(
            "keep",
            (self.game_path / "Game" / "user.txt").read_text(encoding="utf-8"),
        )
        open_url.assert_called_once_with(
            "https://raw.githubusercontent.com/example/mods/main/fixes/test.zip",
            timeout=30,
            context=ANY,
        )
        self.assertIsInstance(open_url.call_args.kwargs["context"], ssl.SSLContext)
        self.assertEqual(
            ssl.CERT_REQUIRED,
            open_url.call_args.kwargs["context"].verify_mode,
        )
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-*")))

    def test_apply_adjusts_pixel_resolution_and_aspect_ratio_only(self) -> None:
        mod = self._mod(
            "archive",
            ("fix.ini",),
            source_path="fixes/test.zip",
            ini_file="fix.ini",
        )
        original_ini = (
            b"[Resolution]\r\n"
            b"Width = 1280\r\n"
            b"Height = 800 ; keep comment\r\n"
            b"\r\n[TargetResolution]\n"
            b"TargetWidth=1280\n"
            b"TargetHeight=800\n"
            b"\n[Ratio]\n"
            b"TargetWidth=16.0\n"
            b"TargetHeight=10.0\n"
            b"\n[SecondaryTarget]\n"
            b"TargetWidth=3840\n"
            b"TargetHeight=2160\n"
            b"\n[Unset]\n"
            b"Width=0\n"
            b"Height=0\n"
            b"\n[Replacement]\n"
            b"ReplaceWidth=1280\n"
            b"ReplaceHeight=720\n"
            b"\n[Aspect]\n"
            b"AspectWidth=16\n"
            b"AspectHeight=10\n"
        )
        archive_bytes = self._archive({"fix.ini": original_ini})
        expected_ini = original_ini.replace(
            b"Width = 1280\r\n",
            b"Width = 1920\r\n",
            1,
        ).replace(
            b"Height = 800 ; keep comment\r\n",
            b"Height = 1080 ; keep comment\r\n",
            1,
        ).replace(
            b"TargetWidth=1280\nTargetHeight=800",
            b"TargetWidth=1920\nTargetHeight=1080",
            1,
        ).replace(
            b"AspectWidth=16\nAspectHeight=10",
            b"AspectWidth=16\nAspectHeight=9",
            1,
        )

        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(archive_bytes),
        ):
            apply_mod_files(
                mod,
                str(self.game_path),
                replace_existing_files=True,
                ini_resolution=(1920, 1080),
            )
            self.assertEqual(expected_ini, (self.game_path / "fix.ini").read_bytes())
            self.assertEqual(
                [],
                remove_mod_files(
                    mod,
                    str(self.game_path),
                    ini_resolution=(1920, 1080),
                ),
            )

        self.assertFalse((self.game_path / "fix.ini").exists())

    def test_catalogued_ori_ratio_preserves_keys_and_other_bytes(self) -> None:
        from scanner.mod_catalog import load_mod_catalog

        mod = next(m for m in load_mod_catalog() if m.mod_id == "ori-and-the-will-of-the-wisps")
        original = (
            b"\xef\xbb\xbf[AspectRatio]\r\nTargetWidth = 16.0 ; width\r\n"
            b"TargetHeight = 10.0\r\n[FOV]\r\nFOVScale = 1.0\r\n"
        )
        path = self.game_path / mod.ini_file
        path.write_bytes(original)
        changed, preserved = update_existing_ini_resolution(mod, str(self.game_path), 1920, 1080)
        self.assertTrue(changed)
        self.assertTrue(preserved)
        self.assertEqual(original.replace(b"TargetHeight = 10.0", b"TargetHeight = 9.0"), path.read_bytes())
        changed, _ = update_existing_ini_resolution(mod, str(self.game_path), 1280, 800)
        self.assertTrue(changed)
        self.assertEqual(
            original.replace(b"TargetWidth = 16.0", b"TargetWidth = 8.0")
            .replace(b"TargetHeight = 10.0", b"TargetHeight = 5.0"),
            path.read_bytes(),
        )

    def test_zero_custom_windowed_resolution_remains_default(self) -> None:
        from scanner.mod_catalog import load_mod_catalog
        from scanner.mod_installer import _rewrite_ini_resolution

        mod = next(m for m in load_mod_catalog() if m.mod_id == "ffxvifix")
        content = (
            b"[Fix Resolution]\r\n"
            b"WindowedResX = 0\r\n"
            b"WindowedResY = 0\r\n"
        )

        self.assertEqual(
            content,
            _rewrite_ini_resolution(content, 1920, 1080, mod.ini_resolution),
        )

    def test_undeclared_target_and_duplicate_keys_are_never_changed(self) -> None:
        from scanner.mod_installer import _rewrite_ini_resolution
        content = (
            b"[Custom Resolution]\nWidth=1280\nWidth=invalid\nHeight=800\n"
            b"TargetWidth=3840\nTargetHeight=2160\n"
        )
        pair = IniResolutionPair("Custom Resolution", "Width", "Height", "pixels")
        self.assertEqual(content, _rewrite_ini_resolution(content, 1920, 1080, (pair,)))

    def test_bulk_update_does_not_overwrite_concurrent_ini_edit(self) -> None:
        mod = self._mod("archive", ("fix.ini",), ini_file="fix.ini")
        path = self.game_path / "fix.ini"
        path.write_bytes(b"[Resolution]\nWidth=1280\nHeight=800\n")
        edited = b"[Resolution]\nWidth=1024\nHeight=768\n"

        def concurrent_edit(*args):
            path.write_bytes(edited)

        with patch("scanner.mod_installer.os.chmod", side_effect=concurrent_edit):
            with self.assertRaisesRegex(ValueError, "changed during"):
                update_existing_ini_resolution(mod, str(self.game_path), 1920, 1080)
        self.assertEqual(edited, path.read_bytes())
        self.assertFalse(list(self.game_path.glob(".anyaspectratio-ini-*")))

    def test_apply_keeps_original_ini_when_automatic_adjustment_fails(self) -> None:
        mod = self._mod(
            "archive",
            ("fix.ini",),
            source_path="fixes/test.zip",
            ini_file="fix.ini",
        )
        original_ini = b"[Resolution]\nWidth=1280\nHeight=800\n"
        archive_bytes = self._archive({"fix.ini": original_ini})
        warnings: list[str] = []

        with (
            patch(
                "scanner.mod_installer.urlopen",
                side_effect=lambda url, **kwargs: io.BytesIO(archive_bytes),
            ),
            patch(
                "scanner.mod_installer._rewrite_ini_resolution",
                side_effect=ValueError("unsupported format"),
            ),
        ):
            apply_mod_files(
                mod,
                str(self.game_path),
                replace_existing_files=True,
                ini_resolution=(1920, 1080),
                ini_adjustment_warnings=warnings,
            )

        self.assertEqual(original_ini, (self.game_path / "fix.ini").read_bytes())
        self.assertEqual(
            ["Could not adjust fix.ini; the original INI was kept."],
            warnings,
        )
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-ini-edit-*")))

    def test_apply_keeps_original_ini_when_staged_replacement_fails(self) -> None:
        mod = self._mod(
            "archive",
            ("fix.ini",),
            source_path="fixes/test.zip",
            ini_file="fix.ini",
        )
        original_ini = b"[Resolution]\nWidth=1280\nHeight=800\n"
        archive_bytes = self._archive({"fix.ini": original_ini})
        warnings: list[str] = []

        with (
            patch(
                "scanner.mod_installer.urlopen",
                side_effect=lambda url, **kwargs: io.BytesIO(archive_bytes),
            ),
            patch(
                "scanner.mod_installer._replace_staged_file",
                side_effect=PermissionError("read-only staging filesystem"),
            ),
        ):
            apply_mod_files(
                mod,
                str(self.game_path),
                replace_existing_files=True,
                ini_resolution=(1920, 1080),
                ini_adjustment_warnings=warnings,
            )

        self.assertEqual(original_ini, (self.game_path / "fix.ini").read_bytes())
        self.assertEqual(
            ["Could not adjust fix.ini; the original INI was kept."],
            warnings,
        )
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-ini-edit-*")))

    def test_file_mod_renames_original_to_backup_and_restores_on_remove(self) -> None:
        mod = self._mod(
            "files",
            ("data.win",),
            source_files=(ModSourceFile("source/data.win", "data.win"),),
        )
        mod = replace(mod, backup_file="data.win.bak")
        target = self.game_path / "data.win"
        backup = self.game_path / "data.win.bak"
        target.write_bytes(b"original")

        with patch(
            "scanner.mod_installer.open_repository_file",
            return_value=io.BytesIO(b"ultrawide"),
        ):
            self.assertEqual(
                [],
                apply_mod_files(
                    mod,
                    str(self.game_path),
                    replace_existing_files=True,
                ),
            )

        self.assertEqual(b"ultrawide", target.read_bytes())
        self.assertEqual(b"original", backup.read_bytes())
        self.assertEqual([], remove_mod_files(mod, str(self.game_path)))
        self.assertEqual(b"original", target.read_bytes())
        self.assertFalse(backup.exists())

    def _backup_mod(self) -> ModDefinition:
        return replace(
            self._mod(
                "archive",
                ("helper.dll", "Managed/Game.dll", "other.dll", "fix.ini"),
                source_path="fixes/test.zip",
                ini_file="fix.ini",
            ),
            backups=(
                ModBackup("Managed/Game.dll", "Managed/Game.dll.bak"),
                ModBackup("other.dll", "other.dll.bak"),
            ),
        )

    def _backup_archive(self) -> bytes:
        return self._archive({
            "helper.dll": b"helper",
            "Managed/Game.dll": b"patched game",
            "other.dll": b"patched other",
            "fix.ini": b"[Resolution]\nWidth=1280\nHeight=800\n",
        })

    def _backup_originals(self) -> None:
        (self.game_path / "Managed").mkdir()
        (self.game_path / "Managed" / "Game.dll").write_bytes(b"original game")
        (self.game_path / "other.dll").write_bytes(b"original other")

    def test_multiple_backups_apply_reapply_and_restore_only_targets(self) -> None:
        mod = self._backup_mod()
        self._backup_originals()
        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda *args, **kwargs: io.BytesIO(self._backup_archive()),
        ):
            for _ in range(2):
                self.assertEqual([], apply_mod_files(
                    mod, str(self.game_path), replace_existing_files=True,
                ))
                self.assertEqual(b"original game", (self.game_path / "Managed/Game.dll.bak").read_bytes())
                self.assertEqual(b"original other", (self.game_path / "other.dll.bak").read_bytes())
                self.assertEqual(b"patched game", (self.game_path / "Managed/Game.dll").read_bytes())
                self.assertEqual(b"helper", (self.game_path / "helper.dll").read_bytes())
            (self.game_path / "fix.ini").write_bytes(b"custom ini")
            self.assertEqual(["fix.ini"], remove_mod_files(mod, str(self.game_path)))
            self.assertTrue((self.game_path / "Managed/Game.dll.bak").exists())
            self.assertEqual([], remove_mod_files(
                mod, str(self.game_path), preserve_modified_ini=True,
            ))
        self.assertEqual(b"original game", (self.game_path / "Managed/Game.dll").read_bytes())
        self.assertEqual(b"original other", (self.game_path / "other.dll").read_bytes())
        self.assertEqual(b"custom ini", (self.game_path / "fix.ini").read_bytes())
        self.assertFalse((self.game_path / "helper.dll").exists())
        self.assertFalse(list(self.game_path.rglob("*.bak")))
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-*")))

    def test_backup_restore_is_not_limited_by_mod_download_file_size(self) -> None:
        mod = self._backup_mod()
        self._backup_originals()
        original = b"original game larger than the normal mod file limit"
        (self.game_path / "Managed/Game.dll").write_bytes(original)
        with patch(
            "scanner.mod_installer.urlopen",
            return_value=io.BytesIO(self._backup_archive()),
        ):
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)

        with patch("scanner.mod_installer.MAX_MOD_FILE_SIZE_BYTES", 4):
            self.assertEqual(
                [],
                remove_mod_files(mod, str(self.game_path), preserve_modified_ini=False),
            )

        self.assertEqual(original, (self.game_path / "Managed/Game.dll").read_bytes())

    def test_backup_conflict_prompt_and_decline_leave_originals_untouched(self) -> None:
        mod = self._backup_mod()
        self._backup_originals()
        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda *args, **kwargs: io.BytesIO(self._backup_archive()),
        ):
            for choice in (None, False):
                self.assertEqual(["Managed/Game.dll", "other.dll"], apply_mod_files(
                    mod, str(self.game_path), replace_existing_files=choice,
                ))
                self.assertEqual(b"original game", (self.game_path / "Managed/Game.dll").read_bytes())
                self.assertEqual(b"original other", (self.game_path / "other.dll").read_bytes())
                self.assertFalse(list(self.game_path.rglob("*.bak")))
        self.assertTrue((self.game_path / "helper.dll").exists())

    def test_missing_original_or_backup_is_rejected_without_removing_added_files(self) -> None:
        mod = self._backup_mod()
        with self.assertRaisesRegex(ValueError, "missing"):
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        self._backup_originals()
        (self.game_path / "helper.dll").write_bytes(b"helper")
        with self.assertRaisesRegex(ValueError, "Properties > Installed Files"):
            remove_mod_files(mod, str(self.game_path), preserve_modified_ini=False)
        self.assertEqual(b"helper", (self.game_path / "helper.dll").read_bytes())
        (self.game_path / "Managed/Game.dll").write_bytes(b"patched game")
        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda *args, **kwargs: io.BytesIO(self._backup_archive()),
        ):
            self.assertEqual(
                [],
                apply_mod_files(mod, str(self.game_path), replace_existing_files=True),
            )
        self.assertEqual(b"patched game", (self.game_path / "Managed/Game.dll").read_bytes())
        self.assertFalse((self.game_path / "Managed/Game.dll.bak").exists())
        self.assertEqual(
            b"original other",
            (self.game_path / "other.dll.bak").read_bytes(),
        )

    def test_backup_replacement_failure_restores_renamed_original(self) -> None:
        mod = self._backup_mod()
        self._backup_originals()
        real_replace = os.replace

        def fail_game(source: Path, target: Path) -> None:
            if target == self.game_path / "Managed/Game.dll":
                raise PermissionError("replacement blocked")
            real_replace(source, target)

        with (
            patch("scanner.mod_installer.urlopen", return_value=io.BytesIO(self._backup_archive())),
            patch("scanner.mod_installer.os.replace", side_effect=fail_game),
        ):
            with self.assertRaisesRegex(PermissionError, "replacement blocked"):
                apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        self.assertEqual(b"original game", (self.game_path / "Managed/Game.dll").read_bytes())
        self.assertFalse(list(self.game_path.rglob("*.bak")))
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-*")))

    def test_backup_restore_failure_keeps_helper_and_backup(self) -> None:
        mod = self._backup_mod()
        self._backup_originals()
        with patch("scanner.mod_installer.urlopen", return_value=io.BytesIO(self._backup_archive())):
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        with patch("scanner.mod_installer.os.replace", side_effect=PermissionError("restore blocked")):
            with self.assertRaisesRegex(PermissionError, "restore blocked"):
                remove_mod_files(mod, str(self.game_path), preserve_modified_ini=False)
        self.assertEqual(b"helper", (self.game_path / "helper.dll").read_bytes())
        self.assertEqual(b"original game", (self.game_path / "Managed/Game.dll.bak").read_bytes())
        self.assertEqual(b"patched game", (self.game_path / "Managed/Game.dll").read_bytes())

    def test_backup_symlink_and_directory_rejected_before_changes(self) -> None:
        mod = self._backup_mod()
        self._backup_originals()
        with patch.object(
            Path, "is_symlink", autospec=True,
            side_effect=lambda path: path.name == "Game.dll.bak",
        ):
            for operation in (apply_mod_files, remove_mod_files):
                with self.assertRaisesRegex(ValueError, "symbolic link"):
                    operation(mod, str(self.game_path))
        (self.game_path / "Managed/Game.dll.bak").mkdir()
        for operation in (apply_mod_files, remove_mod_files):
            with self.assertRaisesRegex(ValueError, "not a regular file"):
                operation(mod, str(self.game_path))

    def test_partial_backup_restore_failure_keeps_all_backups_for_retry(self) -> None:
        mod = self._backup_mod()
        self._backup_originals()
        with patch("scanner.mod_installer.urlopen", return_value=io.BytesIO(self._backup_archive())):
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        real_replace = os.replace

        def fail_second(source: Path, target: Path) -> None:
            if target == self.game_path / "other.dll":
                raise PermissionError("second restore blocked")
            real_replace(source, target)

        with patch("scanner.mod_installer.os.replace", side_effect=fail_second):
            with self.assertRaisesRegex(PermissionError, "second restore blocked"):
                remove_mod_files(mod, str(self.game_path), preserve_modified_ini=False)
        self.assertEqual(b"original game", (self.game_path / "Managed/Game.dll").read_bytes())
        self.assertEqual(b"patched other", (self.game_path / "other.dll").read_bytes())
        self.assertEqual(b"original game", (self.game_path / "Managed/Game.dll.bak").read_bytes())
        self.assertEqual(b"original other", (self.game_path / "other.dll.bak").read_bytes())
        self.assertTrue((self.game_path / "helper.dll").exists())
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-*")))
        self.assertEqual([], remove_mod_files(mod, str(self.game_path), preserve_modified_ini=False))
        self.assertEqual(b"original other", (self.game_path / "other.dll").read_bytes())
        self.assertFalse(list(self.game_path.rglob("*.bak")))
        self.assertFalse((self.game_path / "helper.dll").exists())

    def test_missing_archive_member_does_not_replace_existing_files(self) -> None:
        mod = self._mod(
            "archive",
            ("fix.dll", "fix.ini"),
            source_path="fixes/test.zip",
        )
        (self.game_path / "fix.dll").write_bytes(b"keep old")
        archive_bytes = self._archive({"fix.dll": b"new"})

        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(archive_bytes),
        ):
            with self.assertRaisesRegex(ValueError, "missing fix.ini"):
                apply_mod_files(mod, str(self.game_path))

        self.assertEqual(b"keep old", (self.game_path / "fix.dll").read_bytes())
        self.assertFalse((self.game_path / "fix.ini").exists())

    def _launcher_mod(self) -> ModDefinition:
        return replace(
            self._mod(
                "archive",
                ("Game/offline.exe", "Game/mods/fix.dll"),
                source_path="fixes/test.zip",
                destination="Game",
            ),
            executable="Game/game.exe",
            offline_only=True,
            launcher_replacement=LauncherReplacement(
                "Game/offline.exe", "Game/protected.exe", "Game/protected-old.exe"
            ),
        )

    def _launcher_game(self) -> tuple[Path, Path]:
        folder = self.game_path / "Game"
        folder.mkdir()
        (folder / "game.exe").write_bytes(b"game executable")
        (folder / "protected.exe").write_bytes(b"original protected launcher")
        return folder / "protected.exe", folder / "protected-old.exe"

    def _launcher_archive(self):
        archive = self._archive({"offline.exe": b"offline", "mods/fix.dll": b"fix"})
        return patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(archive),
        )

    def test_launcher_confirm_apply_reapply_remove_and_scan_status(self) -> None:
        from scanner import InstalledApp, SteamScanReport, match_supported_games

        mod = self._launcher_mod()
        target, backup = self._launcher_game()
        unrelated = self.game_path / "Game" / "mods" / "unrelated.dll"
        unrelated.parent.mkdir()
        unrelated.write_bytes(b"keep")
        app = InstalledApp("123", "Test", "Test Game", "", str(self.game_path))
        scan = lambda: match_supported_games(SteamScanReport(1, 1, [app], []), (mod,)).supported_games[0]
        self.assertEqual("not_detected", scan().file_status)
        self.assertTrue(scan().to_dict()["offline_only"])
        with self._launcher_archive():
            self.assertEqual(["Game/protected.exe"], apply_mod_files(mod, str(self.game_path)))
            self.assertFalse(backup.exists())
            self.assertFalse((self.game_path / "Game" / "offline.exe").exists())
            for _ in range(2):
                self.assertEqual(
                    [], apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
                )
                self.assertEqual(b"offline", target.read_bytes())
                self.assertEqual(b"original protected launcher", backup.read_bytes())
                self.assertTrue(launcher_setup_complete(mod, self.game_path))
                self.assertEqual("complete", scan().file_status)
        target.write_bytes(b"original protected launcher")
        self.assertEqual("partial", scan().file_status)
        self.assertIn("Game/protected.exe", scan().files_missing)
        self.assertEqual([], remove_mod_files(mod, str(self.game_path)))
        self.assertEqual(b"original protected launcher", target.read_bytes())
        self.assertFalse(backup.exists())
        self.assertEqual(b"game executable", (self.game_path / "Game" / "game.exe").read_bytes())
        self.assertEqual(b"keep", unrelated.read_bytes())
        self.assertEqual("not_detected", scan().file_status)
        self.assertEqual([], remove_mod_files(mod, str(self.game_path)))

    def test_launcher_declining_replacement_leaves_original_and_backup_untouched(self) -> None:
        target, backup = self._launcher_game()
        with self._launcher_archive():
            self.assertEqual(
                ["Game/protected.exe"],
                apply_mod_files(self._launcher_mod(), str(self.game_path), replace_existing_files=False),
            )
        self.assertEqual(b"original protected launcher", target.read_bytes())
        self.assertFalse(backup.exists())

    def test_launcher_copy_failure_restores_original_and_partial_files_can_be_removed(self) -> None:
        mod = self._launcher_mod()
        target, backup = self._launcher_game()
        from scanner.mod_installer import os
        original_replace = os.replace

        def replace_except_launcher(source, destination):
            if destination == target:
                raise PermissionError("launcher locked")
            return original_replace(source, destination)

        with self._launcher_archive(), patch(
            "scanner.mod_installer.os.replace", side_effect=replace_except_launcher
        ):
            with self.assertRaisesRegex(PermissionError, "launcher locked"):
                apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        self.assertEqual(b"original protected launcher", target.read_bytes())
        self.assertFalse(backup.exists())
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-*")))
        self.assertEqual([], remove_mod_files(mod, str(self.game_path)))
        self.assertEqual(b"original protected launcher", target.read_bytes())

    def test_launcher_remove_restoration_failure_keeps_mod_dependencies(self) -> None:
        mod = self._launcher_mod()
        target, backup = self._launcher_game()
        with self._launcher_archive():
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        with patch("scanner.mod_installer.os.replace", side_effect=PermissionError("locked")):
            with self.assertRaisesRegex(PermissionError, "locked"):
                remove_mod_files(mod, str(self.game_path))
        self.assertTrue(backup.is_file())
        self.assertEqual(b"offline", target.read_bytes())
        self.assertTrue((self.game_path / "Game" / "offline.exe").is_file())

    def test_launcher_mod_deletion_failure_does_not_restore_online_launcher(self) -> None:
        mod = self._launcher_mod()
        target, backup = self._launcher_game()
        with self._launcher_archive():
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        original_unlink = Path.unlink

        def unlink_except_mod(path, *args, **kwargs):
            if path.name == "fix.dll":
                raise PermissionError("mod locked")
            return original_unlink(path, *args, **kwargs)

        with patch.object(Path, "unlink", autospec=True, side_effect=unlink_except_mod):
            with self.assertRaisesRegex(PermissionError, "mod locked"):
                remove_mod_files(mod, str(self.game_path))
        self.assertEqual(b"offline", target.read_bytes())
        self.assertTrue(backup.is_file())
        self.assertTrue((self.game_path / "Game" / "offline.exe").is_file())

    def test_launcher_can_copy_game_executable_without_removing_it(self) -> None:
        mod = self._launcher_mod()
        mod = replace(mod, launcher_replacement=replace(
            mod.launcher_replacement, source="Game/game.exe"
        ))
        target, backup = self._launcher_game()
        executable = self.game_path / "Game" / "game.exe"
        original_game = executable.read_bytes()
        original_launcher = target.read_bytes()
        with self._launcher_archive():
            apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        self.assertEqual(original_game, target.read_bytes())
        self.assertTrue(launcher_setup_complete(mod, self.game_path))
        remove_mod_files(mod, str(self.game_path))
        self.assertEqual(original_game, executable.read_bytes())
        self.assertEqual(original_launcher, target.read_bytes())
        self.assertFalse(backup.exists())

    def test_elden_ring_real_archive_apply_and_remove_in_temporary_game(self) -> None:
        from scanner.mod_catalog import load_mod_catalog
        from scanner.mod_installer import set_offline_mode, set_pack_dir
        mod = next(mod for mod in load_mod_catalog() if mod.mod_id == "elden-ring-ultrawide-fix")
        folder = self.game_path / "Game"
        folder.mkdir()
        (folder / "eldenring.exe").write_bytes(b"original game")
        target = folder / "start_protected_game.exe"
        target.write_bytes(b"original launcher")
        set_pack_dir(Path(__file__).resolve().parents[2])
        set_offline_mode(True)
        try:
            self.assertEqual(
                [], apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
            )
            self.assertTrue(launcher_setup_complete(mod, self.game_path))
            self.assertEqual(
                (folder / "start_game_in_offline_mode.exe").read_bytes(),
                target.read_bytes(),
            )
            for relative in mod.files:
                self.assertTrue((self.game_path / relative).is_file(), relative)
            self.assertEqual(
                [], remove_mod_files(mod, str(self.game_path), preserve_modified_ini=False)
            )
            self.assertEqual(b"original launcher", target.read_bytes())
            self.assertEqual(b"original game", (folder / "eldenring.exe").read_bytes())
            self.assertFalse((folder / "start_protected_game-old.exe").exists())
            for relative in mod.files:
                self.assertFalse((self.game_path / relative).exists(), relative)
        finally:
            set_pack_dir(None)
            set_offline_mode(False)

    def test_launcher_missing_backup_never_saves_offline_copy_as_original(self) -> None:
        mod = self._launcher_mod()
        target, backup = self._launcher_game()
        target.write_bytes(b"offline")
        with self._launcher_archive():
            with self.assertRaisesRegex(ValueError, "backup is missing"):
                apply_mod_files(mod, str(self.game_path), replace_existing_files=True)
        self.assertFalse(backup.exists())
        self.assertEqual(b"offline", target.read_bytes())

    def test_launcher_symlink_backup_rejected_before_mod_changes(self) -> None:
        mod = self._launcher_mod()
        self._launcher_game()
        with patch.object(
            Path, "is_symlink", autospec=True,
            side_effect=lambda path: path.name == "protected-old.exe",
        ):
            for operation in (apply_mod_files, remove_mod_files):
                with self.assertRaisesRegex(ValueError, "symbolic link"):
                    operation(mod, str(self.game_path))

    def test_archive_reapply_preserves_original_backup_and_remove_restores_it(self) -> None:
        mod = replace(
            self._mod("archive", ("data.win",), source_path="fixes/undertale.zip"),
            backup_file="data.win.bak",
        )
        target = self.game_path / "data.win"
        backup = self.game_path / "data.win.bak"
        target.write_bytes(b"original")
        archive_bytes = self._archive({"data.win": b"ultrawide"})

        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(archive_bytes),
        ):
            for _ in range(2):
                self.assertEqual(
                    [],
                    apply_mod_files(mod, str(self.game_path), replace_existing_files=True),
                )
                self.assertEqual(b"ultrawide", target.read_bytes())
                self.assertEqual(b"original", backup.read_bytes())

        self.assertEqual([], remove_mod_files(mod, str(self.game_path)))
        self.assertEqual(b"original", target.read_bytes())
        self.assertFalse(backup.exists())

    def test_rejects_oversized_archive_download_before_installing(self) -> None:
        mod = self._mod(
            "archive",
            ("fix.dll",),
            source_path="fixes/test.zip",
        )
        archive_bytes = self._archive({"fix.dll": b"payload"})

        with (
            patch("scanner.mod_installer.MAX_MOD_DOWNLOAD_SIZE_BYTES", 4),
            patch(
                "scanner.mod_installer.urlopen",
                return_value=io.BytesIO(archive_bytes),
            ),
        ):
            with self.assertRaisesRegex(ValueError, "archive download exceeds"):
                apply_mod_files(mod, str(self.game_path))

        self.assertFalse((self.game_path / "fix.dll").exists())

    def test_rejects_oversized_archive_member_before_installing(self) -> None:
        mod = self._mod(
            "archive",
            ("fix.dll",),
            source_path="fixes/test.zip",
        )
        archive_bytes = self._archive({"fix.dll": b"payload"})

        with (
            patch("scanner.mod_installer.MAX_MOD_FILE_SIZE_BYTES", 4),
            patch(
                "scanner.mod_installer.urlopen",
                return_value=io.BytesIO(archive_bytes),
            ),
        ):
            with self.assertRaisesRegex(ValueError, "Mod file exceeds"):
                apply_mod_files(mod, str(self.game_path))

        self.assertFalse((self.game_path / "fix.dll").exists())

    def test_rejects_oversized_individual_file_download(self) -> None:
        mod = self._mod(
            "files",
            ("fix.dll",),
            source_files=(ModSourceFile("source/fix.dll", "fix.dll"),),
        )

        with (
            patch("scanner.mod_installer.MAX_MOD_FILE_SIZE_BYTES", 4),
            patch(
                "scanner.mod_installer.urlopen",
                return_value=io.BytesIO(b"payload"),
            ),
        ):
            with self.assertRaisesRegex(ValueError, "Mod file exceeds"):
                apply_mod_files(mod, str(self.game_path))

        self.assertFalse((self.game_path / "fix.dll").exists())
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-*")))

    def test_rejects_total_file_download_limit_without_installing_partial_set(self) -> None:
        mod = self._mod(
            "files",
            ("first.dll", "second.dll"),
            source_files=(
                ModSourceFile("source/first.dll", "first.dll"),
                ModSourceFile("source/second.dll", "second.dll"),
            ),
        )
        responses = {
            "https://raw.githubusercontent.com/example/mods/main/source/first.dll": b"12",
            "https://raw.githubusercontent.com/example/mods/main/source/second.dll": b"34",
        }

        with (
            patch("scanner.mod_installer.MAX_MOD_FILE_SIZE_BYTES", 4),
            patch("scanner.mod_installer.MAX_MOD_TOTAL_SIZE_BYTES", 3),
            patch(
                "scanner.mod_installer.urlopen",
                side_effect=lambda url, **kwargs: io.BytesIO(responses[url]),
            ),
        ):
            with self.assertRaisesRegex(ValueError, "total limit"):
                apply_mod_files(mod, str(self.game_path))

        self.assertFalse((self.game_path / "first.dll").exists())
        self.assertFalse((self.game_path / "second.dll").exists())
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-*")))

    def test_modified_ini_requires_confirmation_before_any_files_are_applied(self) -> None:
        mod = self._mod(
            "files",
            ("fix.dll", "fix.ini"),
            source_files=(
                ModSourceFile("source/fix.dll", "fix.dll"),
                ModSourceFile("source/fix.ini", "fix.ini"),
            ),
            ini_file="fix.ini",
        )
        (self.game_path / "fix.ini").write_bytes(b"user configuration")
        responses = {
            "https://raw.githubusercontent.com/example/mods/main/source/fix.dll": (
                b"mod binary"
            ),
            "https://raw.githubusercontent.com/example/mods/main/source/fix.ini": (
                b"default configuration"
            ),
        }

        def open_source(url: str, **kwargs) -> io.BytesIO:
            return io.BytesIO(responses[url])

        with patch("scanner.mod_installer.urlopen", side_effect=open_source):
            conflicting_files = apply_mod_files(mod, str(self.game_path))

            self.assertEqual(["fix.ini"], conflicting_files)
        self.assertFalse((self.game_path / "fix.dll").exists())
        self.assertEqual(
            b"user configuration",
            (self.game_path / "fix.ini").read_bytes(),
        )

        with patch("scanner.mod_installer.urlopen", side_effect=open_source):
            conflicting_files = apply_mod_files(
                mod,
                str(self.game_path),
                replace_existing_files=False,
            )

        self.assertEqual(["fix.ini"], conflicting_files)
        self.assertEqual(b"mod binary", (self.game_path / "fix.dll").read_bytes())
        self.assertEqual(
            b"user configuration",
            (self.game_path / "fix.ini").read_bytes(),
        )

    def test_confirmed_ini_replacement_installs_catalog_version(self) -> None:
        mod = self._mod(
            "files",
            ("fix.dll", "fix.ini"),
            source_files=(
                ModSourceFile("source/fix.dll", "fix.dll"),
                ModSourceFile("source/fix.ini", "fix.ini"),
            ),
            ini_file="fix.ini",
        )
        (self.game_path / "fix.ini").write_bytes(b"user configuration")
        responses = {
            "https://raw.githubusercontent.com/example/mods/main/source/fix.dll": (
                b"mod binary"
            ),
            "https://raw.githubusercontent.com/example/mods/main/source/fix.ini": (
                b"default configuration"
            ),
        }

        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(responses[url]),
        ):
            conflicting_files = apply_mod_files(
                mod,
                str(self.game_path),
                replace_existing_files=True,
            )

        self.assertEqual([], conflicting_files)
        self.assertEqual(
            b"default configuration",
            (self.game_path / "fix.ini").read_bytes(),
        )
        self.assertEqual(b"mod binary", (self.game_path / "fix.dll").read_bytes())

    def test_existing_mod_binary_conflict_prompts_and_can_be_preserved(self) -> None:
        mod = self._mod(
            "files",
            ("fix.dll", "fix.ini"),
            source_files=(
                ModSourceFile("source/fix.dll", "fix.dll"),
                ModSourceFile("source/fix.ini", "fix.ini"),
            ),
        )
        (self.game_path / "fix.dll").write_bytes(b"another mod's binary")
        responses = {
            "https://raw.githubusercontent.com/example/mods/main/source/fix.dll": (
                b"catalog binary"
            ),
            "https://raw.githubusercontent.com/example/mods/main/source/fix.ini": (
                b"catalog configuration"
            ),
        }

        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(responses[url]),
        ):
            conflicts = apply_mod_files(mod, str(self.game_path))

        self.assertEqual(["fix.dll"], conflicts)
        self.assertFalse((self.game_path / "fix.ini").exists())

        with patch(
            "scanner.mod_installer.urlopen",
            side_effect=lambda url, **kwargs: io.BytesIO(responses[url]),
        ):
            conflicts = apply_mod_files(
                mod,
                str(self.game_path),
                replace_existing_files=False,
            )

        self.assertEqual(["fix.dll"], conflicts)
        self.assertEqual(
            b"another mod's binary",
            (self.game_path / "fix.dll").read_bytes(),
        )
        self.assertEqual(
            b"catalog configuration",
            (self.game_path / "fix.ini").read_bytes(),
        )

    def test_identical_existing_mod_file_does_not_require_confirmation(self) -> None:
        mod = self._mod(
            "files",
            ("fix.dll",),
            source_files=(ModSourceFile("source/fix.dll", "fix.dll"),),
        )
        (self.game_path / "fix.dll").write_bytes(b"catalog binary")

        with patch(
            "scanner.mod_installer.urlopen",
            return_value=io.BytesIO(b"catalog binary"),
        ):
            conflicts = apply_mod_files(mod, str(self.game_path))

        self.assertEqual([], conflicts)
        self.assertEqual(b"catalog binary", (self.game_path / "fix.dll").read_bytes())

    def test_directory_and_individual_file_sources_are_copied(self) -> None:
        cases = (
            (
                self._mod(
                    "directory",
                    ("bin/fix.dll",),
                    source_path="mods/test",
                ),
                {
                    "https://raw.githubusercontent.com/example/mods/main/mods/test/bin/fix.dll": (
                        b"directory"
                    )
                },
            ),
            (
                self._mod(
                    "files",
                    ("bin/fix.dll", "settings.ini"),
                    source_files=(
                        ModSourceFile("assets/fix.dll", "bin/fix.dll"),
                        ModSourceFile("assets/settings.ini", "settings.ini"),
                    ),
                ),
                {
                    "https://raw.githubusercontent.com/example/mods/main/assets/fix.dll": (
                        b"individual dll"
                    ),
                    "https://raw.githubusercontent.com/example/mods/main/assets/settings.ini": (
                        b"individual ini"
                    ),
                },
            ),
        )
        for mod, responses in cases:
            with self.subTest(source_kind=mod.source.kind):
                for relative_path in mod.files:
                    (self.game_path / relative_path).unlink(missing_ok=True)

                def open_source(url: str, **kwargs) -> io.BytesIO:
                    return io.BytesIO(responses[url])

                with patch("scanner.mod_installer.urlopen", side_effect=open_source):
                    apply_mod_files(mod, str(self.game_path))
                for relative_path, source_content in zip(
                    mod.files,
                    responses.values(),
                ):
                    self.assertEqual(
                        source_content,
                        (self.game_path / relative_path).read_bytes(),
                    )

    def test_ssl_context_uses_available_system_ca_bundle(self) -> None:
        expected = Mock(spec=ssl.SSLContext)
        with (
            patch("scanner.mod_installer.SYSTEM_CA_BUNDLE_PATHS", (Path(__file__),)),
            patch(
                "scanner.mod_installer.ssl.create_default_context",
                return_value=expected,
            ) as create_context,
        ):
            context = create_ssl_context()

        self.assertIs(expected, context)
        create_context.assert_called_once_with(cafile=str(Path(__file__)))

    def test_reads_and_atomically_writes_catalogued_ini(self) -> None:
        mod = self._mod(
            "files",
            ("config/fix.ini",),
            source_files=(ModSourceFile("source/fix.ini", "config/fix.ini"),),
            ini_file="config/fix.ini",
        )

        self.assertEqual(("", False), read_mod_ini(mod, str(self.game_path)))

        self.assertTrue(write_mod_ini(mod, str(self.game_path), "[Fix]\nEnabled=1\n"))

        self.assertEqual(
            ("[Fix]\nEnabled=1\n", True),
            read_mod_ini(mod, str(self.game_path)),
        )
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-ini-*")))

    def test_writes_ini_when_filesystem_rejects_chmod(self) -> None:
        mod = self._mod(
            "files",
            ("fix.ini",),
            source_files=(ModSourceFile("source/fix.ini", "fix.ini"),),
            ini_file="fix.ini",
        )

        with patch(
            "scanner.mod_installer.os.chmod",
            side_effect=PermissionError("unsupported"),
        ):
            permissions_preserved = write_mod_ini(
                mod,
                str(self.game_path),
                "[Fix]\nEnabled=1\n",
            )

        self.assertFalse(permissions_preserved)
        self.assertEqual(
            ("[Fix]\nEnabled=1\n", True),
            read_mod_ini(mod, str(self.game_path)),
        )
        self.assertFalse(list(self.game_path.rglob(".anyaspectratio-ini-*")))

    def test_rejects_oversized_ini_and_mod_without_ini(self) -> None:
        mod_with_ini = self._mod(
            "files",
            ("fix.ini",),
            source_files=(ModSourceFile("source/fix.ini", "fix.ini"),),
            ini_file="fix.ini",
        )
        mod_without_ini = self._mod(
            "files",
            ("fix.dll",),
            source_files=(ModSourceFile("source/fix.dll", "fix.dll"),),
        )

        with self.assertRaisesRegex(ValueError, "256 KiB"):
            write_mod_ini(mod_with_ini, str(self.game_path), "x" * (256 * 1024 + 1))
        (self.game_path / "fix.ini").write_bytes(b"x" * (256 * 1024 + 1))
        with self.assertRaisesRegex(ValueError, "256 KiB"):
            read_mod_ini(mod_with_ini, str(self.game_path))
        with self.assertRaisesRegex(ValueError, "does not define"):
            read_mod_ini(mod_without_ini, str(self.game_path))

    def test_remove_deletes_only_catalogued_files_even_when_partial(self) -> None:
        mod = self._mod(
            "files",
            ("mods/fix.dat", "fix.ini"),
            source_files=(
                ModSourceFile("fix.dat", "mods/fix.dat"),
                ModSourceFile("fix.ini", "fix.ini"),
            ),
        )
        (self.game_path / "mods").mkdir()
        (self.game_path / "mods" / "fix.dat").write_bytes(b"mod")
        (self.game_path / "mods" / "user.dat").write_bytes(b"user")
        (self.game_path / "fix.ini").write_bytes(b"mod")

        remove_mod_files(mod, str(self.game_path))

        self.assertFalse((self.game_path / "mods" / "fix.dat").exists())
        self.assertFalse((self.game_path / "fix.ini").exists())
        self.assertEqual(b"user", (self.game_path / "mods" / "user.dat").read_bytes())

    def test_removal_deletes_same_named_logs_and_keeps_unrelated_files(self) -> None:
        mod = self._mod("archive", ("mods/Fix.ASI", "mods/Other.asi", "fix.ini"))
        directory = self.game_path / "mods"
        directory.mkdir()
        (directory / "Fix.ASI").write_bytes(b"mod")
        (directory / "Fix.log").write_bytes(b"log")
        (directory / "Other.log").write_bytes(b"log")
        (directory / "unrelated.log").write_bytes(b"keep")
        self.assertEqual([], remove_mod_files(mod, str(self.game_path)))
        self.assertFalse((directory / "Fix.ASI").exists())
        self.assertFalse((directory / "Fix.log").exists())
        self.assertFalse((directory / "Other.log").exists())
        self.assertEqual(b"keep", (directory / "unrelated.log").read_bytes())

    def test_removal_rejects_unsafe_log_before_deleting_mod_files(self) -> None:
        mod = self._mod("archive", ("fix.asi",))
        (self.game_path / "fix.asi").write_bytes(b"mod")
        with patch.object(Path, "is_symlink", autospec=True, side_effect=lambda path: path.name == "fix.log"):
            with self.assertRaisesRegex(ValueError, "symbolic link"):
                remove_mod_files(mod, str(self.game_path))
        self.assertTrue((self.game_path / "fix.asi").exists())

    def test_log_deletion_failure_is_reported(self) -> None:
        mod = self._mod("archive", ("fix.asi",))
        (self.game_path / "fix.asi").write_bytes(b"mod")
        (self.game_path / "fix.log").write_bytes(b"log")
        unlink = Path.unlink
        def guarded_unlink(path: Path, *args: object, **kwargs: object) -> None:
            if path.name == "fix.log":
                raise PermissionError("log is locked")
            unlink(path, *args, **kwargs)
        with patch.object(Path, "unlink", autospec=True, side_effect=guarded_unlink):
            with self.assertRaisesRegex(PermissionError, "log is locked"):
                remove_mod_files(mod, str(self.game_path))

    def test_removing_mod_prompts_before_deleting_modified_ini(self) -> None:
        mod = self._mod(
            "files",
            ("fix.dll", "fix.ini"),
            source_files=(
                ModSourceFile("source/fix.dll", "fix.dll"),
                ModSourceFile("source/fix.ini", "fix.ini"),
            ),
            ini_file="fix.ini",
        )
        (self.game_path / "fix.dll").write_bytes(b"mod")
        (self.game_path / "fix.ini").write_bytes(b"user configuration")
        source_url = (
            "https://raw.githubusercontent.com/example/mods/main/source/fix.ini"
        )

        with patch(
            "scanner.mod_installer.urlopen",
            return_value=io.BytesIO(b"default configuration"),
        ):
            conflicts = remove_mod_files(mod, str(self.game_path))

        self.assertEqual(["fix.ini"], conflicts)
        self.assertEqual(b"mod", (self.game_path / "fix.dll").read_bytes())
        self.assertEqual(
            b"user configuration",
            (self.game_path / "fix.ini").read_bytes(),
        )

        with patch(
            "scanner.mod_installer.urlopen",
            return_value=io.BytesIO(b"default configuration"),
        ) as open_source:
            conflicts = remove_mod_files(
                mod,
                str(self.game_path),
                preserve_modified_ini=True,
            )

        self.assertEqual([], conflicts)
        open_source.assert_called_once_with(
            source_url,
            timeout=30,
            context=ANY,
        )
        self.assertFalse((self.game_path / "fix.dll").exists())
        self.assertEqual(
            b"user configuration",
            (self.game_path / "fix.ini").read_bytes(),
        )

    def test_removing_mod_can_delete_modified_ini_when_confirmed(self) -> None:
        mod = self._mod(
            "files",
            ("fix.ini",),
            source_files=(ModSourceFile("source/fix.ini", "fix.ini"),),
            ini_file="fix.ini",
        )
        (self.game_path / "fix.ini").write_bytes(b"user configuration")

        with patch(
            "scanner.mod_installer.urlopen",
            return_value=io.BytesIO(b"default configuration"),
        ):
            conflicts = remove_mod_files(
                mod,
                str(self.game_path),
                preserve_modified_ini=False,
            )

        self.assertEqual([], conflicts)
        self.assertFalse((self.game_path / "fix.ini").exists())

    def test_removal_works_when_source_is_unavailable(self) -> None:
        mod = self._mod(
            "files",
            ("fix.dll", "fix.ini"),
            source_files=(
                ModSourceFile("source/fix.dll", "fix.dll"),
                ModSourceFile("source/fix.ini", "fix.ini"),
            ),
            ini_file="fix.ini",
        )
        (self.game_path / "fix.dll").write_bytes(b"mod")
        (self.game_path / "fix.ini").write_bytes(b"user configuration")

        with patch("scanner.mod_installer.urlopen", side_effect=OSError("offline")) as source:
            self.assertEqual(["fix.ini"], remove_mod_files(mod, str(self.game_path)))
            self.assertEqual(
                [], remove_mod_files(mod, str(self.game_path), preserve_modified_ini=True)
            )
            self.assertEqual(b"user configuration", (self.game_path / "fix.ini").read_bytes())
            self.assertEqual(
                [], remove_mod_files(mod, str(self.game_path), preserve_modified_ini=False)
            )
            self.assertEqual(2, source.call_count)
        self.assertFalse((self.game_path / "fix.ini").exists())


if __name__ == "__main__":
    unittest.main()
