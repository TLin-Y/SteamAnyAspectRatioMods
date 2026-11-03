"""Turn external/<author>/index.json (see fetch_external_mods.py) into catalog entries.

Steam app id comes from the Steam store search, install dir and executable from the public
SteamCMD app info. Entries that cannot be matched confidently go to unresolved.json instead
of catalog.json so they can be fixed by hand. Matched entries are also appended to the main
catalog and a thanks README listing them is written next to the zips.

Needs external/<author>/author.json: name, url, and support links (label + url).

Usage: python scripts/build_external_catalog.py [author]
"""

import difflib
import json
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from scanner.mod_catalog import load_mod_catalog  # noqa: E402

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MAIN_CATALOG = REPOSITORY_ROOT / "AnyAspectRatioDecky" / "catalog.json"
STORE_SEARCH = "https://store.steampowered.com/api/storesearch/?cc=us&l=en&term="
APP_INFO = "https://api.steamcmd.net/v1/info/"
MATCH_THRESHOLD = 0.85
EXTRACT_MARKER = "EXTRACT_TO_GAME_FOLDER"
LOADER_DLLS = {
    "winmm", "dinput8", "version", "dxgi", "d3d8", "d3d9", "d3d10", "d3d11", "d3d12",
    "ddraw", "dsound", "opengl32", "wininet", "winhttp", "xinput1_3", "xinput1_4",
    "xinput9_1_0", "msacm32", "msvfw32", "dbghelp", "wsock32", "iphlpapi",
}
PRIVATE_DIRECTORIES = ("dotnet/", "bepinex/")
EXCLUDED_EXECUTABLES = re.compile(r"art|soundtrack|launcher|config|setup|crash", re.IGNORECASE)
DESCRIPTION_GAME = re.compile(
    r"\bfor (?:the )?(.+?)(?: that| which| to |, | and | with |\.|$)", re.IGNORECASE
)


def get_json(url: str):
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.load(response)


def normalize(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", " ", text.lower().replace("_", " ")).strip()


def guess_game_name(entry: dict) -> str:
    match = DESCRIPTION_GAME.search(entry.get("description") or "")
    return match.group(1) if match else re.sub(r"(Fix|Tweak)$", "", entry["repo"])


def find_app(game: str) -> tuple[dict | None, list[str]]:
    items = get_json(STORE_SEARCH + urllib.parse.quote(game))["items"]
    wanted = normalize(game)
    wanted_tokens = set(wanted.split())

    def score(item: dict) -> float:
        name = normalize(item["name"])
        ratio = difflib.SequenceMatcher(None, wanted, name).ratio()
        return max(ratio, 0.9 if wanted_tokens <= set(name.split()) else 0.0)

    scored = sorted(((score(item), item) for item in items), key=lambda pair: -pair[0])
    candidates = [f"{item['id']} {item['name']} ({score:.2f})" for score, item in scored[:3]]
    if scored and scored[0][0] >= MATCH_THRESHOLD:
        return scored[0][1], candidates
    return None, candidates


def find_launches(app_id: int) -> tuple[str, list[str]]:
    config = get_json(APP_INFO + str(app_id))["data"][str(app_id)].get("config", {})
    executables: list[str] = []
    for launch in config.get("launch", {}).values():
        executable = launch.get("executable", "").replace("\\", "/")
        if (
            executable.lower().endswith(".exe")
            and launch.get("type", "none") in ("none", "default")
            and "windows" in launch.get("config", {}).get("oslist", "windows")
            and not EXCLUDED_EXECUTABLES.search(executable)
            and executable not in executables
        ):
            executables.append(executable)
    return config.get("installdir", ""), executables


def mod_directory(files: list[str]) -> str:
    """Directory (relative to the game root) the mod files are extracted into."""
    asi = next((path for path in files if path.lower().endswith(".asi")), None)
    return str(Path(asi).parent).replace("\\", "/").lower() if asi else ""


def build_entry(entry: dict, app: dict, install_dir: str, executable: str, credit: dict) -> dict:
    files = sorted(path for path in entry["files"] if path != EXTRACT_MARKER)
    inis = [path for path in files if path.lower().endswith(".ini")]
    ini_file = next(
        (path for path in inis if Path(path).stem.lower().startswith(entry["repo"].lower()[:4])),
        inis[0] if inis else None,
    )
    loaders = sorted(
        {
            Path(path).stem.lower()
            for path in files
            if Path(path).suffix.lower() == ".dll"
            and Path(path).stem.lower() in LOADER_DLLS
            and not path.lower().startswith(PRIVATE_DIRECTORIES)
        }
    )
    result = {
        "id": re.sub(r"[^a-z0-9]+", "-", entry["repo"].lower()).strip("-"),
        "name": f"{re.sub(r'[™®]', '', app['name'])} - {entry['repo']}",
        "steam_app_id": str(app["id"]),
        "install_dir": install_dir,
        "executable": executable,
        "source": {"kind": "archive", "path": entry["zip"]},
        "files": files,
        "wine_dll_overrides": [f"{','.join(loaders)}=n,b"] if loaders else [],
        "credits": [credit],
    }
    if ini_file:
        result["ini_file"] = ini_file
    return result


def render_mod(mod: dict) -> str:
    """Pretty-print one catalog entry at the main catalog's indentation (4 spaces)."""
    body = json.dumps(mod, indent=2, ensure_ascii=False)
    return "\n".join("    " + line for line in body.splitlines())


def merge_into_main_catalog(mods: list[dict], author: str) -> None:
    """Append this author's entries to the main catalog, replacing earlier generated ones.

    Generated entries are always last, so earlier text stays byte-identical.
    """
    text = MAIN_CATALOG.read_text(encoding="utf-8")
    first_generated = text.find(f'"path": "external/{author}/')
    if first_generated != -1:
        start = text.rfind("\n    {", 0, first_generated)
        text = text[:start].rstrip().rstrip(",") + "\n  ]\n}\n"
    closing = text.rstrip().rfind("\n  ]")
    rendered = ",\n".join(render_mod(mod) for mod in mods)
    MAIN_CATALOG.write_text(text[:closing] + ",\n" + rendered + text[closing:], encoding="utf-8")
    load_mod_catalog(MAIN_CATALOG)


def write_thanks(author_dir: Path, author: dict, mods: list[dict], index: list[dict]) -> None:
    by_zip = {entry["zip"]: entry for entry in index}
    support = " | ".join(f"[{link['label']}]({link['url']})" for link in author["support"])
    rows = []
    for mod in sorted(mods, key=lambda item: item["name"].lower()):
        entry = by_zip[mod["source"]["path"]]
        steam = f"[{mod['steam_app_id']}](https://store.steampowered.com/app/{mod['steam_app_id']})"
        rows.append(f"| {mod['name']} | {steam} | {entry['published']} | [{entry['repo']}]({entry['html_url']}) |")
    lines = [
        f"# Special thanks to [{author['name']}]({author['url']})",
        "",
        f"Everything in this folder is the work of {author['name']}, redistributed unchanged from "
        f"[{author['url'].replace('https://', '')}]({author['url']}) so the plugin can install it. "
        "If these fixes make your games better, please support him:",
        "",
        support,
        "",
        f"## Mods ({len(rows)})",
        "",
        "| Mod | Steam ID | Released | Source |",
        "|---|---|---|---|",
        *rows,
        "",
    ]
    (author_dir / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    sys.stdout.reconfigure(errors="replace")
    args = sys.argv[1:]
    author_name = args[0] if args else "Lyall"
    author_dir = REPOSITORY_ROOT / "external" / author_name
    author = json.loads((author_dir / "author.json").read_text(encoding="utf-8"))
    author_name = author["name"]
    index = json.loads((author_dir / "index.json").read_text(encoding="utf-8"))
    main_document = json.loads(MAIN_CATALOG.read_text(encoding="utf-8"))
    prefix = f"external/{author_name}/"
    others = [m for m in main_document["mods"] if not m["source"].get("path", "").startswith(prefix)]
    taken_apps = {mod["steam_app_id"] for mod in others}
    taken_ids = {mod["id"] for mod in others}

    credit = author
    mods: list[dict] = []
    unresolved: list[dict] = []
    for entry in index:
        game = guess_game_name(entry)
        reason = ""
        proposal = None
        app, candidates = find_app(game)
        if app is None:
            reason = "no confident Steam store match"
        elif str(app["id"]) in taken_apps or any(m["steam_app_id"] == str(app["id"]) for m in mods):
            reason = "Steam app already has a mod in the catalog"
        else:
            install_dir, executables = find_launches(app["id"])
            directory = mod_directory(entry["files"])
            matching = [
                path for path in executables if str(Path(path).parent).lower().replace("\\", "/") == directory
                or (directory == "." and "/" not in path)
            ]
            if not executables:
                reason = "no Windows executable in Steam launch info"
            elif not matching:
                reason = f"mod directory '{directory}' differs from every launch executable"
                proposal = build_entry(entry, app, install_dir, executables[0], credit)
            else:
                mod = build_entry(entry, app, install_dir, matching[0], credit)
                if mod["id"] in taken_ids:
                    mod["id"] = f"{author.lower()}-{mod['id']}"
                mods.append(mod)
                print(f"ok   {entry['repo']}: {app['name']} -> {install_dir} / {matching[0]}")
                continue
        unresolved.append(
            {"repo": entry["repo"], "guess": game, "reason": reason, "candidates": candidates, "proposal": proposal}
        )
        print(f"skip {entry['repo']}: {reason}")

    document = {
        "schema_version": main_document["schema_version"],
        "source_repository": main_document["source_repository"],
        "mods": mods,
    }
    (author_dir / "catalog.json").write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    (author_dir / "unresolved.json").write_text(json.dumps(unresolved, indent=2) + "\n", encoding="utf-8")
    load_mod_catalog(author_dir / "catalog.json")
    write_thanks(author_dir, author, mods, index)
    merge_into_main_catalog(mods, author_name)
    print(f"{len(mods)} entries merged into the main catalog, {len(unresolved)} unresolved")


if __name__ == "__main__":
    main()
