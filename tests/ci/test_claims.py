"""Tests for tools/ci/claims.py."""
import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))

import claims

NOW = datetime(2026, 10, 31, 12, 0, tzinfo=timezone.utc)


def decide(comment, author="alice", labels=("card:ready",), assignees=(),
           claimed_at=None):
    return claims.decide(comment, author, list(labels), list(assignees),
                         NOW, claimed_at)


def stale_issue(days_ago, has_open_pr=False):
    return {"number": 5, "labels": ["card:claimed"],
            "claimed_at": (NOW - timedelta(days=days_ago)).isoformat(),
            "has_open_pr": has_open_pr}


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

    def test_claim_not_a_card(self):
        a = decide("/claim", labels=())
        self.assertEqual(a.kind, "reply")
        self.assertNotEqual(a.message, "")

    def test_unclaim_by_assignee(self):
        a = decide("/unclaim", author="bob", labels=("card:claimed",),
                   assignees=("bob",))
        self.assertEqual(a.kind, "unassign")

    def test_unclaim_by_other_ignored(self):
        a = decide("/unclaim", author="alice", labels=("card:claimed",),
                   assignees=("bob",))
        self.assertEqual(a.kind, "none")


class ExpireTests(unittest.TestCase):
    def test_expire_after_7_days(self):
        self.assertEqual(claims.expire([stale_issue(8)], NOW), [5])
        self.assertEqual(claims.expire([stale_issue(6)], NOW), [])

    def test_expire_skips_linked_pr(self):
        self.assertEqual(claims.expire([stale_issue(8, has_open_pr=True)], NOW), [])


if __name__ == "__main__":
    unittest.main()
