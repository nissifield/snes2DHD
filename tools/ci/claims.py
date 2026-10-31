"""Claim bot: /claim and /unclaim issue commands plus 7-day claim expiry.

Runs from the default branch on issue_comment and schedule events, so the
code a PR carries can never affect it. All network goes through one
injectable `request(method, url, data)` callable, like pr_checks.fetch.

Claimed-at is the time of the issue's most recent "assigned" event (from
the events API), never a value the commenter can forge.
"""
import json
import os
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Literal, NamedTuple

READY_LABEL = "card:ready"
CLAIM_LABEL = "card:claimed"
CARD_LABELS = ("card:draft", READY_LABEL, CLAIM_LABEL)
EXPIRE_DAYS = 7


class Action(NamedTuple):
    kind: Literal["assign", "unassign", "reply", "none"]
    message: str


def decide(comment: str, author: str, labels: list[str], assignees: list[str],
           now: datetime, claimed_at: datetime | None = None) -> Action:
    """Map a comment to an action. Only the first token is a command."""
    tokens = comment.split()
    if not tokens or tokens[0] not in ("/claim", "/unclaim"):
        return Action("none", "")
    if tokens[0] == "/claim":
        if CLAIM_LABEL in labels or assignees:
            who = f" by @{assignees[0]}" if assignees else ""
            when = f" on {claimed_at:%Y-%m-%d}" if claimed_at else ""
            return Action("reply", f"This card is already claimed{who}{when}.")
        if READY_LABEL not in labels:
            return Action("reply", "This issue is not a claimable card.")
        return Action("assign",
                      f"@{author} claimed this card on {now:%Y-%m-%d}. Without an "
                      f"open PR the claim expires after {EXPIRE_DAYS} days. "
                      f"Comment `/unclaim` to release it.")
    if author in assignees:
        return Action("unassign",
                      f"@{author} released this card; it is ready to claim again.")
    return Action("none", "")


def expire(issues: list[dict], now: datetime, days: int = EXPIRE_DAYS) -> list[int]:
    """Numbers of issues claimed longer than `days` with no linked open PR.

    Each dict: number, labels (list of names), claimed_at (ISO str or None),
    has_open_pr (bool)."""
    stale = []
    for issue in issues:
        if CLAIM_LABEL not in issue.get("labels", []):
            continue
        if issue.get("has_open_pr"):
            continue
        claimed = issue.get("claimed_at")
        if claimed is None:
            continue  # no assignment event found; leave for a human
        if now - datetime.fromisoformat(claimed) >= timedelta(days=days):
            stale.append(issue["number"])
    return stale


def _request(method: str, url: str, data: dict | None = None) -> object:
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method, headers={
        "Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}",
        "Accept": "application/vnd.github+json",
    })
    with urllib.request.urlopen(req, timeout=20) as resp:
        payload = resp.read()
    return json.loads(payload) if payload else {}


Request = Callable[[str, str, dict | None], object]


def _claimed_at(request: Request, events_url: str) -> str | None:
    """ISO time of the most recent assignment, from issue events."""
    events = request("GET", events_url + "?per_page=100", None)
    times = [e["created_at"] for e in events if e.get("event") == "assigned"]
    return max(times) if times else None


def _has_open_pr(request: Request, timeline_url: str) -> bool:
    """True when the timeline shows a cross-referenced PR that is still open."""
    events = request("GET", timeline_url + "?per_page=100", None)
    for e in events:
        if e.get("event") == "cross-referenced":
            src = e.get("source", {}).get("issue", {})
            if src.get("pull_request") and src.get("state") == "open":
                return True
    return False


def _swap_labels(request: Request, repo: str, number: int,
                 remove: str, add: str) -> None:
    request("DELETE", f"{repo}/issues/{number}/labels/{remove}", None)
    request("POST", f"{repo}/issues/{number}/labels", {"labels": [add]})


def main(event_path: str, request: Request | None = None) -> int:
    if request is None:
        request = _request
    event = json.loads(Path(event_path).read_text())
    api = os.environ.get("GITHUB_API_URL", "https://api.github.com")
    repo = f"{api}/repos/{os.environ['GITHUB_REPOSITORY']}"
    event_name = os.environ.get("GITHUB_EVENT_NAME", "")

    if event_name == "issue_comment":
        issue = event["issue"]
        if "pull_request" in issue:
            return 0  # commands on PRs are ignored
        comment = event["comment"]
        author = comment["user"]["login"]
        labels = [l["name"] for l in issue.get("labels", [])]
        assignees = [a["login"] for a in issue.get("assignees", [])]
        claimed = _claimed_at(request, issue["events_url"]) \
            if CLAIM_LABEL in labels else None
        claimed_dt = datetime.fromisoformat(claimed) if claimed else None
        action = decide(comment.get("body") or "", author, labels, assignees,
                        datetime.now(timezone.utc), claimed_dt)
        number = issue["number"]
        if action.kind == "assign":
            request("POST", f"{repo}/issues/{number}/assignees",
                    {"assignees": [author]})
            _swap_labels(request, repo, number, READY_LABEL, CLAIM_LABEL)
        elif action.kind == "unassign":
            request("DELETE", f"{repo}/issues/{number}/assignees",
                    {"assignees": [author]})
            _swap_labels(request, repo, number, CLAIM_LABEL, READY_LABEL)
        if action.message:
            request("POST", f"{repo}/issues/{number}/comments",
                    {"body": action.message})
    elif event_name == "schedule":
        found = request("GET", f"{repo}/issues?state=open"
                               f"&labels={CLAIM_LABEL}&per_page=100", None)
        rows = []
        for i in found:
            if "pull_request" in i:  # issues list includes PRs
                continue
            rows.append({
                "number": i["number"],
                "labels": [l["name"] for l in i.get("labels", [])],
                "assignees": [a["login"] for a in i.get("assignees", [])],
                "claimed_at": _claimed_at(request, i["events_url"]),
                "has_open_pr": _has_open_pr(request, i["timeline_url"]),
            })
        now = datetime.now(timezone.utc)
        for row in rows:
            if row["number"] not in expire([row], now):
                continue
            if row["assignees"]:
                request("DELETE", f"{repo}/issues/{row['number']}/assignees",
                        {"assignees": row["assignees"]})
            _swap_labels(request, repo, row["number"], CLAIM_LABEL, READY_LABEL)
            request("POST", f"{repo}/issues/{row['number']}/comments", {"body":
                    f"Claim expired after {EXPIRE_DAYS} days without an open "
                    f"PR; this card is ready to claim again."})
    return 0


if __name__ == "__main__":
    raise SystemExit(main(os.environ["GITHUB_EVENT_PATH"]))
