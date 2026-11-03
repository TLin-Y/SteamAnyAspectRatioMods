import io
import ssl
import tempfile
import unittest
from pathlib import Path
from unittest.mock import ANY, Mock, patch
from zipfile import ZIP_DEFLATED, ZipFile

from scanner.mod_catalog import ModDefinition, ModSource, ModSourceFile
from scanner.mod_installer import (
    _create_ssl_context,
    apply_mod_files,
    fetch_mod_readme,
    mod_readme_path,
    read_mod_ini,
    remove_mod_files,
    write_mod_ini,
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
        )

    @staticmethod
    def _archive(entries: dict[str, bytes]) -> bytes:
        content = io.BytesIO()
        with ZipFile(content, "w", ZIP_DEFLATED) as archive:
            for name, value in entries.items():
                archive.writestr(name, value)
        return content.getvalue()

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
            context = _create_ssl_context()

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

        write_mod_ini(mod, str(self.game_path), "[Fix]\nEnabled=1\n")

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


if __name__ == "__main__":
    unittest.main()
