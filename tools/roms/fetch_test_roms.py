#!/usr/bin/env python3
"""Download pinned, open-licensed homebrew test ROMs."""
import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "tools" / "roms" / "test-roms.json"
DEFAULT_DEST = ROOT / "build" / "test-roms"
ALLOWED = {"0BSD", "BSD-2-Clause", "BSD-3-Clause", "CC0-1.0", "ISC", "MIT",
           "Unlicense", "Zlib"}
FILE_RE = re.compile(r"\A[A-Za-z0-9._-]+\.(sfc|smc)\Z")
SHA_RE = re.compile(r"\A[0-9a-f]{64}\Z")
MAX_DOWNLOAD = 8 * 1024 * 1024
TIMEOUT = 60


def load_manifest(path):
    """Validate every entry before any network access or write."""
    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ValueError(f"cannot read manifest {path}: {exc}") from exc
    if not isinstance(entries, list) or not entries:
        raise ValueError("manifest must be a non-empty list")
    seen = set()
    for index, entry in enumerate(entries, 1):
        if not isinstance(entry, dict):
            raise ValueError(f"entry {index}: not an object")
        file = entry.get("file")
        where = f"entry {index} ({file!r})"
        if not isinstance(file, str) or not FILE_RE.fullmatch(file):
            raise ValueError(f"{where}: file must be a plain .sfc/.smc name")
        if file in seen:
            raise ValueError(f"{where}: duplicate file")
        seen.add(file)
        url = entry.get("url")
        if not isinstance(url, str) or not url.startswith("https://"):
            raise ValueError(f"{where}: url must start with https://")
        if entry.get("license") not in ALLOWED:
            raise ValueError(f"{where}: license is not allowed")
        license_url = entry.get("license_url")
        if (not isinstance(license_url, str)
                or not license_url.startswith("https://")):
            raise ValueError(f"{where}: license_url must start with https://")
        sha256 = entry.get("sha256")
        if not isinstance(sha256, str) or not SHA_RE.fullmatch(sha256):
            raise ValueError(f"{where}: sha256 must be 64 lowercase hex")
        frames = entry.get("frames")
        if type(frames) is not int or not 1 <= frames <= 3600:
            raise ValueError(f"{where}: frames must be an int from 1 to 3600")
    return entries


def hash_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url, target, expected):
    with urllib.request.urlopen(url, timeout=TIMEOUT) as response:
        data = response.read(MAX_DOWNLOAD + 1)
    if len(data) > MAX_DOWNLOAD:
        raise ValueError(f"download exceeds {MAX_DOWNLOAD} bytes")
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("sha256 mismatch")
    handle, temporary = tempfile.mkstemp(dir=target.parent,
                                         prefix=".fetch-", suffix=".tmp")
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    args = parser.parse_args(argv)
    dest = args.dest.expanduser()
    if not dest.is_absolute():
        dest = ROOT / dest
    try:
        entries = load_manifest(DEFAULT_MANIFEST)
        dest.mkdir(parents=True, exist_ok=True)
        for entry in entries:
            file = entry["file"]
            target = dest / file
            if target.is_file() and hash_file(target) == entry["sha256"]:
                print(f"ok {file}")
                continue
            try:
                download(entry["url"], target, entry["sha256"])
            except (OSError, ValueError) as exc:
                raise ValueError(f"{file}: {exc}") from exc
            print(f"fetched {file}")
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
