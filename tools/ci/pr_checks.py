"""PR checks: rights box, card link, allowlist, protected paths, blob scan.

CI runs this from a checkout of the base branch, so it imports scan/card
from its own directory regardless of the caller's cwd.
"""
import json
import os
import re
import stat
import subprocess
import sys
import urllib.request
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

import card
import scan

RIGHTS_BOX = "- [x] I have the right to submit this under the project's licences " \
             "and it contains no ROM data or copied code."
# GitHub closing keywords; group 1 is an optional owner/repo prefix, group 2 the number.
CLOSES_RE = re.compile(
    r"\b(?:close[sd]?|fix(?:e[sd])?|resolve[sd]?)\s+([\w.-]+/[\w.-]+)?#(\d+)",
    re.IGNORECASE)
CARD_LABELS = ("card:ready", "card:claimed")
CORE_ASSOCIATIONS = ("OWNER", "MEMBER", "COLLABORATOR")


def check_pr_body(body: str) -> list[str]:
    errors = []
    if RIGHTS_BOX not in body:
        errors.append("rights checkbox not ticked (exact text required)")
    closes = CLOSES_RE.findall(body)
    if len(closes) != 1:
        errors.append(f"expected exactly one issue-closing reference, found {len(closes)}")
    elif closes[0][0]:
        errors.append(f"closing reference must be #N in this repo, "
                      f"not {closes[0][0]}#{closes[0][1]}")
    return errors


def check_labels(labels: list[str]) -> list[str]:
    if not any(name in CARD_LABELS for name in labels):
        return ["linked issue is not labelled card:ready or card:claimed"]
    return []


def check_card_trust(issue: dict) -> list[str]:
    if issue.get("state") != "open" \
            or issue.get("author_association") not in CORE_ASSOCIATIONS:
        return ["card not authored by a core member"]
    return []


def check_paths(changes: list[tuple[str, str, str | None]], c: card.Card) -> list[str]:
    errors = []
    for status, path, old_path in changes:
        # R/C judge both paths: a rename is a delete of the old path plus an
        # add of the new one. Deny applies to every status.
        for p in [path] + ([old_path] if old_path else []):
            if card.is_protected(p):
                errors.append(f"protected: {p} needs a core member")
            if not any(card.glob_match(g, p) for g in c.allow):
                errors.append(f"outside allowlist: {p}")
            elif any(card.glob_match(g, p) for g in c.deny):
                errors.append(f"denied by card: {p}")
    return errors


def _git(pr_dir: str, *args: str) -> str:
    return subprocess.run(["git", "-C", pr_dir, *args],
                          check=True, capture_output=True, text=True).stdout


def changes_from_git(pr_dir: str, base_sha: str,
                     head_sha: str) -> list[tuple[str, str, str | None]]:
    """(status, path, old_path) per change; R/C carry the old path too."""
    out = _git(pr_dir, "diff", "-z", "--name-status", "-M", base_sha, head_sha)
    fields = out.split("\0")
    changes = []
    i = 0
    while i < len(fields) and fields[i]:
        status = fields[i]
        i += 1
        code = status[0]
        if code in "RC":  # R100/C100: similarity score ignored
            old_path, path = fields[i], fields[i + 1]
            i += 2
            changes.append((code, path, old_path))
        elif code in "AMD":
            changes.append((code, fields[i], None))
            i += 1
        else:
            path = fields[i] if i < len(fields) else ""
            raise ValueError(f"unsupported status {status}: {path}")
    return changes


def check_files(pr_dir: str,
                changes: list[tuple[str, str, str | None]]) -> tuple[list[str], list[str]]:
    """Every non-deleted path must exist and be a regular file (this also
    rejects symlinks and gitlinks, which lstat as links and directories).
    Returns (errors, scannable repo-relative paths)."""
    errors, scannable = [], []
    for status, path, _ in changes:
        if status == "D":
            continue
        try:
            st = os.lstat(os.path.join(pr_dir, path))
        except OSError:
            errors.append(f"missing from checkout: {path}")
            continue
        if not stat.S_ISREG(st.st_mode):
            errors.append(f"not a regular file: {path}")
            continue
        scannable.append(path)
    return errors, scannable


def _fetch(url: str) -> dict:
    req = urllib.request.Request(url, headers={
        "Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}",
        "Accept": "application/vnd.github+json",
    })
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.load(resp)


def main(event_path: str, pr_dir: str,
         fetch: Callable[[str], dict] | None = None) -> int:
    if fetch is None:
        fetch = _fetch
    event = json.loads(Path(event_path).read_text())
    pr = event["pull_request"]
    api = os.environ.get("GITHUB_API_URL", "https://api.github.com")
    repo = f"{api}/repos/{os.environ['GITHUB_REPOSITORY']}"

    errors = []
    head_sha = pr["head"]["sha"]
    checked_out = _git(pr_dir, "rev-parse", "HEAD").strip()
    if checked_out != head_sha:
        errors.append(f"checkout is at {checked_out}, not the event's head {head_sha}")

    live = fetch(f"{repo}/pulls/{pr['number']}")
    body = live.get("body") or ""
    errors += check_pr_body(body)
    if not errors:
        number = int(CLOSES_RE.search(body).group(2))
        issue = fetch(f"{repo}/issues/{number}")
        errors += check_card_trust(issue)
        labels = [l["name"] for l in issue.get("labels", [])]
        errors += check_labels(labels)
        c = card.parse_card(number, labels, issue.get("body") or "")

        try:
            changes = changes_from_git(pr_dir, pr["base"]["sha"], head_sha)
        except (ValueError, subprocess.CalledProcessError) as e:
            changes = []
            errors.append(str(e))
        errors += check_paths(changes, c)

        file_errors, scannable = check_files(pr_dir, changes)
        errors += file_errors
        for finding in scan.scan(Path(pr_dir), scannable):
            errors.append(f"{finding.path}: {finding.rule}: {finding.detail}")

    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    pr_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    raise SystemExit(main(os.environ["GITHUB_EVENT_PATH"], pr_dir))
