"""Tests for tools/ci/card.py."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))

import card

BODY = """Some intro text.

### Goal

Do the docs thing.

### Files you may touch

docs/**
README.md

### Files you must not touch

docs/private/**

### Done when

Tests pass.
"""


class CardTests(unittest.TestCase):
    def test_parse_card_sections(self):
        c = card.parse_card(12, ["card:ready"], BODY)
        self.assertEqual(c.number, 12)
        self.assertEqual(c.labels, ["card:ready"])
        self.assertEqual(c.allow, ["docs/**", "README.md"])
        self.assertEqual(c.deny, ["docs/private/**"])

    def test_glob_doublestar(self):
        self.assertTrue(card.glob_match("docs/**", "docs/a/b/c.md"))
        self.assertFalse(card.glob_match("docs/**", "src/a.md"))

    def test_glob_doublestar_zero_dirs(self):
        self.assertTrue(card.glob_match("src/**/x.py", "src/x.py"))
        self.assertTrue(card.glob_match("src/**/x.py", "src/a/b/x.py"))
        self.assertFalse(card.glob_match("src/**/x.py", "src/a/b/y.py"))

    def test_protected(self):
        self.assertTrue(card.is_protected("tools/ci/scan.py"))
        self.assertTrue(card.is_protected("LICENSE"))
        self.assertTrue(card.is_protected("LICENSES/CC-BY-SA-4.0.txt"))
        self.assertFalse(card.is_protected("docs/x.md"))

    def test_protected_case_insensitive(self):
        self.assertTrue(card.is_protected("license.md"))
        self.assertTrue(card.is_protected("Tools/CI/scan.py"))

    def test_protected_git_metadata(self):
        self.assertTrue(card.is_protected(".gitattributes"))
        self.assertTrue(card.is_protected(".gitmodules"))
        self.assertTrue(card.is_protected(".lfsconfig"))
        self.assertTrue(card.is_protected("docs/CODEOWNERS"))


if __name__ == "__main__":
    unittest.main()
