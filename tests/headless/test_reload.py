#!/usr/bin/env python3
"""Reload two generated ROMs in one process and require stable frame hashes."""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

LINE = re.compile(r"reload (\d+) loaded=(\d+) ran=(\d+) unloaded=(\d+) sha256=([0-9a-f]{64})")
TIMEOUT = 120  # a hung emulator must fail instead of blocking CI.


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_reload.py <reload binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()

    with tempfile.TemporaryDirectory() as directory:
        rom = Path(directory) / "min.sfc"
        ram_rom = Path(directory) / "ram.sfc"
        for path, *args in ((rom,), (ram_rom, "ram-sensitive")):
            subprocess.run(
                [sys.executable, Path(__file__).with_name("make_min_rom.py"), path, *args],
                check=True,
                timeout=TIMEOUT,
            )
        result = subprocess.run(
            [binary, str(rom), str(ram_rom)],
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
        )

    if result.returncode != 0:
        print(f"reload harness failed ({result.returncode}): {result.stderr}", file=sys.stderr)
        return 1

    hashes: dict[int, set[str]] = {0: set(), 1: set()}
    lines = result.stdout.splitlines()
    if len(lines) != 6:
        print(f"expected 6 reload lines, got {len(lines)}: {result.stdout!r}", file=sys.stderr)
        return 1
    for line in lines:
        match = LINE.fullmatch(line)
        if not match:
            print(f"unexpected output: {line!r}", file=sys.stderr)
            return 1
        round_index = int(match[1])
        loaded, ran, unloaded = int(match[2]), int(match[3]), int(match[4])
        if loaded == 0 or ran != 0 or unloaded != 0:
            print(f"unexpected entry counts on reload {round_index}: {line!r}", file=sys.stderr)
            return 1
        hashes[round_index % 2].add(match[5])

    for rom_index, digests in hashes.items():
        if len(digests) != 1:
            print(f"ROM {rom_index} frame hashes differ: {sorted(digests)!r}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
