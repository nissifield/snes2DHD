"""Tests for tools/ci/pr_checks.py. main() runs against a fake event + stub fetch."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))

import card
import pr_checks

RIGHTS = "- [x] I have the right to submit this under the project's licences " \
         "and it contains no ROM data or copied code."
UNTICKED = "- [ ] I have the right to submit this under the project's licences " \
           "and it contains no ROM data or copied code."

CARD_BODY = "### Files you may touch\n\ndocs/**\n\n" \
            "### Files you must not touch\n\ndocs/private/**\n"


def make_card(allow=("docs/**",), deny=("docs/private/**",)):
    body = "### Files you may touch\n\n" + "\n".join(allow) + \
           "\n\n### Files you must not touch\n\n" + "\n".join(deny) + "\n"
    return card.parse_card(12, ["card:ready"], body)


class PrBodyTests(unittest.TestCase):
    def test_pr_body_ok(self):
        self.assertEqual(pr_checks.check_pr_body(f"{RIGHTS}\n\nCloses #12\n"), [])

    def test_pr_body_unticked(self):
        self.assertEqual(len(pr_checks.check_pr_body(f"{UNTICKED}\n\nCloses #12\n")), 1)

    def test_pr_body_no_closes(self):
        errors = pr_checks.check_pr_body(f"{RIGHTS}\n")
        self.assertTrue(any("Closes" in e for e in errors))

    def test_pr_body_two_closes(self):
        errors = pr_checks.check_pr_body(f"{RIGHTS}\n\nCloses #12\nCloses #13\n")
        self.assertTrue(any("Closes" in e for e in errors))

    def test_card_not_ready(self):
        self.assertEqual(pr_checks.check_labels(["card:ready"]), [])
        self.assertEqual(pr_checks.check_labels(["card:claimed"]), [])
        self.assertTrue(pr_checks.check_labels(["type:docs"]))


class PathTests(unittest.TestCase):
    def test_allow_glob_doublestar(self):
        c = make_card()
        self.assertEqual(pr_checks.check_paths([("A", "docs/a/b/c.md", None)], c), [])
        self.assertTrue(pr_checks.check_paths([("A", "src/x.py", None)], c))

    def test_path_with_space(self):
        c = make_card(allow=("docs/my dir/**",), deny=())
        self.assertEqual(
            pr_checks.check_paths([("A", "docs/my dir/file name.md", None)], c), [])

    def test_rename_into_allowlist_ok(self):
        c = make_card()
        self.assertEqual(pr_checks.check_paths([("R", "docs/new.md", "old/place.md")], c), [])

    def test_rename_out_of_denied(self):
        c = make_card()
        self.assertTrue(pr_checks.check_paths([("R", "notes.md", "docs/private/x.md")], c))

    def test_delete_outside_allowlist_fails(self):
        c = make_card()
        self.assertEqual(pr_checks.check_paths([("D", "docs/gone.md", None)], c), [])
        self.assertTrue(pr_checks.check_paths([("D", "src/gone.py", None)], c))

    def test_protected_path_flagged(self):
        c = make_card(allow=("**",), deny=())
        errors = pr_checks.check_paths([("M", "tools/ci/scan.py", None)], c)
        self.assertIn("protected: tools/ci/scan.py needs a core member", errors)


class MainTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def run_main(self, pr_body, labels=("card:ready",), files=None):
        if files is None:
            files = [{"filename": "docs/x.md", "status": "added"}]
        event = {
            "pull_request": {"number": 7, "body": pr_body},
            "repository": {"url": "https://api.test/repos/o/r"},
        }
        event_path = self.root / "event.json"
        event_path.write_text(json.dumps(event))
        payload = {
            "https://api.test/repos/o/r/issues/12": {
                "labels": [{"name": name} for name in labels],
                "body": CARD_BODY,
            },
            "https://api.test/repos/o/r/pulls/7/files": files,
        }
        return pr_checks.main(str(event_path), str(self.root), fetch=payload.get)

    def test_main_happy_path(self):
        (self.root / "docs").mkdir()
        (self.root / "docs" / "x.md").write_text("hello\n")
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 0)

    def test_main_bad_body_fails(self):
        self.assertEqual(self.run_main(f"{UNTICKED}\n\nCloses #12\n"), 1)

    def test_main_card_not_ready_fails(self):
        (self.root / "docs").mkdir()
        (self.root / "docs" / "x.md").write_text("hello\n")
        self.assertEqual(
            self.run_main(f"{RIGHTS}\n\nCloses #12\n", labels=("type:docs",)), 1)

    def test_main_disallowed_file_fails(self):
        (self.root / "src").mkdir()
        (self.root / "src" / "x.py").write_text("print(1)\n")
        files = [{"filename": "src/x.py", "status": "added"}]
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n", files=files), 1)


if __name__ == "__main__":
    unittest.main()
