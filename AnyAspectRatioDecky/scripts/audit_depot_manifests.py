"""Fetch Steam depot manifests and compare their executable paths with the catalog."""

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PLUGIN_ROOT.parent
CATALOG_PATH = PLUGIN_ROOT / "catalog.json"
DOWNLOAD_ROOT = REPOSITORY_ROOT / "downloaded"
DOWNLOADER = REPOSITORY_ROOT / "DepotDownloader.exe"
ROW = re.compile(r"^\s*\d+\s+\d+\s+[0-9a-fA-F]{40}\s+[0-9a-fA-F]+\s+(.+)$")
MANIFEST_NAME = re.compile(r"^manifest_\d+_\d+\.txt$")


def manifest_executables(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    if not lines or not lines[0].startswith("Content Manifest for Depot "):
        raise ValueError(f"Unrecognized manifest: {path}")
    count_line = next((line for line in lines if line.startswith("Total number of files")), "")
    count_match = re.fullmatch(r"Total number of files\s*:\s*(\d+)\s*", count_line)
    if not count_match:
        raise ValueError(f"Missing manifest file count: {path}")
    try:
        header = next(i for i, line in enumerate(lines) if line.strip().endswith("Flags Name"))
    except StopIteration as exc:
        raise ValueError(f"Missing manifest file table: {path}") from exc
    result = []
    count = 0
    for line in lines[header + 1:]:
        if not line.strip():
            continue
        match = ROW.match(line)
        if not match:
            raise ValueError(f"Unrecognized manifest row in {path}: {line}")
        count += 1
        name = match.group(1).replace("\\", "/")
        if name.lower().endswith(".exe"):
            result.append(name)
    if count != int(count_match.group(1)):
        raise ValueError(f"Incomplete manifest file table: {path}")
    return result


def fetch_app(app_id: str, username: str | None) -> dict:
    app_dir = DOWNLOAD_ROOT / "manifests" / app_id
    app_dir.mkdir(parents=True, exist_ok=True)
    run_dir = app_dir / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    run_dir.mkdir()
    command = [str(DOWNLOADER), "-app", app_id, "-os", "windows", "-manifest-only",
               "-dir", str(run_dir)]
    if username:
        command.extend(["-username", username])
    print(f"\nFetching app {app_id}...", flush=True)
    process = subprocess.Popen(
        command, cwd=DOWNLOAD_ROOT, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", bufsize=1,
    )
    assert process.stdout is not None
    output = []
    while True:
        char = process.stdout.read(1)
        if not char:
            break
        sys.stdout.write(char)
        sys.stdout.flush()
        output.append(char)
    returncode = process.wait()
    transcript = "".join(output)
    manifests = sorted(path.name for path in run_dir.iterdir() if MANIFEST_NAME.match(path.name))
    unavailable = [
        line.strip() for line in transcript.splitlines()
        if re.search(r"(?:depot|app) \d+ .*not available|unable to download manifest|"
                     r"error downloading (?:depot )?manifest", line, re.IGNORECASE)
    ]
    result = {
        "app_id": app_id,
        "run": run_dir.name,
        "returncode": returncode,
        "manifests": manifests,
        "unavailable_depots": unavailable,
        "status": "unavailable" if returncode != 0 or not manifests else
                  "partial" if unavailable else "complete",
    }
    (app_dir / "latest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def audit_mod(mod: dict, result: dict | None, executables: list[str]) -> dict:
    executable = mod.get("executable", "").replace("\\", "/")
    by_casefold = {path.casefold(): path for path in executables}
    found = by_casefold.get(executable.casefold()) if executable else None
    status = result["status"] if result else "unavailable"
    if not executable:
        match = "not specified"
    elif found:
        match = "present" if found == executable else "case mismatch"
    elif status == "complete":
        match = "not found in accessible manifests"
    else:
        match = "unknown (missing or incomplete manifests)"
    asi_paths = [path for path in mod["files"] if path.lower().endswith(".asi")]
    return {
        "id": mod["id"],
        "game": mod["name"],
        "steam_app_id": mod["steam_app_id"],
        "install_dir": mod["install_dir"],
        "install_dir_status": "unverified by manifest",
        "manifest_status": status,
        "unavailable_depots": result["unavailable_depots"] if result else [],
        "catalog_executable": executable,
        "executable_status": match,
        "manifest_executables": sorted(executables, key=str.casefold),
        "source": mod["source"],
        "asi_targets": asi_paths,
        "asi_target_note": "Relative to install_dir; review against mod instructions.",
    }


def load_app(app_id: str) -> tuple[dict | None, list[str]]:
    app_dir = DOWNLOAD_ROOT / "manifests" / app_id
    latest = app_dir / "latest.json"
    if not latest.is_file():
        return None, []
    result = json.loads(latest.read_text(encoding="utf-8"))
    run_dir = app_dir / result["run"]
    executables = set()
    for name in result["manifests"]:
        executables.update(manifest_executables(run_dir / name))
    return result, sorted(executables, key=str.casefold)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true", help="Download current public Windows manifests.")
    parser.add_argument("--app", action="append", default=[], metavar="ID",
                        help="Limit to a catalog Steam app ID; repeat for multiple games.")
    parser.add_argument("--username", help="Steam username for interactive login; never pass a password.")
    args = parser.parse_args()
    if args.username and not args.fetch:
        parser.error("--username requires --fetch")
    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    mods = catalog["mods"]
    known_apps = {str(mod["steam_app_id"]) for mod in mods}
    unknown = set(args.app) - known_apps
    if unknown:
        parser.error("App IDs not in catalog: " + ", ".join(sorted(unknown)))
    selected = set(args.app) if args.app else known_apps
    fetch_results = []
    if args.fetch:
        if not DOWNLOADER.is_file():
            parser.error(f"DepotDownloader not found: {DOWNLOADER}")
        DOWNLOAD_ROOT.mkdir(parents=True, exist_ok=True)
        for app_id in sorted(selected, key=int):
            fetch_results.append(fetch_app(app_id, args.username))
    cache = {app_id: load_app(app_id) for app_id in selected}
    report = [audit_mod(mod, *cache[str(mod["steam_app_id"])])
              for mod in mods if str(mod["steam_app_id"]) in selected]
    DOWNLOAD_ROOT.mkdir(parents=True, exist_ok=True)
    report_path = DOWNLOAD_ROOT / "catalog-audit.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for entry in report:
        print(f'{entry["game"]}: manifest={entry["manifest_status"]}; '
              f'executable={entry["executable_status"]}; ASI={", ".join(entry["asi_targets"]) or "(none)"}')
    print(f"\nFull report: {report_path}")
    print("Manifest results do not verify install_dir or which executable loads the mod.")
    return 1 if any(result["status"] != "complete" for result in fetch_results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
