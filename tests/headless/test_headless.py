#!/usr/bin/env python3
"""Run the headless runner on the synthetic test ROM and check its frame hash."""

import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

FRAME = re.compile(r"frame 60 (\d+)x(\d+) sha256=([0-9a-f]{64})")
TIMEOUT = 120  # a hung emulator must fail instead of blocking CI.


def run(binary: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [binary, *args],
        capture_output=True,
        text=True,
        timeout=TIMEOUT,
    )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_headless.py <headless binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()

    with tempfile.TemporaryDirectory() as directory:
        rom = Path(directory) / "min.sfc"
        subprocess.run(
            [sys.executable, Path(__file__).with_name("make_min_rom.py"), rom],
            check=True,
            timeout=TIMEOUT,
        )
        ram_rom = Path(directory) / "ram.sfc"
        subprocess.run(
            [sys.executable, Path(__file__).with_name("make_min_rom.py"), ram_rom, "ram-sensitive"],
            check=True,
            timeout=TIMEOUT,
        )

        first = run(binary, "--frames", "60", str(rom))
        second = run(binary, "--frames", "60", str(rom))
        missing = run(binary, "--frames", "60", str(Path(directory) / "missing.sfc"))
        # The ROM draws work RAM bytes in colour 0, so clock-seeded power-on noise
        # would make these separate processes disagree.
        ram_runs = [run(binary, "--frames", "60", str(ram_rom)) for _ in range(6)]

    if first.returncode != 0:
        print(f"first run failed ({first.returncode}): {first.stderr}", file=sys.stderr)
        return 1
    if second.returncode != 0:
        print(f"second run failed ({second.returncode}): {second.stderr}", file=sys.stderr)
        return 1

    match = FRAME.fullmatch(first.stdout.strip())
    if not match:
        print(f"unexpected output: {first.stdout!r}", file=sys.stderr)
        return 1
    if first.stdout != second.stdout:
        print(f"runs differ: {first.stdout!r} != {second.stdout!r}", file=sys.stderr)
        return 1

    width, height, digest = int(match[1]), int(match[2]), match[3]
    black = hashlib.sha256(bytes(width * height * 4)).hexdigest()
    if digest == black:
        print("frame is entirely black", file=sys.stderr)
        return 1

    if missing.returncode != 1:
        print(f"missing ROM exit code was {missing.returncode}, expected 1", file=sys.stderr)
        return 1

    for index, result in enumerate(ram_runs):
        if result.returncode != 0:
            print(f"ram-sensitive run {index} failed ({result.returncode}): {result.stderr}", file=sys.stderr)
            return 1
        line = result.stdout
        if not line.endswith("\n") or "\n" in line[:-1] or not FRAME.fullmatch(line[:-1]):
            print(f"ram-sensitive run {index} returned unexpected output: {line!r}", file=sys.stderr)
            return 1
    outputs = {result.stdout for result in ram_runs}
    if len(outputs) != 1:
        print(f"ram-sensitive runs differ: {sorted(outputs)!r}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
