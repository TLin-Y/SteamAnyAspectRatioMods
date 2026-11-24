from helper.errors import PluginError
from installer import backups, game_folder, launcher, mod_ini
from scanner.mod_catalog import ModDefinition, mod_log_names
from utils import files, folders


def remove_mod_files(
    mod: ModDefinition,
    game_path: str,
    *,
    preserve_modified_ini: bool | None = None,
    ini_resolution: tuple[int, int] | None = None,
) -> list[str]:
    if mod.manual:
        raise PluginError(f"Mod {mod.mod_id} must be installed manually; follow its ReadMe")
    game_root = game_folder.root(game_path)
    file_backups = backups.resolve_files(mod, game_root)
    backups.check_before_remove(file_backups)
    game_launcher = launcher.resolve(mod, game_root)
    launcher.check_before_remove(game_launcher)

    kept_ini = None
    if mod.ini_file is not None and preserve_modified_ini is not False:
        ini_path = folders.resolve_inside(game_root, mod.ini_file)
        if ini_path.is_file() and mod_ini.is_modified(mod, ini_path, ini_resolution):
            if preserve_modified_ini is None:
                return [mod.ini_file]
            kept_ini = ini_path

    targets = list(dict.fromkeys(
        folders.resolve_inside(game_root, name) for name in (*mod.files, *mod_log_names(mod.files))
    ))
    # A launcher copy made from a mod file is removed after the original launcher is back.
    shipped_launcher = (
        game_launcher.source
        if game_launcher is not None and game_launcher.entry.source in mod.files else None
    )
    backups.restore(file_backups)
    for target in targets:
        if target not in (kept_ini, shipped_launcher) and target not in file_backups:
            files.delete(target)
    launcher.restore(game_launcher)
    if shipped_launcher is not None:
        files.delete(shipped_launcher)
    for backup in file_backups.values():
        files.delete(backup)
    folders.remove_empty_parents([target for target in targets if target != kept_ini], game_root)
    return []
