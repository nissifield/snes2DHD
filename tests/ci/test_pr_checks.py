"""Tests for tools/ci/pr_checks.py. main() runs against a real temp git repo + stub fetch."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))

import card
import pr_checks

RIGHTS = "- [x] I have the right to submit this under the project's licences " \
         "and it contains no ROM data or copied code."
UNTICKED = "- [ ] I have the right to submit this under the project's licences " \
           "and it contains no ROM data or copied code."

CARD_BODY = "### Files you may touch\n\ndocs/**\n\n" \
            "### Files you must not touch\n\ndocs/private/**\n"

ENV = {"GITHUB_API_URL": "https://api.test", "GITHUB_REPOSITORY": "o/r"}
PR_URL = "https://api.test/repos/o/r/pulls/7"
ISSUE_URL = "https://api.test/repos/o/r/issues/12"


def git(d, *args):
    return subprocess.run(["git", "-C", str(d), *args],
                          check=True, capture_output=True, text=True).stdout.strip()


def make_card(allow=("docs/**",), deny=("docs/private/**",)):
    body = "### Files you may touch\n\n" + "\n".join(allow) + \
           "\n\n### Files you must not touch\n\n" + "\n".join(deny) + "\n"
    return card.parse_card(12, ["card:ready"], body)


class PrBodyTests(unittest.TestCase):
    def test_pr_body_ok(self):
        self.assertEqual(pr_checks.check_pr_body(f"{RIGHTS}\n\nCloses #12\n"), [])

    def test_pr_body_ok_other_keywords(self):
        for kw in ("fixes #12", "Resolved #12", "CLOSE #12"):
            self.assertEqual(pr_checks.check_pr_body(f"{RIGHTS}\n\n{kw}\n"), [])

    def test_pr_body_unticked(self):
        self.assertEqual(len(pr_checks.check_pr_body(f"{UNTICKED}\n\nCloses #12\n")), 1)

    def test_pr_body_no_closes(self):
        errors = pr_checks.check_pr_body(f"{RIGHTS}\n")
        self.assertTrue(any("closing" in e for e in errors))

    def test_pr_body_two_closes(self):
        errors = pr_checks.check_pr_body(f"{RIGHTS}\n\nCloses #12\nand fixes #99\n")
        self.assertTrue(any("closing" in e for e in errors))

    def test_pr_body_cross_repo_close(self):
        errors = pr_checks.check_pr_body(f"{RIGHTS}\n\nCloses other/repo#12\n")
        self.assertTrue(any("this repo" in e for e in errors))

    def test_card_not_ready(self):
        self.assertEqual(pr_checks.check_labels(["card:ready"]), [])
        self.assertEqual(pr_checks.check_labels(["card:claimed"]), [])
        self.assertTrue(pr_checks.check_labels(["type:docs"]))

    def test_card_trust(self):
        good = {"state": "open", "author_association": "MEMBER"}
        self.assertEqual(pr_checks.check_card_trust(good), [])
        closed = {"state": "closed", "author_association": "OWNER"}
        outsider = {"state": "open", "author_association": "CONTRIBUTOR"}
        for bad in (closed, outsider):
            self.assertEqual(pr_checks.check_card_trust(bad),
                             ["card not authored by a core member"])


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
        self.assertEqual(
            pr_checks.check_paths([("R", "docs/new.md", "docs/old.md")], c), [])

    def test_rename_from_outside_allowlist_fails(self):
        c = make_card()
        self.assertTrue(
            pr_checks.check_paths([("R", "docs/new.md", "src/old.py")], c))

    def test_rename_out_of_denied(self):
        c = make_card()
        self.assertTrue(pr_checks.check_paths([("R", "notes.md", "docs/private/x.md")], c))

    def test_delete_outside_allowlist_fails(self):
        c = make_card()
        self.assertEqual(pr_checks.check_paths([("D", "docs/gone.md", None)], c), [])
        self.assertTrue(pr_checks.check_paths([("D", "src/gone.py", None)], c))

    def test_delete_denied_fails(self):
        c = make_card()
        errors = pr_checks.check_paths([("D", "docs/private/x.md", None)], c)
        self.assertTrue(any("denied" in e for e in errors))

    def test_protected_path_flagged(self):
        c = make_card(allow=("**",), deny=())
        errors = pr_checks.check_paths([("M", "tools/ci/scan.py", None)], c)
        self.assertIn("protected: tools/ci/scan.py needs a core member", errors)


class RepoTests(unittest.TestCase):
    """A real git repo in a temp dir: base commit in setUp, head via helpers."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name) / "pr"
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        git(self.repo, "config", "user.email", "test@example.com")
        git(self.repo, "config", "user.name", "test")
        self.write_files({"README.md": "hi\n"})
        self.base = self.commit("base")

    def write_files(self, files):
        for name, content in files.items():
            p = self.repo / name
            p.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(content, bytes):
                p.write_bytes(content)
            else:
                p.write_text(content)

    def commit(self, msg="c"):
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-qm", msg)
        return git(self.repo, "rev-parse", "HEAD")


class ChangesFromGitTests(RepoTests):
    def test_changes_from_git_statuses(self):
        self.write_files({"mod.md": "v1\n", "del.md": "bye\n", "ren.md": "same\n"})
        self.base = self.commit("base2")
        self.write_files({"mod.md": "v2\n", "new.md": "new\n"})
        (self.repo / "del.md").unlink()
        git(self.repo, "mv", "ren.md", "renamed.md")
        head = self.commit("head")
        changes = pr_checks.changes_from_git(str(self.repo), self.base, head)
        self.assertEqual(sorted(changes), sorted([
            ("M", "mod.md", None),
            ("D", "del.md", None),
            ("A", "new.md", None),
            ("R", "renamed.md", "ren.md"),
        ]))

    def test_changes_from_git_many_files(self):
        self.write_files({f"docs/f{i:02}.md": f"file {i}\n" for i in range(35)})
        head = self.commit("head")
        changes = pr_checks.changes_from_git(str(self.repo), self.base, head)
        self.assertEqual(len(changes), 35)
        self.assertTrue(all(s == "A" for s, _, _ in changes))

    def test_changes_from_git_type_change_unsupported(self):
        self.write_files({"link.md": "was a file\n"})
        self.base = self.commit("base2")
        (self.repo / "link.md").unlink()
        (self.repo / "link.md").symlink_to("README.md")
        head = self.commit("head")
        with self.assertRaisesRegex(ValueError, "unsupported status T: link.md"):
            pr_checks.changes_from_git(str(self.repo), self.base, head)


class MainTests(RepoTests):
    def run_main(self, body, labels=("card:ready",), state="open",
                 assoc="OWNER", fetched_body=None, head=None):
        event = {
            "pull_request": {"number": 7, "body": body,
                             "base": {"sha": self.base},
                             "head": {"sha": head or self.head}},
        }
        event_path = Path(self.tmp.name) / "event.json"
        event_path.write_text(json.dumps(event))
        payload = {
            PR_URL: {"body": body if fetched_body is None else fetched_body},
            ISSUE_URL: {"labels": [{"name": n} for n in labels],
                        "body": CARD_BODY, "state": state,
                        "author_association": assoc},
        }
        with mock.patch.dict(os.environ, ENV):
            return pr_checks.main(str(event_path), str(self.repo), fetch=payload.get)

    def add_docs_file(self):
        self.write_files({"docs/x.md": "hello\n"})
        self.head = self.commit("head")

    def test_main_happy_path(self):
        self.add_docs_file()
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 0)

    def test_main_bad_body_fails(self):
        self.add_docs_file()
        self.assertEqual(self.run_main(f"{UNTICKED}\n\nCloses #12\n"), 1)

    def test_main_card_not_ready_fails(self):
        self.add_docs_file()
        self.assertEqual(
            self.run_main(f"{RIGHTS}\n\nCloses #12\n", labels=("type:docs",)), 1)

    def test_main_card_closed_fails(self):
        self.add_docs_file()
        self.assertEqual(
            self.run_main(f"{RIGHTS}\n\nCloses #12\n", state="closed"), 1)

    def test_main_card_author_not_core_fails(self):
        self.add_docs_file()
        self.assertEqual(
            self.run_main(f"{RIGHTS}\n\nCloses #12\n", assoc="CONTRIBUTOR"), 1)

    def test_main_body_refetched(self):
        self.add_docs_file()
        good = f"{RIGHTS}\n\nCloses #12\n"
        bad = f"{UNTICKED}\n\nCloses #12\n"
        self.assertEqual(self.run_main(bad, fetched_body=good), 0)
        self.assertEqual(self.run_main(good, fetched_body=bad), 1)

    def test_main_disallowed_file_fails(self):
        self.write_files({"src/x.py": "print(1)\n"})
        self.head = self.commit("head")
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 1)

    def test_main_rom_file_fails(self):
        self.write_files({"docs/game.sfc": "not really\n"})
        self.head = self.commit("head")
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 1)

    def test_main_head_mismatch_fails(self):
        self.add_docs_file()
        self.assertEqual(
            self.run_main(f"{RIGHTS}\n\nCloses #12\n", head=self.base), 1)

    def test_main_missing_file_fails(self):
        self.add_docs_file()
        (self.repo / "docs" / "x.md").unlink()
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 1)

    def test_main_symlink_fails(self):
        (self.repo / "docs").mkdir()
        (self.repo / "docs" / "link.md").symlink_to("../README.md")
        self.head = self.commit("head")
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 1)

    def test_main_gitlink_fails(self):
        git(self.repo, "update-index", "--add", "--cacheinfo",
            f"160000,{self.base},vendor/lib")
        git(self.repo, "commit", "-qm", "head")  # no add -A: it would unstage the gitlink
        self.head = git(self.repo, "rev-parse", "HEAD")
        (self.repo / "vendor" / "lib").mkdir(parents=True)
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 1)

    def test_main_many_files_ok(self):
        self.write_files({f"docs/f{i:02}.md": f"file {i}\n" for i in range(35)})
        self.head = self.commit("head")
        self.assertEqual(self.run_main(f"{RIGHTS}\n\nCloses #12\n"), 0)


if __name__ == "__main__":
    unittest.main()
