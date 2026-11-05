"""Build the offline pack: a repository-layout zip with only what the plugin installs.

It holds the catalog, every mod file the catalog references, and each mod README (no source code).
Import it in the plugin with Mods > Import repository zip.

Usage: python scripts/build_offline_pack.py [version]
"""

import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PLUGIN_ROOT.parent
sys.path.insert(0, str(PLUGIN_ROOT))

from scanner.mod_catalog import load_mod_catalog  # noqa: E402
from store.pack import _wanted_paths  # noqa: E402
from store.plugin_store import DEFAULT_CATALOG_PATH  # noqa: E402
from update_catalog_hashes import update as update_catalog_hashes  # noqa: E402


def main() -> None:
    version = sys.argv[1] if len(sys.argv) > 1 else "dev"
    update_catalog_hashes()
    mods = load_mod_catalog(PLUGIN_ROOT / "catalog.json")
    required, optional = _wanted_paths(mods)

    root = f"AnyAspectRatio-Offline-{version}"
    output = PLUGIN_ROOT / "out" / f"{root}.zip"
    output.parent.mkdir(exist_ok=True)

    paths = sorted(required | optional | {DEFAULT_CATALOG_PATH})
    missing = [path for path in paths if path in required and not (REPOSITORY_ROOT / path).is_file()]
    if missing:
        raise SystemExit("Missing required files:\n" + "\n".join(missing))

    with ZipFile(output, "w", ZIP_DEFLATED) as archive:
        for path in paths:
            source = REPOSITORY_ROOT / path
            if source.is_file():
                archive.write(source, f"{root}/{path}")
    print(f"{output} ({output.stat().st_size / 1e6:.1f} MB, {len(mods)} mods)")


if __name__ == "__main__":
    main()
