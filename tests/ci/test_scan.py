"""Tests for tools/ci/scan.py. All fixtures are synthetic, built in temp dirs."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))

import scan


def rom_bytes(offset=0x7FDC, size=64 * 1024):
    """Synthetic bytes with a checksum/complement pair (XOR 0xFFFF) at `offset`."""
    data = bytearray(size)
    data[offset:offset + 2] = (0x1234).to_bytes(2, "little")      # complement
    data[offset + 2:offset + 4] = (0xEDCB).to_bytes(2, "little")  # checksum
    return bytes(data)


class ScanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def write(self, name, data):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(data, str):
            data = data.encode()
        p.write_bytes(data)
        return p

    def rules(self, path):
        return {f.rule for f in scan.scan_file(path)}

    def test_clean_text_passes(self):
        p = self.write("notes.txt", "just some text\n")
        self.assertEqual(scan.scan_file(p), [])

    def test_rom_extension(self):
        p = self.write("game.sfc", "not a rom\n")
        self.assertEqual(self.rules(p), {"rom-extension"})

    def test_disguised_rom(self):
        p = self.write("x.png", rom_bytes())
        self.assertIn("snes-header", self.rules(p))

    def test_copier_header_offset(self):
        p = self.write("x.png", rom_bytes(offset=0x7FDC + 0x200,
                                            size=64 * 1024 + 0x200))
        self.assertIn("snes-header", self.rules(p))

    def test_archive_rejected(self):
        by_magic = self.write("notes.txt", b"PK\x03\x04" + b"\x00" * 16)
        by_ext = self.write("bundle.zip", "not really a zip\n")
        self.assertIn("archive", self.rules(by_magic))
        self.assertIn("archive", self.rules(by_ext))

    def test_too_large(self):
        p = self.write("big.bin", b"\x00" * (1024 * 1024 + 1))
        self.assertEqual(self.rules(p), {"too-large"})

    def test_png_under_assets_ok(self):
        p = self.write("assets/sprites/hero.png",
                       b"\x89PNG\r\n\x1a\n" + b"\x00" * 64)
        self.assertEqual(scan.scan_file(p), [])


if __name__ == "__main__":
    unittest.main()
