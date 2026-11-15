"""Build the offline pack: repository-layout zip parts with only what the plugin installs.

It holds the catalog, every mod file the catalog references, and each mod README (no source code).
Large packs are split into individually importable ZIP parts. Keep all parts together; select any
one part in the plugin to import the complete set.

Usage: python scripts/build_offline_pack.py [version]
"""

import os
import re
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PLUGIN_ROOT.parent
MAX_PART_SIZE_BYTES = 800_000_000
ZIP_OVERHEAD_RESERVE_BYTES = 16 * 1024 * 1024
VERSION_PATTERN = re.compile(r"(?:dev|v?\d+(?:\.\d+)*)")
sys.path.insert(0, str(PLUGIN_ROOT))

import verify_catalog_steam  # noqa: E402
from update_catalog_hashes import update as update_catalog_hashes  # noqa: E402

from scanner.mod_catalog import load_mod_catalog  # noqa: E402
from store.offline_pack import OFFLINE_PACK_NAME, archive_names, parse_part_name, wanted_paths  # noqa: E402
from store.plugin_store import DEFAULT_CATALOG_PATH  # noqa: E402


def _partition_paths(
    paths: list[str],
    file_sizes: dict[str, int],
    catalog_size: int,
    max_part_size: int = MAX_PART_SIZE_BYTES,
) -> list[list[str]]:
    """Partition paths conservatively, accounting for a catalog copy in each ZIP."""
    budget = max_part_size - ZIP_OVERHEAD_RESERVE_BYTES - catalog_size
    if budget <= 0:
        raise ValueError("The offline pack catalog leaves no room for ZIP payloads.")

    parts: list[list[str]] = []
    current: list[str] = []
    current_size = 0
    for path in sorted(paths):
        size = file_sizes[path]
        if size > budget:
            raise ValueError(
                f"{path} is too large to fit in an {max_part_size // 1_000_000} MB offline part."
            )
        if current and current_size + size > budget:
            parts.append(current)
            current = []
            current_size = 0
        current.append(path)
        current_size += size
    if current:
        parts.append(current)
    return parts or [[]]


def main() -> None:
    version = sys.argv[1] if len(sys.argv) > 1 else "dev"
    if VERSION_PATTERN.fullmatch(version) is None:
        raise SystemExit("Version must be 'dev' or a numeric version such as '1.8.0'.")
    if verify_catalog_steam.main() != 0:
        raise SystemExit("Catalog does not match Steam metadata (or Steam was unreachable); see above.")
    update_catalog_hashes()
    mods = load_mod_catalog(PLUGIN_ROOT / "catalog.json")
    required, optional = wanted_paths(mods)

    root = f"{OFFLINE_PACK_NAME}-{version}"
    output_dir = PLUGIN_ROOT / "out"
    output_dir.mkdir(exist_ok=True)
    catalog = REPOSITORY_ROOT / DEFAULT_CATALOG_PATH
    if not catalog.is_file():
        raise SystemExit(f"Missing required file: {DEFAULT_CATALOG_PATH}")

    paths = sorted(required | optional)
    missing = [path for path in paths if path in required and not (REPOSITORY_ROOT / path).is_file()]
    if missing:
        raise SystemExit("Missing required files:\n" + "\n".join(missing))

    paths = [
        path
        for path in sorted(required | optional)
        if (REPOSITORY_ROOT / path).is_file()
    ]
    file_sizes = {
        path: (REPOSITORY_ROOT / path).stat().st_size
        for path in paths
    }
    parts = _partition_paths(paths, file_sizes, catalog.stat().st_size)
    names = archive_names(version, len(parts))
    for name, part_paths in zip(names, parts):
        output = output_dir / name
        temporary = output.with_suffix(output.suffix + ".tmp")
        try:
            with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
                archive.write(catalog, f"{root}/{DEFAULT_CATALOG_PATH}")
                for path in part_paths:
                    archive.write(REPOSITORY_ROOT / path, f"{root}/{path}")
            size = temporary.stat().st_size
            if size > MAX_PART_SIZE_BYTES:
                raise ValueError(
                    f"Built part exceeds {MAX_PART_SIZE_BYTES // 1_000_000} MB: {output.name}"
                )
            os.replace(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)
        print(f"{output} ({size / 1e6:.1f} MB, {len(part_paths)} files)")
    # Remove parts left over from an earlier build of this version with a different part count.
    for existing in output_dir.iterdir():
        part = parse_part_name(existing.name)
        built_earlier = existing.name == archive_names(version, 1)[0] or (part is not None and part[0] == version)
        if existing.is_file() and existing.name not in names and built_earlier:
            existing.unlink()
    print(f"Offline pack: {len(parts)} part(s), {len(mods)} mods")


if __name__ == "__main__":
    main()
