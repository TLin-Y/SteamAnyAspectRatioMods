from helper.errors import PluginError
from installer import game_folder
from scanner.mod_catalog import ModDefinition, mod_log_names
from utils import folders, sizes, text_files

MAX_MOD_LOG_SIZE_BYTES = 256 * sizes.KIB


def existing_mod_logs(mod_files: tuple[str, ...] | list[str], game_path: str) -> list[str]:
    root = game_folder.root(game_path)
    return [name for name in mod_log_names(mod_files) if folders.resolve_inside(root, name).is_file()]


def read_mod_log(mod: ModDefinition, game_path: str, name: str) -> tuple[str, bool]:
    if name not in mod_log_names(mod.files):
        raise PluginError("Log is not associated with this mod")
    path = folders.resolve_inside(game_folder.root(game_path), name)
    if not path.is_file():
        raise PluginError("Mod log is no longer available")
    return text_files.read_tail(path, MAX_MOD_LOG_SIZE_BYTES)
