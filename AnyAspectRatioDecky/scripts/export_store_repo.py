"""Export the Decky Plugin Store build of AnyAspectRatio into its own repository.

The store builds a plugin from the root of a git submodule, so the store build
lives in https://github.com/TLin-Y/AnyAspectRatio-Decky with the plugin files at
the top. This script refreshes a local clone of that repository from the
monorepo:

    python AnyAspectRatioDecky/scripts/export_store_repo.py D:/code/AnyAspectRatio-Decky

It copies an allowlist of tracked plugin files, writes the combined LICENSE and
the store README, removes the test scripts from package.json, and turns off the
in-plugin self-updater. Python packages go to py_modules/ and runtime data files
to defaults/, because the store CI only packages those (see ROOT_PATHS below).
It never commits or pushes.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = PLUGIN_ROOT.parent
TEMPLATE_DIR = Path(__file__).resolve().parent / "store_repo"
STORE_REPOSITORY = "TLin-Y/AnyAspectRatio-Decky"

# The store CI (decky CLI `plugin build`) zips only LICENSE, README.md, main.py,
# package.json, plugin.json, root *.py, dist/, bin/, py_modules/ and the contents
# of defaults/ (placed at the plugin root). Python packages therefore go to
# py_modules/ and runtime data files to defaults/.
ROOT_PATHS = (
    ".gitignore",
    "main.py",
    "package.json",
    "plugin.json",
    "pnpm-lock.yaml",
    "rollup.config.js",
    "tsconfig.json",
    "src",
)
PY_PACKAGES = ("helper", "logger", "scanner", "store", "utils")
DEFAULTS_FILES = (
    "LICENSE-Decky-Plugin-Template.txt",
    "LICENSE-LGPL-2.1.txt",
    "THIRD_PARTY_NOTICES.md",
    "catalog.json",
)
EXPORT_PATHS = ROOT_PATHS + PY_PACKAGES + DEFAULTS_FILES
BUILD_FLAVOR = "py_modules/helper/build_flavor.py"
# Monorepo files (relative to the repository root) copied for the store README.
SCREENSHOTS = ("resources/games-tab.jpg", "resources/edit-ini.jpg")
SELF_UPDATE_ON = "SELF_UPDATE_ENABLED = True"
SELF_UPDATE_OFF = "SELF_UPDATE_ENABLED = False"


def git(*args: str, cwd: Path = REPO_ROOT) -> str:
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout


def plugin_files() -> list[str]:
    """Tracked plus new, not-ignored files under the allowlisted plugin paths."""
    listed = git(
        "ls-files", "--cached", "--others", "--exclude-standard", "-z", "--",
        *(f"AnyAspectRatioDecky/{path}" for path in EXPORT_PATHS),
    )
    prefix = "AnyAspectRatioDecky/"
    files = sorted({name[len(prefix):] for name in listed.split("\0") if name})
    return [name for name in files if (PLUGIN_ROOT / name).is_file()]


def store_path(name: str) -> str:
    """Where a plugin file goes in the store repository."""
    top = name.split("/", 1)[0]
    if top in PY_PACKAGES:
        return f"py_modules/{name}"
    if name in DEFAULTS_FILES:
        return f"defaults/{name}"
    return name


def check_destination(destination: Path) -> None:
    if not (destination / ".git").is_dir():
        raise SystemExit(f"{destination} is not a git clone of {STORE_REPOSITORY}")
    origin = git("remote", "get-url", "origin", cwd=destination).strip()
    if STORE_REPOSITORY.lower() not in origin.lower():
        raise SystemExit(f"{destination} origin is {origin}, expected {STORE_REPOSITORY}")
    if destination.resolve() == REPO_ROOT.resolve():
        raise SystemExit("Destination must not be the monorepo")


def clear_destination(destination: Path) -> None:
    for entry in destination.iterdir():
        if entry.name == ".git":
            continue
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()


def newline_of(data: bytes) -> bytes:
    return b"\r\n" if b"\r\n" in data else b"\n"


def write_license(destination: Path) -> None:
    """Own MIT license first, then the template's BSD license, as the store requires."""
    own = (REPO_ROOT / "LICENSE").read_bytes().rstrip()
    template = (PLUGIN_ROOT / "LICENSE-Decky-Plugin-Template.txt").read_bytes().rstrip()
    newline = newline_of(own)
    template = template.replace(b"\r\n", b"\n").replace(b"\n", newline)
    separator = newline * 2 + b"-" * 79 + newline * 2
    (destination / "LICENSE").write_bytes(own + separator + template + newline)


def disable_self_update(destination: Path) -> None:
    path = destination / BUILD_FLAVOR
    text = path.read_bytes().decode("utf-8")
    if text.count(SELF_UPDATE_ON) != 1:
        raise SystemExit(f"Expected one '{SELF_UPDATE_ON}' line in {path}")
    path.write_bytes(text.replace(SELF_UPDATE_ON, SELF_UPDATE_OFF).encode("utf-8"))


def strip_test_scripts(destination: Path) -> None:
    """The test suite stays in the monorepo, so drop the scripts that run it."""
    path = destination / "package.json"
    raw = path.read_bytes()
    package = json.loads(raw)
    scripts = package.get("scripts", {})
    for name in [name for name in scripts if name.startswith("test")]:
        del scripts[name]
    newline = newline_of(raw).decode()
    text = json.dumps(package, indent=2, ensure_ascii=False) + "\n"
    path.write_bytes(text.replace("\n", newline).encode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("destination", type=Path, help="local clone of the store repository")
    parser.add_argument(
        "--allow-dirty", action="store_true",
        help="export even if AnyAspectRatioDecky has uncommitted changes",
    )
    args = parser.parse_args()
    destination: Path = args.destination.resolve()

    check_destination(destination)
    dirty = git("status", "--porcelain", "--", "AnyAspectRatioDecky", "resources", "LICENSE")
    if dirty.strip() and not args.allow_dirty:
        print(dirty, file=sys.stderr)
        raise SystemExit("Commit the plugin changes first, or pass --allow-dirty.")

    files = plugin_files()
    clear_destination(destination)
    for name in files:
        target = destination / store_path(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(PLUGIN_ROOT / name, target)
    for name in SCREENSHOTS:
        target = destination / "screenshots" / Path(name).name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO_ROOT / name, target)
    shutil.copy2(TEMPLATE_DIR / "README.md", destination / "README.md")
    write_license(destination)
    disable_self_update(destination)
    strip_test_scripts(destination)

    commit = git("rev-parse", "--short", "HEAD").strip()
    print(f"Exported {len(files)} plugin files to {destination}")
    print(f"Source: SteamAnyAspectRatioMods {commit}{' (dirty)' if dirty.strip() else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
