#!/usr/bin/env python3
"""Drive test_input with a synthetic joypad ROM and constant-colour references."""

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_min_rom import build

TIMEOUT = 120

# SEI; CLC; XCE; force blank; CGRAM address 0; CGRAM0 = constant; screen on; spin.
COLOUR_CODE = bytearray([
    0x78, 0x18, 0xFB,
    0xA9, 0x80, 0x8D, 0x00, 0x21,
    0x9C, 0x21, 0x21,
    0xA9, 0x00, 0x8D, 0x22, 0x21,  # low byte placeholder
    0xA9, 0x00, 0x8D, 0x22, 0x21,  # high byte placeholder
    0xA9, 0x0F, 0x8D, 0x00, 0x21,
    0x80, 0xFE,
])


def reference_colour(mask: int) -> int:
    """Gamepad bit i (B=0 .. R=11) becomes CGRAM bit 11-i."""
    return sum(((mask >> i) & 1) << (11 - i) for i in range(12))


def colour_rom(colour: int) -> bytes:
    code = bytearray(COLOUR_CODE)
    code[12] = colour & 0xFF
    code[17] = (colour >> 8) & 0xFF
    return build(bytes(code))


def states() -> list[int]:
    singletons = [1 << i for i in range(12)]
    # B|A|Start|Up, ignored high bits alone, ignored high bits combined with B.
    return [0, *singletons, 0x119, 0xF000, 0xF001]


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_input.py <test_input binary>", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()

    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        joypad = temporary / "joypad.sfc"
        subprocess.run(
            [sys.executable, Path(__file__).with_name("make_min_rom.py"), joypad, "joypad"],
            check=True,
            timeout=TIMEOUT,
        )

        references: dict[int, Path] = {}
        for mask in states():
            colour = reference_colour(mask & 0x0FFF)
            path = temporary / f"ref-{colour:04x}.sfc"
            if not path.exists():
                path.write_bytes(colour_rom(colour))
            references[mask] = path

        command = [str(binary), str(joypad), str(references[0])]
        command += [f"{mask:04x}:{references[mask]}" for mask in states() if mask != 0]
        result = subprocess.run(command, capture_output=True, text=True, timeout=TIMEOUT)

    if result.stdout:
        print(result.stdout, end="")
    if result.returncode != 0:
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
