"""Tests for tools/ci/claims.py. The request callable is stubbed; no network."""
import json
import os
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))

import claims

NOW = datetime(2026, 10, 31, 12, 0, tzinfo=timezone.utc)


def decide(comment, author="alice", labels=("card:ready",), assignees=(),
           claimed_at=None, other_claims=()):
    return claims.decide(comment, author, list(labels), list(assignees),
                         NOW, claimed_at, list(other_claims))


def stale_issue(days_ago, has_open_pr=False):
    return {"number": 5, "labels": ["card:claimed"],
            "claimed_at": (NOW - timedelta(days=days_ago)).isoformat(),
            "has_open_pr": has_open_pr}


def make_request(routes):
    """routes: list of ((method, url-prefix), value | callable(method, url, data))."""
    calls = []

    def request(method, url, data=None):
        calls.append((method, url, data))
        for (m, prefix), response in routes:
            if method == m and url.startswith(prefix):
                return response(method, url, data) if callable(response) else response
        raise AssertionError(f"unexpected request: {method} {url}")

    request.calls = calls
    return request


class DecideTests(unittest.TestCase):
    def test_claim_ready_unassigned(self):
        a = decide("/claim")
        self.assertEqual(a.kind, "assign")
        self.assertIn("@alice", a.message)

    def test_claim_already_claimed_replies(self):
        a = decide("/claim", labels=("card:claimed",), assignees=("bob",),
                   claimed_at=NOW - timedelta(days=1))
        self.assertEqual(a.kind, "reply")
        self.assertIn("@bob", a.message)

    def test_claim_not_a_card_no_reply(self):
        a = decide("/claim", labels=())
        self.assertEqual(a.kind, "none")

    def test_claim_on_draft_no_reply(self):
        a = decide("/claim", labels=("card:draft",))
        self.assertEqual(a.kind, "none")

    def test_claim_assigned_non_card_no_reply(self):
        # an assignee alone does not make an issue a card
        a = decide("/claim", labels=(), assignees=("bob",))
        self.assertEqual(a.kind, "none")

    def test_claim_second_open_claim_replies(self):
        a = decide("/claim", other_claims=(9,))
        self.assertEqual(a.kind, "reply")
        self.assertIn("#9", a.message)

    def test_unclaim_by_assignee(self):
        a = decide("/unclaim", author="bob", labels=("card:claimed",),
                   assignees=("bob",))
        self.assertEqual(a.kind, "unassign")

    def test_unclaim_by_other_ignored(self):
        a = decide("/unclaim", author="alice", labels=("card:claimed",),
                   assignees=("bob",))
        self.assertEqual(a.kind, "none")

    def test_unclaim_ready_card_noop(self):
        a = decide("/unclaim", author="bob", labels=("card:ready",),
                   assignees=("bob",))
        self.assertEqual(a.kind, "none")


class ExpireTests(unittest.TestCase):
    def test_expire_after_7_days(self):
        self.assertEqual(claims.expire([stale_issue(8)], NOW), [5])
        self.assertEqual(claims.expire([stale_issue(6)], NOW), [])

    def test_expire_skips_linked_pr(self):
        self.assertEqual(claims.expire([stale_issue(8, has_open_pr=True)], NOW), [])


class PaginateTests(unittest.TestCase):
    def test_paginate_follows_next_link(self):
        pages = {
            "u?per_page=100": claims.Page([1, 2], "u2?per_page=100"),
            "u2?per_page=100": claims.Page([3]),
        }
        out = claims._paginate(lambda m, url, d: pages[url], "u")
        self.assertEqual(out, [1, 2, 3])

    def test_paginate_plain_list_is_one_page(self):
        out = claims._paginate(lambda m, url, d: [1], "u")
        self.assertEqual(out, [1])


class HasOpenPrTests(unittest.TestCase):
    def test_counts_only_claimer_pr_with_closes(self):
        prs = [
            {"user": {"login": "mallory"}, "body": "Closes #8"},
            {"user": {"login": "bob"}, "body": "mentions #8 in passing"},
            {"user": {"login": "bob"}, "body": "Closes other/repo#8"},
        ]
        req = lambda m, u, d: prs
        self.assertFalse(claims._has_open_pr(req, "repo", 8, "bob"))
        prs.append({"user": {"login": "bob"}, "body": "Closes #8"})
        self.assertTrue(claims._has_open_pr(req, "repo", 8, "bob"))
        self.assertFalse(claims._has_open_pr(req, "repo", 9, "bob"))

    def test_no_claimer_is_false(self):
        self.assertFalse(claims._has_open_pr(lambda m, u, d: [], "repo", 8, ""))


ENV = {"GITHUB_API_URL": "https://api.test", "GITHUB_REPOSITORY": "o/r"}
REPO = "https://api.test/repos/o/r"


class MainFlowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def run_main(self, event, request, event_name):
        path = Path(self.tmp.name) / "event.json"
        path.write_text(json.dumps(event))
        env = dict(ENV, GITHUB_EVENT_NAME=event_name)
        with mock.patch.dict(os.environ, env):
            return claims.main(str(path), request)

    def test_main_claim_flow(self):
        routes = [
            (("GET", f"{REPO}/issues?state=open"), []),
            (("GET", f"{REPO}/issues/5"),
             {"state": "open", "number": 5, "assignees": [],
              "labels": [{"name": "card:ready"}]}),
            (("POST", f"{REPO}/issues/5/assignees"),
             {"assignees": [{"login": "alice"}]}),
            (("DELETE", f"{REPO}/issues/5/labels/card:ready"), {}),
            (("POST", f"{REPO}/issues/5/labels"), {}),
            (("POST", f"{REPO}/issues/5/comments"), {}),
        ]
        request = make_request(routes)
        event = {"issue": {"number": 5, "labels": []},
                 "comment": {"user": {"login": "alice"}, "body": "/claim"}}
        rc = self.run_main(event, request, "issue_comment")
        self.assertEqual(rc, 0)
        order = [u.split("/issues/5/")[-1].split("?")[0]
                 for m, u, _ in request.calls if "/issues/5/" in u]
        # assign first, label swap second, reply last
        self.assertEqual(order, ["assignees", "labels/card:ready", "labels",
                                 "comments"])
        comment = [d for m, u, d in request.calls if u.endswith("/comments")][0]
        self.assertIn("@alice claimed", comment["body"])

    def test_main_claim_assign_refused(self):
        routes = [
            (("GET", f"{REPO}/issues?state=open"), []),
            (("GET", f"{REPO}/issues/5"),
             {"state": "open", "number": 5, "assignees": [],
              "labels": [{"name": "card:ready"}]}),
            # GitHub silently refuses the assignee
            (("POST", f"{REPO}/issues/5/assignees"), {"assignees": []}),
            (("POST", f"{REPO}/issues/5/comments"), {}),
        ]
        request = make_request(routes)
        event = {"issue": {"number": 5},
                 "comment": {"user": {"login": "alice"}, "body": "/claim"}}
        rc = self.run_main(event, request, "issue_comment")
        self.assertEqual(rc, 0)
        self.assertFalse([c for c in request.calls if c[0] == "DELETE"])
        comment = [d for m, u, d in request.calls if u.endswith("/comments")][0]
        self.assertIn("would not assign", comment["body"])

    def test_main_refetches_issue_not_event_snapshot(self):
        # Event snapshot says ready; the live issue is already claimed by bob.
        routes = [
            (("GET", f"{REPO}/issues/5/events"),
             [{"event": "assigned", "created_at": NOW.isoformat()}]),
            (("GET", f"{REPO}/issues/5"),
             {"state": "open", "number": 5,
              "assignees": [{"login": "bob"}],
              "labels": [{"name": "card:claimed"}],
              "events_url": f"{REPO}/issues/5/events"}),
            (("POST", f"{REPO}/issues/5/comments"), {}),
        ]
        request = make_request(routes)
        event = {"issue": {"number": 5, "labels": [{"name": "card:ready"}],
                           "assignees": []},
                 "comment": {"user": {"login": "alice"}, "body": "/claim"}}
        rc = self.run_main(event, request, "issue_comment")
        self.assertEqual(rc, 0)
        self.assertFalse([c for c in request.calls
                          if c[0] == "POST" and c[1].endswith("/assignees")])
        comment = [d for m, u, d in request.calls if u.endswith("/comments")][0]
        self.assertIn("already claimed", comment["body"])

    def test_main_schedule_flow(self):
        stale_at = (datetime.now(timezone.utc) - timedelta(days=8)).isoformat()
        fresh_at = (datetime.now(timezone.utc) - timedelta(days=1)).isoformat()
        stale = {"number": 8, "labels": [{"name": "card:claimed"}],
                 "assignees": [{"login": "bob"}],
                 "events_url": f"{REPO}/issues/8/events"}
        fresh = {"number": 9, "labels": [{"name": "card:claimed"}],
                 "assignees": [{"login": "carol"}],
                 "events_url": f"{REPO}/issues/9/events"}
        page2 = f"{REPO}/issues?state=open&labels=card:claimed&page=2"

        def issues_page(method, url, data):
            if "page=2" in url:
                return [fresh]
            return claims.Page([stale], page2)

        routes = [
            (("GET", f"{REPO}/issues?state=open"), issues_page),
            (("GET", f"{REPO}/issues/8/events"),
             [{"event": "assigned", "created_at": stale_at}]),
            (("GET", f"{REPO}/issues/9/events"),
             [{"event": "assigned", "created_at": fresh_at}]),
            # bob's open PR closes #9, not #8: it must not keep #8 alive
            (("GET", f"{REPO}/pulls?state=open"),
             [{"user": {"login": "bob"}, "body": "Closes #9"}]),
            (("DELETE", f"{REPO}/issues/8/assignees"), {}),
            (("DELETE", f"{REPO}/issues/8/labels/card:claimed"), {}),
            (("POST", f"{REPO}/issues/8/labels"), {}),
            (("POST", f"{REPO}/issues/8/comments"), {}),
        ]
        request = make_request(routes)
        rc = self.run_main({}, request, "schedule")
        self.assertEqual(rc, 0)
        urls = [u for _, u, _ in request.calls]
        self.assertTrue(any("page=2" in u for u in urls))  # pagination followed
        self.assertIn(("DELETE", f"{REPO}/issues/8/assignees",
                       {"assignees": ["bob"]}), request.calls)
        self.assertFalse([c for c in request.calls if "/issues/9/" in c[1]
                          and c[0] != "GET"])
        comment = [d for m, u, d in request.calls if u.endswith("/comments")][0]
        self.assertIn("Claim expired", comment["body"])


if __name__ == "__main__":
    unittest.main()
