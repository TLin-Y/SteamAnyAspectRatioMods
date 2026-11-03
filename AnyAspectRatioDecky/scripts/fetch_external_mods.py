"""Fetch the latest release zip of every repo of a Codeberg author into external/<author>/<repo>/.

Writes external/<author>/index.json describing each zip (version, files, size, description).
Existing zips are kept, so re-running only downloads new releases.

Usage: python scripts/fetch_external_mods.py [author]
"""

import json
import sys
import urllib.error
import urllib.request
from pathlib import Path
from zipfile import ZipFile

API = "https://codeberg.org/api/v1"
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
EXTERNAL_DIR = REPOSITORY_ROOT / "external"
PAGE_SIZE = 50


def get_json(url: str):
    with urllib.request.urlopen(url, timeout=60) as response:
        return json.load(response)


def list_repositories(author: str) -> list[dict]:
    repositories: list[dict] = []
    page = 1
    while True:
        batch = get_json(f"{API}/users/{author}/repos?limit={PAGE_SIZE}&page={page}")
        repositories += batch
        if len(batch) < PAGE_SIZE:
            return repositories
        page += 1


def latest_zip_asset(repository: dict, author: str) -> dict | None:
    if not repository["release_counter"]:
        return None
    releases = get_json(f"{API}/repos/{author}/{repository['name']}/releases?limit=1")
    for asset in releases[0]["assets"] if releases else []:
        if asset["name"].lower().endswith(".zip"):
            return {
                **asset,
                "tag": releases[0]["tag_name"],
                "published": releases[0]["published_at"][:10],
            }
    return None


def download(url: str, destination: Path, required: bool = True) -> bool:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".tmp")
    try:
        with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as target:
            target.write(response.read())
    except urllib.error.HTTPError:
        temporary.unlink(missing_ok=True)
        if required:
            raise
        return False
    temporary.replace(destination)
    return True


def main() -> None:
    author = sys.argv[1] if len(sys.argv) > 1 else "Lyall"
    author_dir = EXTERNAL_DIR / author
    index = []
    for repository in list_repositories(author):
        asset = latest_zip_asset(repository, author)
        if asset is None:
            print(f"skip {repository['name']}: no zip release")
            continue
        destination = author_dir / repository["name"] / asset["name"]
        if not destination.is_file():
            print(f"download {repository['name']} {asset['tag']}")
            download(asset["browser_download_url"], destination)
        readme = destination.parent / "README.md"
        if not readme.is_file():
            readme_url = f"{repository['html_url']}/raw/branch/{repository['default_branch']}/README.md"
            download(readme_url, readme, required=False)
        with ZipFile(destination) as archive:
            files = sorted(info.filename for info in archive.infolist() if not info.is_dir())
        index.append(
            {
                "repo": repository["name"],
                "description": repository["description"],
                "html_url": repository["html_url"],
                "tag": asset["tag"],
                "published": asset["published"],
                "zip": destination.relative_to(REPOSITORY_ROOT).as_posix(),
                "size": destination.stat().st_size,
                "files": files,
            }
        )
    author_dir.mkdir(parents=True, exist_ok=True)
    (author_dir / "index.json").write_text(json.dumps(index, indent=2), encoding="utf-8")
    print(f"{len(index)} mods indexed in {author_dir}")


if __name__ == "__main__":
    main()
