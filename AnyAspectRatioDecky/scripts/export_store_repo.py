"""Export the Decky Plugin Store build of AnyAspectRatio into its own repository.

The store builds a plugin from the root of a git submodule, so the store build
lives in https://github.com/TLin-Y/AnyAspectRatio-Decky with the plugin files at
the top. This script refreshes a local clone of that repository from the
monorepo:

    python AnyAspectRatioDecky/scripts/export_store_repo.py D:/code/AnyAspectRatio-Decky

It copies an allowlist of tracked plugin files, writes the combined LICENSE and
the store README, removes the test scripts from package.json, turns off the
in-plugin self-updater and replaces its module with a stub. Python packages go
to py_modules/ and runtime data files to defaults/, because the store CI only
packages those (see ROOT_PATHS below). The result is then checked against the
store review rules.

Optional steps, each one including the ones before it:

    --build   pnpm install --frozen-lockfile, typecheck and build in the clone
    --commit  commit the export as "AnyAspectRatio <version> (SteamAnyAspectRatioMods <sha>)"
    --push    push the commit and tag it v<version>
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
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
PY_PACKAGES = ("helper", "installer", "logger", "scanner", "store", "utils")
DEFAULTS_FILES = (
    "LICENSE-Decky-Plugin-Template.txt",
    "LICENSE-LGPL-2.1.txt",
    "THIRD_PARTY_LICENSES.txt",
    "THIRD_PARTY_NOTICES.md",
    "catalog.json",
)
EXPORT_PATHS = ROOT_PATHS + PY_PACKAGES + DEFAULTS_FILES
BUILD_FLAVOR = "py_modules/helper/build_flavor.py"
# Monorepo files (relative to the repository root) copied for the store README.
SCREENSHOTS = ("resources/games-tab.jpg", "resources/edit-ini.jpg", "resources/how-mods-work.svg")
SELF_UPDATE_ON = "SELF_UPDATE_ENABLED = True"
SELF_UPDATE_OFF = "SELF_UPDATE_ENABLED = False"
UPDATER = "py_modules/installer/plugin_updater.py"
UPDATER_STUB = TEMPLATE_DIR / "plugin_updater.py"
PNPM = "pnpm@9.15.9"

# Everything allowed at the top of the store repository after an export.
STORE_TOP_LEVEL = {
    ".git", ".gitignore", "LICENSE", "README.md", "defaults", "main.py", "package.json",
    "plugin.json", "pnpm-lock.yaml", "py_modules", "rollup.config.js",
    "screenshots", "src", "tsconfig.json",
}
# Build output and dependencies, ignored by the store repository's .gitignore.
BUILD_OUTPUT = {"dist", "node_modules", "out"}
BINARY_SUFFIXES = {".7z", ".asi", ".bin", ".dll", ".exe", ".pyc", ".so", ".zip"}
# The store build must not be able to replace or restart itself.
FORBIDDEN_PYTHON = ("subprocess", "systemctl", SELF_UPDATE_ON)


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


def replace_updater(destination: Path) -> None:
    path = destination / UPDATER
    if not path.is_file():
        raise SystemExit(f"Expected the self-updater at {path}")
    shutil.copy2(UPDATER_STUB, path)


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


def exported_files(destination: Path) -> list[Path]:
    skipped = {".git"} | BUILD_OUTPUT
    return [
        path for path in destination.rglob("*")
        if path.is_file() and path.relative_to(destination).parts[0] not in skipped
    ]


def verify_export(destination: Path) -> str:
    """Check the export against the store review rules; return the plugin version."""
    problems = []
    unexpected = {entry.name for entry in destination.iterdir()} - STORE_TOP_LEVEL - BUILD_OUTPUT
    if unexpected:
        problems.append(f"unexpected top-level entries: {sorted(unexpected)}")
    for name in ("backend", "assets", "bin"):
        if (destination / name).exists():
            problems.append(f"{name}/ must not exist")

    for path in exported_files(destination):
        relative = path.relative_to(destination).as_posix()
        if path.suffix.lower() in BINARY_SUFFIXES:
            problems.append(f"binary or archive file: {relative}")
        if path.suffix == ".py":
            text = path.read_text(encoding="utf-8")
            problems += [f"{relative} contains '{word}'" for word in FORBIDDEN_PYTHON if word in text]

    if SELF_UPDATE_OFF not in (destination / BUILD_FLAVOR).read_text(encoding="utf-8"):
        problems.append(f"{BUILD_FLAVOR} must say '{SELF_UPDATE_OFF}'")
    if (destination / UPDATER).read_bytes() != UPDATER_STUB.read_bytes():
        problems.append(f"{UPDATER} is not the store stub")

    package = json.loads((destination / "package.json").read_text(encoding="utf-8"))
    manifest = json.loads((destination / "plugin.json").read_text(encoding="utf-8"))
    if "remote_binary" in package:
        problems.append("package.json must not use remote_binary")
    if any(name.startswith("test") for name in package.get("scripts", {})):
        problems.append("package.json still has test scripts")
    if package.get("version") != manifest.get("version"):
        problems.append(
            f"package.json version {package.get('version')} != plugin.json version {manifest.get('version')}"
        )
    lockfile = (destination / "pnpm-lock.yaml").read_text(encoding="utf-8")
    if not lockfile.startswith("lockfileVersion: '9.0'"):
        problems.append("pnpm-lock.yaml must be lockfileVersion '9.0'")
    license_text = (destination / "LICENSE").read_text(encoding="utf-8")
    own = (REPO_ROOT / "LICENSE").read_text(encoding="utf-8").splitlines()[0]
    template = (PLUGIN_ROOT / "LICENSE-Decky-Plugin-Template.txt").read_text(encoding="utf-8").splitlines()[0]
    if not license_text.startswith(own) or template not in license_text:
        problems.append("LICENSE must be our own license followed by the Decky template license")

    if problems:
        raise SystemExit("Store export check failed:\n  " + "\n  ".join(problems))
    return str(manifest["version"])


def build(destination: Path) -> None:
    """Install, typecheck and build like the store CI, with empty npm configs."""
    corepack = shutil.which("corepack")
    if corepack is None:
        raise SystemExit("corepack is not on PATH")
    with tempfile.TemporaryDirectory() as scratch:
        empty = Path(scratch) / "empty.npmrc"
        empty.write_text("", encoding="utf-8")
        env = {**os.environ, "NPM_CONFIG_USERCONFIG": str(empty), "NPM_CONFIG_GLOBALCONFIG": str(empty)}
        for command in (["install", "--frozen-lockfile"], ["run", "typecheck"], ["run", "build"]):
            print(f"> pnpm {' '.join(command)}", flush=True)
            if subprocess.run([corepack, PNPM, *command], cwd=destination, env=env).returncode:
                raise SystemExit(f"pnpm {' '.join(command)} failed")
    if not (destination / "dist" / "index.js").is_file():
        raise SystemExit("The build did not produce dist/index.js")


def commit(destination: Path, version: str, source: str) -> None:
    git("add", "--all", cwd=destination)
    if not git("status", "--porcelain", cwd=destination).strip():
        print("Store repository already matches this export; nothing to commit.")
        return
    git("commit", "--quiet", "-m", f"AnyAspectRatio {version} (SteamAnyAspectRatioMods {source})",
        cwd=destination)
    print(f"Committed {git('rev-parse', '--short', 'HEAD', cwd=destination).strip()}")


def push(destination: Path, version: str) -> None:
    tag = f"v{version}"
    head = git("rev-parse", "HEAD", cwd=destination).strip()
    tagged = git("tag", "--list", tag, cwd=destination).strip()
    if tagged and git("rev-list", "-n", "1", tag, cwd=destination).strip() != head:
        raise SystemExit(f"Tag {tag} already exists on another commit; bump the plugin version first.")
    if not tagged:
        git("tag", tag, cwd=destination)
    git("push", "origin", "HEAD", cwd=destination)
    git("push", "origin", tag, cwd=destination)
    print(f"Pushed {head[:7]} and tag {tag}. Submodule commit for the store PR: {head}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("destination", type=Path, help="local clone of the store repository")
    parser.add_argument(
        "--allow-dirty", action="store_true",
        help="export even if AnyAspectRatioDecky has uncommitted changes",
    )
    steps = parser.add_mutually_exclusive_group()
    steps.add_argument("--build", action="store_true", help="also install, typecheck and build")
    steps.add_argument("--commit", action="store_true", help="also build and commit")
    steps.add_argument("--push", action="store_true", help="also build, commit, push and tag")
    args = parser.parse_args()
    destination: Path = args.destination.resolve()

    check_destination(destination)
    dirty = git("status", "--porcelain", "--", "AnyAspectRatioDecky", "resources", "LICENSE")
    if dirty.strip() and not args.allow_dirty:
        print(dirty, file=sys.stderr)
        raise SystemExit("Commit the plugin changes first, or pass --allow-dirty.")
    if dirty.strip() and (args.commit or args.push):
        raise SystemExit("Only a clean monorepo can be committed to the store repository.")

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
    replace_updater(destination)
    strip_test_scripts(destination)
    version = verify_export(destination)

    source = git("rev-parse", "--short", "HEAD").strip()
    print(f"Exported {len(files)} plugin files to {destination}")
    print(f"Source: SteamAnyAspectRatioMods {source}{' (dirty)' if dirty.strip() else ''}, version {version}")

    if args.build or args.commit or args.push:
        build(destination)
    if args.commit or args.push:
        commit(destination, version, source)
    if args.push:
        push(destination, version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
