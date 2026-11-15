from support.builders import RAW, directory_source, mod
from support.fakes import fetched, serving
from support.sandbox import SandboxTest

from store.mod_sources import fetch_mod_readme, mod_readme_path


class ModSourceTests(SandboxTest):
    def test_readme_comes_from_the_top_folder_of_a_nested_source(self) -> None:
        nested = mod(directory_source("Ninja Gaiden Sigma 2/nested/mod"), "Game/fix.dll")
        self.assertEqual("Ninja Gaiden Sigma 2/README.md", mod_readme_path(nested))
        with serving(b"# Mod\n\nRead me") as network:
            self.assertEqual("# Mod\n\nRead me", fetch_mod_readme(nested))
        self.assertEqual([f"{RAW}/Ninja%20Gaiden%20Sigma%202/README.md"], fetched(network))
