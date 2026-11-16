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

# Same, but the joypad value becomes the backdrop: wait for vblank and the
# auto-joypad busy window to end, then write $4218/$4219 >> 4 to CGRAM0.
# Nothing here is a game: every byte is chosen to expose the joypad register.
JOYPAD_CODE = bytes([
    0x78, 0x18, 0xFB,              # SEI; CLC; XCE
    0xA9, 0x80, 0x8D, 0x00, 0x21,  # LDA #$80; STA $2100  (force blank)
    0x9C, 0x21, 0x21,              # STZ $2121             (CGRAM address 0)
    0x9C, 0x22, 0x22,              # STZ $2122             (clear backdrop)
    0x9C, 0x22, 0x22,              # STZ $2122
    0xA9, 0x0F, 0x8D, 0x00, 0x21,  # LDA #$0F; STA $2100  (screen on, full brightness)
    0xA9, 0x01, 0x8D, 0x00, 0x42,  # LDA #$01; STA $4200  (enable auto-joypad)
    0xAD, 0x12, 0x42,              # off:  LDA $4212
    0x29, 0x80,                    #       AND #$80
    0xD0, 0xF9,                    #       BNE off           (wait out the previous vblank)
    0xAD, 0x12, 0x42,              # vbl:  LDA $4212
    0x29, 0x80,                    #       AND #$80
    0xF0, 0xF9,                    #       BEQ vbl           (wait for the next vblank)
    0xAD, 0x12, 0x42,              # busy: LDA $4212
    0x29, 0x01,                    #       AND #$01
    0xF0, 0xF9,                    #       BEQ busy          (busy set)
    0xAD, 0x12, 0x42,              # idle: LDA $4212
    0x29, 0x01,                    #       AND #$01
    0xD0, 0xF9,                    #       BNE idle          (wait until polling is done)
    0x9C, 0x21, 0x21,              # STZ $2121             (CGRAM address 0)
    0xC2, 0x20,                    # REP #$20              (16-bit A)
    0xAD, 0x18, 0x42,              # LDA $4218             (JOY1L:JOY1H)
    0x4A, 0x4A, 0x4A, 0x4A,        # LSR x4                (16-bit shifted word -> CGRAM value)
    0xE2, 0x20,                    # SEP #$20              (8-bit A)
    0x8D, 0x22, 0x21,              # STA $2122             (colour low byte)
    0xEB,                          # XBA                   (high byte into A)
    0x8D, 0x22, 0x21,              # STA $2122             (colour high byte)
    0x4C, 0x22, 0x80,              # JMP vbl
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
        print("usage: make_min_rom.py <out path> [ram-sensitive|joypad]", file=sys.stderr)
        return 2
    variant = sys.argv[2] if len(sys.argv) == 3 else ""
    if variant == "":
        code = CODE
    elif variant == "ram-sensitive":
        code = RAM_CODE
    elif variant == "joypad":
        code = JOYPAD_CODE
    else:
        print("usage: make_min_rom.py <out path> [ram-sensitive|joypad]", file=sys.stderr)
        return 2
    Path(sys.argv[1]).write_bytes(build(code))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
