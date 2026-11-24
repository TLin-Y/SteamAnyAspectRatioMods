"""Builds the desktop app for the OS it runs on (PyInstaller cannot cross-build).

Windows gets one .exe, macOS an .app bundle, Linux a folder (its Qt libraries stay replaceable, as the LGPL asks).

python -m pip install -r requirements.txt
python build.py            -> build/release/AnyAspectRatio-Desktop-<version>-<OS>.<ext>
python build.py --skip-web -> reuse web/dist built on another machine (one without Node)
"""

import json
import re
import shutil
import subprocess
import sys
import sysconfig
from importlib import metadata
from pathlib import Path

import PyInstaller.__main__
from packaging.requirements import Requirement
from packaging.utils import canonicalize_name

HERE = Path(__file__).resolve().parent
PLUGIN = HERE.parent / "AnyAspectRatioDecky"
WEB = HERE / "web"
BUILD = HERE / "build"
ICON = HERE / "assets" / "icon.png"
LICENSES = BUILD / "THIRD_PARTY_LICENSES.txt"
NAME = "AnyAspectRatio"
LINUX = sys.platform not in ("win32", "darwin")
SEPARATOR = ";" if sys.platform == "win32" else ":"
UNUSED_QT = ("QtQml", "QtQuick", "QtQuickWidgets", "QtOpenGLWidgets", "QtDataVisualization")
# Linux: every distro has these, and they must match its graphics drivers, fonts and sound; the build machine's
# older copies would break them. Qt's GTK theme plugin would load the bundled GTK against the desktop's own
# GTK modules; Qt falls back to the desktop portal (native file dialogs) without it.
HOST_FILES = (
    "libstdc++.so.6", "libgcc_s.so.1", "libgbm.so.1", "libfontconfig.so.1", "libfreetype.so.6", "libasound.so.2",
    "PySide6/Qt/plugins/platformthemes/libqgtk3.so",
)


def run(*command: str, cwd: Path = HERE) -> None:
    subprocess.run([shutil.which(command[0]) or command[0], *command[1:]], cwd=cwd, check=True)


def write_licenses() -> None:
    """The license texts of Python and the packages frozen into the app, which their licenses ask to ship along."""
    found: dict[str, metadata.Distribution] = {}
    queue = ["pywebview", *(["PySide6", "QtPy"] if LINUX else [])]
    while queue:
        name = canonicalize_name(queue.pop())
        if name in found:
            continue
        try:
            found[name] = distribution = metadata.distribution(name)
        except metadata.PackageNotFoundError:
            continue
        for requirement in map(Requirement, distribution.requires or []):
            if requirement.marker is None or requirement.marker.evaluate({"extra": ""}):
                queue.append(requirement.name)
    python_license = next(
        (path for path in (Path(sys.base_prefix) / "LICENSE.txt", Path(sysconfig.get_paths()["stdlib"]) / "LICENSE.txt")
         if path.is_file()), None,
    )
    sections = [(f"Python {sys.version.split()[0]}", python_license.read_text(encoding="utf-8", errors="replace")
                 if python_license else "Python Software Foundation License: https://docs.python.org/3/license.html")]
    for distribution in sorted(found.values(), key=lambda item: item.metadata["Name"].casefold()):
        files = [file for file in distribution.files or [] if re.search(r"LICEN[CS]E|COPYING|NOTICE", file.name, re.I)]
        texts = [Path(distribution.locate_file(file)).read_text(encoding="utf-8", errors="replace") for file in files]
        declared = distribution.metadata.get("License-Expression") or distribution.metadata.get("License") or "see the project"
        sections.append((f"{distribution.metadata['Name']} {distribution.version}", "\n\n".join(texts) or f"License: {declared}"))
    if LINUX:  # PySide6's wheels name the LGPL (which builds on the GPL) but carry neither text.
        sections += [(path.stem, path.read_text(encoding="utf-8")) for path in sorted((HERE / "licenses").glob("*.txt"))]
    LICENSES.parent.mkdir(parents=True, exist_ok=True)
    LICENSES.write_text("".join(f"{'=' * 72}\n{title}\n{'=' * 72}\n\n{text.strip()}\n\n" for title, text in sections),
                        encoding="utf-8")


def freeze() -> Path:
    write_licenses()
    data = {
        PLUGIN / "catalog.json": ".",
        PLUGIN / "plugin.json": ".",  # The installed version for the update check.
        WEB / "index.html": "web",
        WEB / "styles.css": "web",
        WEB / "dist" / "app.js": "web/dist",
        ICON: "assets",
        LICENSES: ".",
    }
    windows = sys.platform == "win32"
    PyInstaller.__main__.run([
        str(HERE / "app.py"),
        "--name", NAME,
        "--windowed",
        "--onefile" if windows else "--onedir",
        "--noconfirm",
        "--clean",
        "--paths", str(PLUGIN),
        "--hidden-import", "main",  # The plugin's main.py, imported by name at run time.
        "--exclude-module", "tkinter",
        # Qt WebEngine links Qt Quick, so PyInstaller would add its Python module and every QML plugin (3D, charts...).
        *(argument for module in UNUSED_QT for argument in ("--exclude-module", f"PySide6.{module}")),
        "--icon", str(ICON),  # Pillow converts it to the .exe's .ico and the .app's .icns.
        "--osx-bundle-identifier", "io.github.tlin-y.anyaspectratio",
        "--distpath", str(BUILD / "dist"),
        "--workpath", str(BUILD / "work"),
        "--specpath", str(BUILD),
        *(argument for source, target in data.items() for argument in ("--add-data", f"{source}{SEPARATOR}{target}")),
    ])
    dist = BUILD / "dist"
    if LINUX:
        for name in HOST_FILES:
            path = dist / NAME / "_internal" / name
            if path.exists():
                path.unlink()
            else:
                print(f"Note: {name} is not in the bundle; check whether PyInstaller's layout changed.")
    return {"win32": dist / f"{NAME}.exe", "darwin": dist / f"{NAME}.app"}.get(sys.platform, dist / NAME)


def executable(app: Path) -> Path:
    if app.suffix == ".app":
        return app / "Contents" / "MacOS" / NAME
    return app / NAME if app.is_dir() else app


def self_test(app: Path) -> None:
    report = BUILD / "self-test.txt"
    report.unlink(missing_ok=True)
    subprocess.run([str(executable(app)), "--self-test", str(report)], timeout=300, check=False)
    result = report.read_text(encoding="utf-8") if report.is_file() else "no report written"
    print(f"Self-test: {result}")
    if not result.startswith("ok"):
        sys.exit("The packaged app failed its self-test")


def release(app: Path, version: str) -> Path:
    out = BUILD / "release"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    stem = f"{NAME}-Desktop-{version}-" + {"win32": "Windows", "darwin": "macOS"}.get(sys.platform, "Linux")
    if sys.platform == "win32":
        target = out / f"{stem}.exe"
        shutil.copy2(app, target)
    elif sys.platform == "darwin":
        # ditto keeps the bundle's symlinks and code signature intact.
        target = out / f"{stem}.zip"
        run("ditto", "-c", "-k", "--keepParent", str(app), str(target))
    else:
        # A tarball keeps the executable bits; xz makes it about a quarter smaller than gzip.
        target = out / f"{stem}.tar.xz"
        run("tar", "-cJf", str(target), "-C", str(app.parent), app.name)
    # Also beside the download: the one-file .exe keeps its copy out of sight.
    shutil.copy2(LICENSES, out / f"{stem}-licenses.txt")
    return target


def main() -> None:
    version = json.loads((PLUGIN / "package.json").read_text(encoding="utf-8"))["version"]
    if "--skip-web" not in sys.argv:
        run("npm", "ci", cwd=WEB)
        run("node", "build.mjs", "--minify", cwd=WEB)
    app = freeze()
    self_test(app)
    print(f"Release file: {release(app, version)}")


if __name__ == "__main__":
    main()
