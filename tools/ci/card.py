"""Task-card parsing and protected-path rules for snes2DHD CI."""
import fnmatch
import re
from typing import NamedTuple

# CODEOWNERS-gated paths; PRs touching these always need a core member.
# Matched case-insensitively; any basename of CODEOWNERS is also protected.
PROTECTED = (
    ".gitattributes",
    ".github/",
    ".gitmodules",
    ".lfsconfig",
    ".agents/",
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


# Basenames protected in any directory: git applies nested .gitattributes /
# .lfsconfig / .gitmodules, and CODEOWNERS can live in .github/ or docs/.
PROTECTED_BASENAMES = frozenset({".gitattributes", ".lfsconfig", ".gitmodules",
                                 "codeowners"})


class Card(NamedTuple):
    number: int
    labels: list[str]
    allow: list[str]
    deny: list[str]


def glob_match(pattern: str, path: str) -> bool:
    """fnmatch-style match where `*` stays within a directory and `**` crosses them."""
    pat = re.escape(pattern)
    pat = pat.replace(r"\*\*/", r"(?:.*/)?")  # **/ also matches zero directories
    pat = pat.replace(r"\*\*", ".*").replace(r"\*", "[^/]*").replace(r"\?", "[^/]")
    return re.fullmatch(pat, path) is not None


def is_protected(path: str) -> bool:
    lower = path.lower()
    if lower.rsplit("/", 1)[-1] in PROTECTED_BASENAMES:
        return True
    for entry in PROTECTED:
        entry = entry.lower()
        if entry.endswith("/"):
            if lower == entry[:-1] or lower.startswith(entry):
                return True
        elif fnmatch.fnmatchcase(lower, entry):
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
            # GitHub issue forms render an empty optional field as "_No response_".
            if stripped != "_No response_":
                lines.append(stripped)
    return lines


def parse_card(number: int, labels: list[str], body: str) -> Card:
    return Card(number, list(labels),
                _section(body, "Files you may touch"),
                _section(body, "Files you must not touch"))
