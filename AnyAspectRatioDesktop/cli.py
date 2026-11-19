import argparse
import json
import logging
import sys

from backend import DesktopBackend


def print_games(report: dict) -> None:
    steam = report["steam_scan"]
    print(f"Steam: {steam['libraries_scanned']} libraries, {steam['apps_scanned']} apps")
    for game in report["supported_games"]:
        status = "update available" if game["update_available"] else game["file_status"]
        print(f"  {game['mod_id']:<32} {game['app_name']:<45} {status}")
        print(f"  {'':<32} {game['game_path']}")
    for warning in report["warnings"]:
        print(f"Warning: {warning}")


def find_game(backend: DesktopBackend, mod_id: str) -> str:
    report = backend.call("scan_supported_games")
    paths = [game["game_path"] for game in report["supported_games"] if game["mod_id"] == mod_id and game["game_path"]]
    if len(paths) != 1:
        sys.exit(f"{mod_id}: expected one installed game, found {len(paths)}")
    return paths[0]


def set_applied(backend: DesktopBackend, mod_id: str, applied: bool, force: bool) -> None:
    game_path = find_game(backend, mod_id)
    option = "replace_existing_files" if applied else "preserve_modified_ini"
    result = backend.call("set_mod_applied", mod_id, game_path, applied)
    if result["conflicting_files"] and force:
        result = backend.call("set_mod_applied", mod_id, game_path, applied, **{option: applied})
    print(json.dumps(result, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description="AnyAspectRatio desktop backend")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("scan", help="list installed games that have a mod")
    commands.add_parser("update-catalog", help="download the newest catalog")
    for name in ("apply", "remove"):
        command = commands.add_parser(name, help=f"{name} a mod's files")
        command.add_argument("mod_id")
        command.add_argument("--force", action="store_true", help="replace (apply) or delete (remove) changed files")
    args = parser.parse_args()

    sys.stdout.reconfigure(errors="replace")
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    backend = DesktopBackend()
    try:
        if args.command == "scan":
            print_games(backend.call("scan_supported_games"))
        elif args.command == "update-catalog":
            print(json.dumps(backend.call("update_dependencies"), indent=2))
        else:
            set_applied(backend, args.mod_id, args.command == "apply", args.force)
    finally:
        backend.stop()


if __name__ == "__main__":
    main()
