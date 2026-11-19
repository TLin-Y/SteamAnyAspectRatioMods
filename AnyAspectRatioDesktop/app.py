import logging
import os
import subprocess
import sys
import webbrowser
from pathlib import Path

import webview

from backend import PLUGIN_NAME, DesktopBackend


def web_root() -> Path:
    bundled = getattr(sys, "_MEIPASS", None)
    return Path(bundled) / "web" if bundled else Path(__file__).resolve().parent / "web"


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
        if sys.platform == "win32":
            os.startfile(path)
        else:
            subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", str(path)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

    def open_url(self, url: str) -> None:
        if url.startswith(("https://", "http://")):
            webbrowser.open(url)

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


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    backend = DesktopBackend()
    backend.start_catalog_refresh()
    api = Api(backend)
    api._window = webview.create_window(
        PLUGIN_NAME,
        url=str(web_root() / "index.html"),
        js_api=api,
        width=1100,
        height=860,
        min_size=(420, 500),
        background_color="#0e141b",
    )
    try:
        webview.start(debug="--debug" in sys.argv)
    finally:
        backend.stop()


if __name__ == "__main__":
    main()
