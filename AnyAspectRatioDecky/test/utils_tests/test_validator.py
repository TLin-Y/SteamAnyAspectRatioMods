import unittest

from helper.validator import Validator


class ValidatorTests(unittest.TestCase):
    def check(self, rule, accepted, rejected) -> None:
        for value in accepted:
            with self.subTest(rule=rule.__qualname__, value=value):
                self.assertTrue(rule(value))
        for value in rejected:
            with self.subTest(rule=rule.__qualname__, value=value):
                self.assertFalse(rule(value))

    def test_relative_path_stays_inside_its_folder(self) -> None:
        self.check(
            Validator.Paths.is_relative,
            ["dinput8.dll", "P3R/Binaries/Win64/P3RFix.asi", "a.b/c..d"],
            ["", "/etc/passwd", "../x", "a/../../x", "a/./b", "a//b", "a/", "a\\b", "C:/x", "a\0b"],
        )

    def test_file_names(self) -> None:
        self.check(
            lambda name: Validator.Files.has_extension(name, ".asi"),
            ["mod.asi", "Mod.ASI", "bin/Fix.Asi"],
            ["mod.asi.bak", "mod.dll", "asi", ""],
        )
        self.check(Validator.Files.is_hidden, [".git", ".DS_Store"], ["mods", "a.b", ""])

    def test_os_junk_and_helper_executables(self) -> None:
        self.check(
            Validator.Files.is_os_junk,
            ["__MACOSX/._mod.asi", ".DS_Store", "bin/Thumbs.db"],
            ["mod.asi", "bin/thumbs.dbx", "MACOSX/x"],
        )
        self.check(
            Validator.Files.is_helper_executable,
            ["unins000.exe", "UnityCrashHandler64.exe", "EasyAntiCheat_Setup.exe", "GameLauncher.exe"],
            ["DarkSoulsIII.exe", "P5R.exe"],
        )

    def test_proxy_dlls(self) -> None:
        self.check(Validator.Wine.is_proxy_dll, ["dinput8", "WINMM", "xinput1_3"], ["d3d12", "dinput8.dll", ""])

    def test_sha256(self) -> None:
        self.check(Validator.Hash.sha256, ["0" * 64, "ab" * 32], ["", "A" * 64, "0" * 63, "g" * 64, None, 1])

    def test_github_repository(self) -> None:
        self.check(
            Validator.GitHub.repository,
            ["https://github.com/owner/repo", "https://github.com/owner/repo/", "https://github.com/a/" + "r" * 150],
            [
                "http://github.com/owner/repo",
                "https://gitlab.com/owner/repo",
                "https://github.com/owner",
                "https://github.com/owner/repo/tree/main",
                "https://github.com/owner/repo?x=1",
                "https://github.com/owner/repo#readme",
                "https://github.com/own er/repo",
            ],
        )

    def test_git_ref_follows_git_rules_only(self) -> None:
        self.check(
            Validator.GitHub.ref,
            ["main", "v1.9.0", "feature/x", "v1.0+build", "user@topic", "a" * 300, "0123abc"],
            ["", "-x", "/x", "a..b", "x/", "x.", "x.lock", "a b", "a~1", "a^", "a:b", "a?", "a*", "a[b", "a\\b",
             "a@{1}", "a\tb", "a\nb"],
        )

    def test_resolution(self) -> None:
        self.check(
            Validator.Settings.resolution,
            [1, 1280, Validator.Settings.MAX_RESOLUTION],
            [0, -1, Validator.Settings.MAX_RESOLUTION + 1, 800.0, True, "800"],
        )

    def test_catalog_names_follow_our_convention_without_length_limits(self) -> None:
        self.check(Validator.Catalog.mod_id, ["p3r", "dark-souls-3", "a" * 200], ["", "Upper", "a_b", "a b", None])
        self.check(
            Validator.Catalog.display_tag,
            ["stretch", "may-be-outdated-report-to-lyall", "for_v1"],
            ["", "Upper", "a b", None],
        )
        self.check(Validator.Catalog.steam_app_id, ["1687950"], ["", "12a", "-1", "١٢"])

    def test_wine_override_uses_wine_load_orders(self) -> None:
        self.check(
            Validator.Wine.override,
            ["dinput8=n,b", "winmm=n", "msvcp140,vcruntime140,vcruntime140_1=b", "d3d9,dinput8=n,b"],
            ["", "dinput8", "dinput8=", "dinput8=x", "DINPUT8=n", "dinput8=n;b", ",dinput8=n", "a b=n", 'a"=n', None],
        )

    def test_ini_names_mirror_what_the_ini_rewriter_can_find(self) -> None:
        self.check(
            Validator.Ini.section,
            ["Aspect Ratio", "Display:Settings", "Graphics/Quality"],
            ["", " Padded", "a]b", "a\nb", "é"],
        )
        self.check(Validator.Ini.key, ["Width", "FOV_Scale", "a" * 200], ["", "1Width", "Wid th", "_x", "a\nb"])


if __name__ == "__main__":
    unittest.main()
