"""Check that every author-download page in the catalog still shows its mod.

Nexus Mods answers 200 without an og:title for a missing mod, and refuses automated requests.

Usage: python scripts/verify_author_pages.py
Exit codes: 0 verified, 1 a page is gone, 2 a site was unreachable, 3 a site refused the check
(open the pages marked "blocked" by hand).
"""

import json
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

CATALOG_PATH = Path(__file__).resolve().parents[1] / "catalog.json"
USER_AGENT = "Mozilla/5.0 AnyAspectRatio-catalog-check"
ATTEMPTS = 3
RETRY_DELAY_SECONDS = 3
MAX_PAGE_BYTES = 4 * 1024 * 1024
OG_TITLE = re.compile(rb'<meta[^>]+property="og:title"[^>]+content="([^"]+)"', re.IGNORECASE)

MISSING, UNREACHABLE, BLOCKED = "missing", "unreachable", "blocked"
REFUSED_STATUSES = (403, 429, 503)


def fetch(url: str) -> tuple[int, bytes]:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return response.status, response.read(MAX_PAGE_BYTES)
    except urllib.error.HTTPError as error:
        return error.code, b""


def check_page(url: str, fetcher=fetch, sleep=time.sleep) -> tuple[str, str]:
    """("ok", title), (MISSING | BLOCKED | UNREACHABLE, reason); refusals and failures are retried."""
    reason, refused = "", False
    for attempt in range(ATTEMPTS):
        if attempt:
            sleep(RETRY_DELAY_SECONDS * attempt)
        try:
            status, body = fetcher(url)
        except OSError as error:
            reason = f"{type(error).__name__}: {error}"
            continue
        if status in (404, 410):
            return MISSING, f"HTTP {status}"
        if status != 200:
            reason, refused = f"HTTP {status}", status in REFUSED_STATUSES
            continue
        title = OG_TITLE.search(body)
        if title is None:
            return MISSING, "the page no longer shows a mod (no og:title)"
        return "ok", title.group(1).decode("utf-8", "replace")
    if refused:
        return BLOCKED, f"{reason}: the site refused an automated check, open the page by hand"
    return UNREACHABLE, reason


def main(fetcher=fetch, sleep=time.sleep) -> int:
    entries = json.loads(CATALOG_PATH.read_text(encoding="utf-8")).get("author_downloads", [])
    counts = {MISSING: 0, UNREACHABLE: 0, BLOCKED: 0}
    for entry in entries:
        page = entry["source"]["page"]
        result, detail = check_page(page, fetcher, sleep)
        print(f"{result:11} {entry['id']}: {detail} ({page})")
        if result in counts:
            counts[result] += 1
    print(f"Author pages: {len(entries) - sum(counts.values())}/{len(entries)} verified")
    return 1 if counts[MISSING] else 2 if counts[UNREACHABLE] else 3 if counts[BLOCKED] else 0


if __name__ == "__main__":
    sys.exit(main())
