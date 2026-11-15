#!/usr/bin/env python3
"""Check downloaded homebrew test ROMs against a golden frame-hash file."""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "tools" / "roms" / "test-roms.json"
FILE_RE = re.compile(r"\A[A-Za-z0-9._-]+\.(sfc|smc)\Z")
SHA_RE = re.compile(r"\A[0-9a-f]{64}\Z")
FRAME_RE = re.compile(r"\Aframe (\d+) (\d+)x(\d+) sha256=([0-9a-f]{64})\Z")
TIMEOUT = 300


def load_manifest(path):
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
        if not isinstance(file, str) or not FILE_RE.fullmatch(file):
            raise ValueError(f"entry {index}: bad file name {file!r}")
        if file in seen:
            raise ValueError(f"entry {index}: duplicate file {file}")
        seen.add(file)
        frames = entry.get("frames")
        if type(frames) is not int or not 1 <= frames <= 3600:
            raise ValueError(f"entry {index}: bad frames {frames!r}")
    return entries


def load_golden(path):
    """Return None when the golden is missing, else file -> sha256."""
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    except OSError as exc:
        raise ValueError(f"cannot read golden {path}: {exc}") from exc
    golden = {}
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if (len(parts) != 2 or not FILE_RE.fullmatch(parts[0])
                or not SHA_RE.fullmatch(parts[1])):
            raise ValueError(f"malformed golden line {number}: {line!r}")
        golden[parts[0]] = parts[1]
    return golden


def frame_hash(binary, frames, rom):
    try:
        result = subprocess.run(
            [str(binary), "--frames", str(frames), str(rom)],
            capture_output=True, text=True, timeout=TIMEOUT)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return None, str(exc)
    if result.returncode != 0:
        return None, f"exit {result.returncode}: {result.stderr.strip()}"
    lines = result.stdout.splitlines()
    if len(lines) != 1:
        return None, f"unexpected output: {result.stdout!r}"
    match = FRAME_RE.fullmatch(lines[0])
    if not match or int(match[1]) != frames:
        return None, f"unexpected output: {lines[0]!r}"
    return match[4], None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headless", type=Path, required=True)
    parser.add_argument("--roms", type=Path, required=True)
    parser.add_argument("--golden", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args(argv)

    try:
        entries = load_manifest(args.manifest)
        golden = load_golden(args.golden)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    ok = 0
    failed = False
    computed = []
    for entry in entries:
        file = entry["file"]
        rom = args.roms / file
        if not rom.is_file():
            print(f"missing {file}: run tools/roms/fetch_test_roms.py first",
                  file=sys.stderr)
            return 1
        digest, error = frame_hash(args.headless, entry["frames"], rom)
        if error is not None:
            print(f"FAIL {file}: {error}", file=sys.stderr)
            failed = True
            continue
        computed.append((file, digest))
        if golden is not None and golden.get(file) == digest:
            print(f"ok {file}")
            ok += 1
        else:
            print(f"FAIL {file}")
            failed = True

    print(f"frame hashes: {ok}/{len(entries)} ok")
    if failed or golden is None:
        print("golden lines for this build:")
        for file, digest in computed:
            print(f"{file} {digest}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
