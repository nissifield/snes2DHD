#!/usr/bin/env python3
"""Run deterministic save-state and SRAM persistence tests on synthetic ROMs."""

import subprocess
import sys
import tempfile
from pathlib import Path

TIMEOUT = 120


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_save_state.py <test_save_state binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        min_rom = root / "min.sfc"
        joypad_rom = root / "joypad.sfc"
        for path, *args in ((min_rom,), (joypad_rom, "joypad")):
            subprocess.run(
                [sys.executable, Path(__file__).with_name("make_min_rom.py"), path, *args],
                check=True,
                timeout=TIMEOUT,
            )
        result = subprocess.run(
            [binary, str(min_rom), str(joypad_rom), str(root)],
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
        )

    if result.returncode != 0:
        print(f"save_state harness failed ({result.returncode}): {result.stderr}", file=sys.stderr)
        return 1
    if "save_state: all checks ok" not in result.stdout:
        print(f"unexpected save_state output: {result.stdout!r}", file=sys.stderr)
        return 1
    print(result.stdout.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
