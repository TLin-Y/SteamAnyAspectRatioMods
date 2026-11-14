"""Write SHA-256 checksums into catalog.json.

Archive sources get the zip's "sha256": the plugin verifies the download and keys its cache on it,
so run this after changing any mod zip and commit the catalog with it. Loose-file sources
("files"/"directory") get "file_hashes", one checksum per installed file, verified after download.

Usage: python scripts/update_catalog_hashes.py [--check]
  --check   change nothing; exit 1 when a checksum is missing or stale
"""

import hashlib
import json
import re
import sys
from pathlib import Path

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PLUGIN_ROOT.parent
CATALOG_PATH = PLUGIN_ROOT / "catalog.json"

ZIP_PATH_LINE = re.compile(r'^(\s*)"path":\s*"([^"]+\.zip)"(,?)\s*$')
HASH_LINE = re.compile(r'^\s*"sha256":')
LOOSE_KIND_LINE = re.compile(r'^(\s*)"kind":\s*"(files|directory)",\s*$')
FILE_HASHES_START = re.compile(r'^\s*"file_hashes":\s*\{')
FILE_HASHES_END = re.compile(r"^\s*\},?\s*$")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def loose_file_hashes(catalog: dict) -> list[dict[str, str]]:
    """Per installed file checksums for each files/directory source, in catalog order."""
    result = []
    for mod in catalog["mods"]:
        source = mod["source"]
        if source["kind"] == "files":
            pairs = [(item["destination"], item["path"]) for item in source["files"]]
        elif source["kind"] == "directory":
            pairs = [(path, f"{source['path']}/{path}") for path in mod["files"]]
        else:
            continue
        hashes = {}
        for installed, repository_path in pairs:
            path = REPOSITORY_ROOT / repository_path
            if not path.is_file():
                raise SystemExit(f"Missing mod file: {repository_path}")
            hashes[installed] = file_sha256(path)
        result.append(hashes)
    return result


def _without_file_hashes(lines: list[str]) -> list[str]:
    kept, skipping = [], False
    for line in lines:
        if skipping:
            skipping = not FILE_HASHES_END.match(line)
        elif FILE_HASHES_START.match(line):
            skipping = True
        else:
            kept.append(line)
    return kept


def updated_catalog_text(text: str) -> str:
    newline = "\r\n" if "\r\n" in text else "\n"
    loose_hashes = iter(loose_file_hashes(json.loads(text)))
    lines = [line for line in _without_file_hashes(text.splitlines()) if not HASH_LINE.match(line)]
    output: list[str] = []
    for index, line in enumerate(lines):
        kind = LOOSE_KIND_LINE.match(line)
        if kind is not None:
            indent = kind.group(1)
            hashes = list(next(loose_hashes).items())
            output.append(line)
            output.append(f'{indent}"file_hashes": {{')
            output.extend(
                f'{indent}  {json.dumps(path)}: "{digest}"{"," if position < len(hashes) - 1 else ""}'
                for position, (path, digest) in enumerate(hashes)
            )
            output.append(f"{indent}}},")
            continue
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
