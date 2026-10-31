"""PR checks: rights box, card link, allowlist, protected paths, blob scan.

CI runs this from a checkout of the base branch, so it imports scan/card
from its own directory regardless of the caller's cwd.
"""
import json
import os
import re
import sys
import urllib.request
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

import card
import scan

RIGHTS_BOX = "- [x] I have the right to submit this under the project's licences " \
             "and it contains no ROM data or copied code."
CLOSES_RE = re.compile(r"Closes #(\d+)")
CARD_LABELS = ("card:ready", "card:claimed")
STATUS_MAP = {"added": "A", "modified": "M", "removed": "D", "renamed": "R"}


def check_pr_body(body: str) -> list[str]:
    errors = []
    if RIGHTS_BOX not in body:
        errors.append("rights checkbox not ticked (exact text required)")
    closes = CLOSES_RE.findall(body)
    if len(closes) != 1:
        errors.append(f"expected exactly one 'Closes #N', found {len(closes)}")
    return errors


def check_labels(labels: list[str]) -> list[str]:
    if not any(name in CARD_LABELS for name in labels):
        return ["linked issue is not labelled card:ready or card:claimed"]
    return []


def check_paths(changes: list[tuple[str, str, str | None]], c: card.Card) -> list[str]:
    errors = []
    for status, path, old_path in changes:
        for p in (path, old_path):
            if p and card.is_protected(p):
                errors.append(f"protected: {p} needs a core member")
        # D: path is the deleted file; R: judge the new path; A/M: the path.
        if not any(card.glob_match(g, path) for g in c.allow):
            errors.append(f"outside allowlist: {path}")
        elif status != "D" and any(card.glob_match(g, path) for g in c.deny):
            errors.append(f"denied by card: {path}")
    return errors


def _fetch(url: str) -> dict:
    req = urllib.request.Request(url, headers={
        "Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}",
        "Accept": "application/vnd.github+json",
    })
    with urllib.request.urlopen(req) as resp:
        return json.load(resp)


def main(event_path: str, base_dir: str,
         fetch: Callable[[str], dict] | None = None) -> int:
    if fetch is None:
        fetch = _fetch
    event = json.loads(Path(event_path).read_text())
    pr = event["pull_request"]
    repo = event["repository"]["url"]

    errors = check_pr_body(pr.get("body") or "")
    if not errors:
        number = int(CLOSES_RE.search(pr["body"]).group(1))
        issue = fetch(f"{repo}/issues/{number}")
        labels = [l["name"] for l in issue.get("labels", [])]
        errors += check_labels(labels)
        c = card.parse_card(number, labels, issue.get("body") or "")

        files = fetch(f"{repo}/pulls/{pr['number']}/files")
        changes = [(STATUS_MAP[f["status"]], f["filename"],
                    f.get("previous_filename")) for f in files]
        errors += check_paths(changes, c)

        to_scan = [Path(base_dir) / f["filename"] for f in files
                   if f["status"] in ("added", "modified", "renamed")]
        for finding in scan.scan([p for p in to_scan if p.exists()]):
            errors.append(f"{finding.path}: {finding.rule}: {finding.detail}")

    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    base = sys.argv[1] if len(sys.argv) > 1 else "."
    raise SystemExit(main(os.environ["GITHUB_EVENT_PATH"], base))
