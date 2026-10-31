"""Task-card parsing and protected-path rules for snes2DHD CI."""
import fnmatch
import re
from typing import NamedTuple

# CODEOWNERS-gated paths; PRs touching these always need a core member.
PROTECTED = (
    ".github/",
    ".agents/",
    "CODEOWNERS",
    "LICENSE*",
    "LICENSES/",
    "third_party/",
    "external/",
    "patches/",
    "tools/ci/",
    "tools/review/",
    "tests/golden/",
    "profiles/schema/",
    "ROADMAP.md",
    "AGENTS.md",
)


class Card(NamedTuple):
    number: int
    labels: list[str]
    allow: list[str]
    deny: list[str]


def glob_match(pattern: str, path: str) -> bool:
    """fnmatch-style match where `*` stays within a directory and `**` crosses them."""
    pat = re.escape(pattern)
    pat = pat.replace(r"\*\*", ".*").replace(r"\*", "[^/]*").replace(r"\?", "[^/]")
    return re.fullmatch(pat, path) is not None


def is_protected(path: str) -> bool:
    for entry in PROTECTED:
        if entry.endswith("/"):
            if path == entry[:-1] or path.startswith(entry):
                return True
        elif fnmatch.fnmatchcase(path, entry):
            return True
    return False


def _section(body: str, title: str) -> list[str]:
    lines = []
    in_section = False
    for line in body.splitlines():
        stripped = line.strip()
        if stripped.startswith("###"):
            in_section = stripped == f"### {title}"
        elif in_section and stripped:
            lines.append(stripped)
    return lines


def parse_card(number: int, labels: list[str], body: str) -> Card:
    return Card(number, list(labels),
                _section(body, "Files you may touch"),
                _section(body, "Files you must not touch"))
