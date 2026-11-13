"""Check catalog `install_dir` and `executable` against Steam's public app info.

`install_dir` must equal Steam's `installdir`; `executable` or one of `executable_alternatives` must
be a Steam Windows launch entry. Mods that correctly target another exe (Steam starts a launcher)
are listed in steam_catalog_exceptions.json with Steam's launch list at review time; the exception
lapses when that list changes.

Usage: python scripts/verify_catalog_steam.py
Exit codes: 0 verified, 1 mismatches or stale exceptions, 2 Steam unreachable.
"""

import json
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
CATALOG_PATH = SCRIPTS.parent / "catalog.json"
EXCEPTIONS_PATH = SCRIPTS / "steam_catalog_exceptions.json"
sys.path.insert(0, str(SCRIPTS))

import steam_pics  # noqa: E402


def normalize(path: str) -> str:
    return path.replace("\\", "/").strip().lstrip("/").casefold()


def windows_launches(appinfo: dict) -> list[str]:
    """Launch executables usable on Windows; entries without an oslist count."""
    out = []
    for entry in (appinfo.get("config", {}).get("launch") or {}).values():
        if not isinstance(entry, dict) or not entry.get("executable"):
            continue
        oslist = str((entry.get("config") or {}).get("oslist", "")).lower()
        executable = entry["executable"].replace("\\", "/").lstrip("/")
        if (not oslist or "windows" in oslist) and executable not in out:
            out.append(executable)
    return out


def executable_matches(mod: dict, launches: list[str]) -> bool:
    wanted = {normalize(p) for p in [mod.get("executable"), *mod.get("executable_alternatives", [])] if p}
    return bool(wanted & {normalize(p) for p in launches})


def check_mod(mod: dict, appinfo: dict | None, exceptions: dict) -> list[str]:
    if appinfo is None:
        return [f"Steam has no public app {mod['steam_app_id']}"]
    problems = []
    steam_dir = appinfo.get("config", {}).get("installdir")
    if steam_dir != mod["install_dir"]:
        problems.append(f"install_dir {mod['install_dir']!r} != Steam installdir {steam_dir!r}")
    launches = windows_launches(appinfo)
    executable = mod.get("executable")
    if executable and not executable_matches(mod, launches):
        exception = exceptions.get(mod["id"])
        if exception is None:
            problems.append(f"executable {executable!r} is not a Steam launch entry {launches}; "
                            "fix it, or add an exception if the mod targets a non-launch exe")
        elif sorted(exception["steam_launch"]) != sorted(launches):
            problems.append(f"Steam launch entries changed from {exception['steam_launch']} "
                            f"to {launches}; recheck executable {executable!r} and the exception")
    return problems


def verify(mods: list[dict], pics: dict[int, dict | None], exceptions: dict) -> tuple[dict, list[str]]:
    """Return ({mod id: problems}, stale exception notes)."""
    failures = {}
    notes = [f"exception for unknown mod id {mod_id!r}"
             for mod_id in exceptions.keys() - {mod["id"] for mod in mods}]
    for mod in mods:
        appinfo = pics.get(int(mod["steam_app_id"]))
        problems = check_mod(mod, appinfo, exceptions)
        if problems:
            failures[mod["id"]] = problems
        elif mod["id"] in exceptions and executable_matches(mod, windows_launches(appinfo)):
            notes.append(f"exception for {mod['id']!r} is no longer needed")
    return failures, notes


def load_exceptions() -> dict:
    return json.loads(EXCEPTIONS_PATH.read_text(encoding="utf-8"))


def main() -> int:
    mods = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))["mods"]
    exceptions = load_exceptions()
    try:
        pics = steam_pics.lookup(sorted({int(mod["steam_app_id"]) for mod in mods}))
    except Exception as error:
        print(f"Steam metadata check could not reach Steam: {type(error).__name__}: {error}", file=sys.stderr)
        return 2
    failures, notes = verify(mods, pics, exceptions)
    for mod_id, problems in failures.items():
        for problem in problems:
            print(f"FAIL {mod_id}: {problem}")
    for note in notes:
        print(f"NOTE {note}")
    print(f"Steam metadata check: {len(mods) - len(failures)}/{len(mods)} catalog entries match Steam "
          f"({len(exceptions)} exceptions)")
    return 1 if failures or notes else 0


if __name__ == "__main__":
    sys.exit(main())
