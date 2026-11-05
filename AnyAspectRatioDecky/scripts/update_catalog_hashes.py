"""Write the SHA-256 of every archive mod's zip into catalog.json.

The plugin keys its downloaded-archive cache on this checksum, so run this after changing any
mod zip and commit the catalog with it; users then get the new zip without clearing a cache.

Usage: python scripts/update_catalog_hashes.py [--check]
  --check   change nothing; exit 1 when a checksum is missing or stale
"""

import hashlib
import re
import sys
from pathlib import Path

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PLUGIN_ROOT.parent
CATALOG_PATH = PLUGIN_ROOT / "catalog.json"

ZIP_PATH_LINE = re.compile(r'^(\s*)"path":\s*"([^"]+\.zip)"(,?)\s*$')
HASH_LINE = re.compile(r'^\s*"sha256":')


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def updated_catalog_text(text: str) -> str:
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = [line for line in text.splitlines() if not HASH_LINE.match(line)]
    output: list[str] = []
    for index, line in enumerate(lines):
        match = ZIP_PATH_LINE.match(line)
        if match is None:
            output.append(line)
            continue
        indent, zip_path, _ = match.groups()
        archive = REPOSITORY_ROOT / zip_path
        if not archive.is_file():
            raise SystemExit(f"Missing archive: {zip_path}")
        # The hash follows the path; it only takes a trailing comma when more keys come after it.
        more_keys = not lines[index + 1].lstrip().startswith("}")
        output.append(f'{indent}"path": "{zip_path}",')
        output.append(f'{indent}"sha256": "{file_sha256(archive)}"{"," if more_keys else ""}')
    return newline.join(output) + (newline if text.endswith("\n") else "")


def update(check: bool = False) -> bool:
    """Rewrite the catalog; returns True when it changed (or would change with check=True)."""
    original = CATALOG_PATH.read_bytes().decode("utf-8")
    updated = updated_catalog_text(original)
    if updated == original:
        return False
    if not check:
        CATALOG_PATH.write_bytes(updated.encode("utf-8"))
    return True


def main() -> None:
    check = "--check" in sys.argv[1:]
    changed = update(check)
    if check and changed:
        raise SystemExit("catalog.json archive checksums are stale: run scripts/update_catalog_hashes.py")
    print("catalog.json checksums " + ("updated" if changed else "are up to date"))


if __name__ == "__main__":
    main()
