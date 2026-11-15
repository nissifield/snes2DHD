#!/usr/bin/env python3
"""Write the 32 KiB LoROM test program used by the headless runner."""

import sys
from pathlib import Path

# SEI; CLC; XCE; force blank; CGRAM colour 0 = red; screen on at full brightness; loop forever.
CODE = bytes([
    0x78, 0x18, 0xFB,
    0xA9, 0x80, 0x8D, 0x00, 0x21,
    0x9C, 0x21, 0x21,
    0xA9, 0x1F, 0x8D, 0x22, 0x21,
    0x9C, 0x22, 0x21,
    0xA9, 0x0F, 0x8D, 0x00, 0x21,
    0x80, 0xFE,
])

# Same, but colour 0 comes from work RAM $0000/$0001 so the frame exposes the
# power-on RAM contents instead of hiding them behind a constant.
RAM_CODE = bytes([
    0x78, 0x18, 0xFB,
    0xA9, 0x80, 0x8D, 0x00, 0x21,
    0x9C, 0x21, 0x21,
    0xAD, 0x00, 0x00, 0x8D, 0x22, 0x21,
    0xAD, 0x01, 0x00, 0x29, 0x7F, 0x8D, 0x22, 0x21,
    0xA9, 0x0F, 0x8D, 0x00, 0x21,
    0x80, 0xFE,
])


def build(code: bytes = CODE) -> bytes:
    rom = bytearray(b"\xff" * 32768)
    rom[0:len(code)] = code
    rom[0x7FC0:0x7FD5] = b"SNES2DHD MIN TEST".ljust(21, b" ")
    # LoROM, 32 KiB, no coprocessor, version 0.
    rom[0x7FD5:0x7FDC] = bytes([0x20, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00])
    for offset in range(0x7FE0, 0x8000, 2):
        rom[offset:offset + 2] = b"\x00\x80"

    # Checksum: sum with a placeholder, then complement.
    rom[0x7FDC:0x7FE0] = b"\xff\xff\x00\x00"
    checksum = sum(rom) & 0xFFFF
    rom[0x7FDE:0x7FE0] = checksum.to_bytes(2, "little")
    rom[0x7FDC:0x7FDE] = (checksum ^ 0xFFFF).to_bytes(2, "little")
    return bytes(rom)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("usage: make_min_rom.py <out path> [ram-sensitive]", file=sys.stderr)
        return 2
    ram_sensitive = len(sys.argv) == 3
    if ram_sensitive and sys.argv[2] != "ram-sensitive":
        print("usage: make_min_rom.py <out path> [ram-sensitive]", file=sys.stderr)
        return 2
    Path(sys.argv[1]).write_bytes(build(RAM_CODE if ram_sensitive else CODE))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
