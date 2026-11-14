#!/usr/bin/env python3
"""Pack built binaries and the licence files into one release archive."""
import os
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
USAGE = "usage: package.py OUT.tar.gz|OUT.zip FILE..."
TOP = "snes2dhd"
# Bundled licence members own these top-level names; a binary may not collide.
RESERVED = {"LICENSE", "NOTICE", "licenses"}


def archive_members(files, root):
    """Return every (source, member) pair, checked before anything is written."""
    members = []
    seen = set()
    for path in files:
        name = path.name
        if not path.is_file():
            raise ValueError(f"missing input: {path}")
        if name in seen:
            raise ValueError(f"duplicate base name: {name}")
        if name in RESERVED:
            raise ValueError(f"base name clashes with a bundled licence: {name}")
        seen.add(name)
        members.append((path, f"{TOP}/{name}"))
    # Read the licence files the root actually has, so a licence change needs no
    # code change here and nothing old is baked in.
    for source, member in (
        (root / "LICENSE", f"{TOP}/LICENSE"),
        (root / "NOTICE", f"{TOP}/NOTICE"),
        (root / "external" / "ares" / "LICENSE", f"{TOP}/licenses/ares-LICENSE"),
    ):
        if not source.is_file():
            raise ValueError(f"missing licence: {source}")
        members.append((source, member))
    return members


def write_archive(out, members):
    """Write beside OUT, then rename, so a failed run leaves no release file."""
    out.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=out.parent,
                                         prefix=f".{out.name}.", suffix=".tmp")
    os.close(handle)
    try:
        if str(out).endswith(".zip"):
            with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as archive:
                for source, member in members:
                    archive.write(source, member)
        else:
            with tarfile.open(temporary, "w:gz", dereference=True) as archive:
                for source, member in members:
                    archive.add(source, member)
        # mkstemp creates 0600; a published archive needs normal read access.
        os.chmod(temporary, 0o644)
        os.replace(temporary, out)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def package(out: Path, files: list[Path], root: Path) -> int:
    if not str(out).endswith((".tar.gz", ".zip")):
        print(USAGE, file=sys.stderr)
        return 2
    try:
        members = archive_members(files, root)
        write_archive(out, members)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"wrote {out} ({len(members)} files)")
    return 0


def main(argv=None):
    args = sys.argv[1:] if argv is None else list(argv)
    if len(args) < 2:
        print(USAGE, file=sys.stderr)
        return 2
    return package(Path(args[0]), [Path(arg) for arg in args[1:]], ROOT)


if __name__ == "__main__":
    sys.exit(main())
