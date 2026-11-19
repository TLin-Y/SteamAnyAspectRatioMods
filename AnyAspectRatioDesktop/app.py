import logging
import os
import subprocess
import sys
import tempfile
import traceback
from pathlib import Path

import webview

from backend import BUNDLE_DIR, PLUGIN_NAME, DesktopBackend, system_environment

# Linux windows use Qt from pip (PySide6), so the packaged app needs no distro GTK packages.
USE_QT = sys.platform not in ("win32", "darwin")
PAGE = "web/index.html"
ICON = "assets/icon.png"  # build.py also turns it into the .exe and .app icons.


def open_with_system(target: str) -> None:
    """Show a folder in the file manager or a web page in the browser."""
    if sys.platform == "win32":
        os.startfile(target)
    else:
        subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", target], env=system_environment(),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)


def app_file(relative: str) -> Path:
    """A file shipped with the app: unpacked by PyInstaller, or in this folder of a checkout."""
    return (Path(BUNDLE_DIR) if BUNDLE_DIR else Path(__file__).resolve().parent) / relative


class Api:
    """What the page reaches through window.pywebview.api. Private attributes stay hidden from it."""

    def __init__(self, backend: DesktopBackend) -> None:
        self._backend = backend
        self._window: webview.Window | None = None

    def call(self, method: str, args: list | None = None) -> object:
        return self._backend.call(method, *(args or []))

    def launch(self, action: str, args: list | None = None) -> object:
        return self._backend.launch(action, *(args or []))

    def system_info(self) -> dict[str, object]:
        return self._backend.system_info()

    def games_by_folder(self) -> list[dict[str, object]]:
        return self._backend.games_by_folder()

    def virtual_desktops(self) -> list[dict[str, object]]:
        return self._backend.virtual_desktops()

    def open_folder(self, folder: str) -> None:
        path = Path(folder)
        if not path.is_absolute() or not path.is_dir():
            raise ValueError(f"Folder not found: {folder}")
        open_with_system(str(path))

    def open_url(self, url: str) -> None:
        if url.startswith(("https://", "http://")):
            open_with_system(url)

    def pick_path(self, folder: bool, extensions: list[str]) -> str | None:
        if self._window is None:
            return None
        if folder:
            picked = self._window.create_file_dialog(webview.FileDialog.FOLDER)
        else:
            patterns = ";".join(f"*.{extension}" for extension in extensions) or "*.*"
            picked = self._window.create_file_dialog(
                webview.FileDialog.OPEN, file_types=(f"Files ({patterns})", "All files (*.*)")
            )
        return picked[0] if picked else None


def self_test(report: Path) -> None:
    """build.py runs the packaged app this way: it must hold the plugin, the page and a working web view."""
    try:
        for name in (PAGE, "web/styles.css", "web/dist/app.js", ICON):
            if not app_file(name).is_file():
                raise FileNotFoundError(app_file(name))
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as data_dir:
            backend = DesktopBackend(Path(data_dir))
            try:
                mods = backend.call("get_settings")["catalog"]["mod_count"]
                backend.call("scan_supported_games")
            finally:
                backend.stop()
        # A hidden window that closes once the page has loaded proves the toolkit and its web engine start.
        window = webview.create_window(PLUGIN_NAME, url=str(app_file(PAGE)), hidden=True)
        window.events.loaded += window.destroy
        start_window()
        report.write_text(f"ok: {mods} mods, page loaded", encoding="utf-8")
    except BaseException:
        report.write_text(traceback.format_exc(), encoding="utf-8")


def start_window(debug: bool = False) -> None:
    if USE_QT:
        # Ubuntu 24.04+ blocks the user namespaces Chromium's sandbox needs (the page is the app's own), and
        # without GBM (virtual machines, some NVIDIA drivers) Qt WebEngine's GPU path draws a blank page.
        flags = os.environ.get("QTWEBENGINE_CHROMIUM_FLAGS", "")
        os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = f"{flags} --no-sandbox --disable-gpu".strip()
    # Only Qt takes a PNG here; the Windows window shows the .exe's icon and macOS the .app's.
    webview.start(gui="qt" if USE_QT else None, debug=debug, icon=str(app_file(ICON)) if USE_QT else None)


def main() -> None:
    if len(sys.argv) == 3 and sys.argv[1] == "--self-test":
        return self_test(Path(sys.argv[2]))
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    backend = DesktopBackend()
    backend.start_catalog_refresh()
    api = Api(backend)
    api._window = webview.create_window(
        PLUGIN_NAME,
        url=str(app_file(PAGE)),
        js_api=api,
        width=1100,
        height=860,
        min_size=(420, 500),
        background_color="#0e141b",
    )
    try:
        start_window(debug="--debug" in sys.argv)
    finally:
        backend.stop()


if __name__ == "__main__":
    main()
