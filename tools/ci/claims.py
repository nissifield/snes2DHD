"""Claim bot: /claim and /unclaim issue commands plus 7-day claim expiry.

Runs from the default branch on issue_comment and schedule events, so the
code a PR carries can never affect it. All network goes through one
injectable `request(method, url, data)` callable, like pr_checks.fetch.

Claimed-at is the time of the issue's most recent "assigned" event (from
the events API), never a value the commenter can forge. Decisions use a
freshly fetched issue, never the event's snapshot.
"""
import json
import os
import sys
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Literal, NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pr_checks

READY_LABEL = "card:ready"
CLAIM_LABEL = "card:claimed"
CARD_LABELS = ("card:draft", READY_LABEL, CLAIM_LABEL)
EXPIRE_DAYS = 7


class Action(NamedTuple):
    kind: Literal["assign", "unassign", "reply", "none"]
    message: str


def decide(comment: str, author: str, labels: list[str], assignees: list[str],
           now: datetime, claimed_at: datetime | None = None,
           other_claims: list[int] = ()) -> Action:
    """Map a comment to an action. Only the first token is a command.

    Replies only when something changes, plus one "already claimed" reply
    per /claim on a real card; non-card issues get silence."""
    tokens = comment.split()
    if not tokens or tokens[0] not in ("/claim", "/unclaim"):
        return Action("none", "")
    if tokens[0] == "/claim":
        if CLAIM_LABEL in labels or assignees:
            if not any(l in CARD_LABELS for l in labels):
                return Action("none", "")
            who = f" by @{assignees[0]}" if assignees else ""
            when = f" on {claimed_at:%Y-%m-%d}" if claimed_at else ""
            return Action("reply", f"This card is already claimed{who}{when}.")
        if READY_LABEL not in labels:
            return Action("none", "")  # draft or non-card: nothing to say
        if other_claims:
            return Action("reply",
                          f"You already have an open claim on "
                          f"#{other_claims[0]}; finish or `/unclaim` it first.")
        return Action("assign",
                      f"@{author} claimed this card on {now:%Y-%m-%d}. Without an "
                      f"open PR the claim expires after {EXPIRE_DAYS} days. "
                      f"Comment `/unclaim` to release it.")
    if CLAIM_LABEL in labels and author in assignees:
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


class Page(list):
    """One page of a list endpoint; next_url is the Link rel="next" target."""
    def __init__(self, items, next_url=None):
        super().__init__(items)
        self.next_url = next_url


def _next_link(header: str) -> str | None:
    for part in header.split(","):
        url, _, rel = part.partition(";")
        if rel.strip() == 'rel="next"':
            return url.strip().strip("<>")
    return None


def _request(method: str, url: str, data: dict | None = None) -> object:
    body = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(url, data=body, method=method, headers={
        "Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}",
        "Accept": "application/vnd.github+json",
    })
    with urllib.request.urlopen(req, timeout=20) as resp:
        payload = resp.read()
        link = resp.headers.get("Link", "")
    parsed = json.loads(payload) if payload else {}
    if isinstance(parsed, list):
        return Page(parsed, _next_link(link))
    return parsed


Request = Callable[[str, str, dict | None], object]


def _paginate(request: Request, url: str) -> list:
    """All items of a list endpoint, following Link rel="next"."""
    items = []
    url = url + ("&" if "?" in url else "?") + "per_page=100"
    while url:
        page = request("GET", url, None)
        items.extend(page)
        url = getattr(page, "next_url", None)
    return items


def _claimed_at(request: Request, events_url: str) -> str | None:
    """ISO time of the most recent assignment, from issue events."""
    events = _paginate(request, events_url)
    times = [e["created_at"] for e in events if e.get("event") == "assigned"]
    return max(times) if times else None


def _has_open_pr(request: Request, repo: str, number: int, claimer: str) -> bool:
    """True when the claimer has an open PR whose body closes this issue.
    Anyone else's PR, or a bare mention, does not keep a claim alive."""
    if not claimer:
        return False
    for pr in _paginate(request, f"{repo}/pulls?state=open"):
        if pr.get("user", {}).get("login") != claimer:
            continue
        for m in pr_checks.CLOSES_RE.finditer(pr.get("body") or ""):
            if not (m.group("prefix") or m.group("url")) \
                    and m.group("num") == str(number):
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
        if "pull_request" in event["issue"]:
            return 0  # commands on PRs are ignored
        number = event["issue"]["number"]
        live = request("GET", f"{repo}/issues/{number}", None)
        if live.get("state") != "open" or "pull_request" in live:
            return 0
        comment = event["comment"]
        author = comment["user"]["login"]
        labels = [l["name"] for l in live.get("labels", [])]
        assignees = [a["login"] for a in live.get("assignees", [])]
        body = comment.get("body") or ""
        other = []
        if body.split()[:1] == ["/claim"] and READY_LABEL in labels:
            mine = _paginate(request, f"{repo}/issues?state=open"
                                      f"&labels={CLAIM_LABEL}&assignee={author}")
            other = [i["number"] for i in mine
                     if i["number"] != number and "pull_request" not in i]
        claimed = _claimed_at(request, live["events_url"]) \
            if CLAIM_LABEL in labels else None
        claimed_dt = datetime.fromisoformat(claimed) if claimed else None
        action = decide(body, author, labels, assignees,
                        datetime.now(timezone.utc), claimed_dt, other)
        if action.kind == "assign":
            resp = request("POST", f"{repo}/issues/{number}/assignees",
                           {"assignees": [author]})
            if author not in [a.get("login") for a in resp.get("assignees", [])]:
                # GitHub silently refuses some assignees; a claimed label with
                # nobody assigned would never expire, so don't swap labels.
                action = Action("reply",
                                "GitHub would not assign you — comment on the "
                                "issue first / check your account")
            else:
                _swap_labels(request, repo, number, READY_LABEL, CLAIM_LABEL)
        elif action.kind == "unassign":
            request("DELETE", f"{repo}/issues/{number}/assignees",
                    {"assignees": [author]})
            _swap_labels(request, repo, number, CLAIM_LABEL, READY_LABEL)
        if action.message:
            request("POST", f"{repo}/issues/{number}/comments",
                    {"body": action.message})
    elif event_name == "schedule":
        found = _paginate(request, f"{repo}/issues?state=open"
                                   f"&labels={CLAIM_LABEL}")
        rows = []
        for i in found:
            if "pull_request" in i:  # issues list includes PRs
                continue
            assignees = [a["login"] for a in i.get("assignees", [])]
            rows.append({
                "number": i["number"],
                "labels": [l["name"] for l in i.get("labels", [])],
                "assignees": assignees,
                "claimed_at": _claimed_at(request, i["events_url"]),
                "has_open_pr": _has_open_pr(request, repo, i["number"],
                                            assignees[0] if assignees else ""),
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
