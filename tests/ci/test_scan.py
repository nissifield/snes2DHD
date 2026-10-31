"""Tests for tools/ci/scan.py. All fixtures are synthetic, built in temp dirs."""
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "ci"))

import scan

PNG = b"\x89PNG\r\n\x1a\n"
IEND = b"\x00\x00\x00\x00IEND\xae\x42\x60\x82"


def rom_bytes(offset=0x7FDC, size=64 * 1024, map_mode=0x21, rom_size=0x0A):
    """Synthetic bytes with a plausible SNES header at `offset`."""
    data = bytearray(size)
    data[offset - 7] = map_mode   # map mode
    data[offset - 5] = rom_size   # ROM size
    data[offset:offset + 2] = (0x1234).to_bytes(2, "little")      # complement
    data[offset + 2:offset + 4] = (0xEDCB).to_bytes(2, "little")  # checksum
    return bytes(data)


def zip_bytes():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("a.txt", "hi")
    return buf.getvalue()


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
        return name

    def rules(self, rel):
        return {f.rule for f in scan.scan_file(self.root, rel)}

    def test_clean_text_passes(self):
        rel = self.write("notes.txt", "just some text\n")
        self.assertEqual(scan.scan_file(self.root, rel), [])

    def test_rom_extension(self):
        rel = self.write("game.sfc", "not a rom\n")
        self.assertEqual(self.rules(rel), {"rom-extension"})

    def test_disguised_rom(self):
        rel = self.write("x.png", rom_bytes())
        self.assertIn("snes-header", self.rules(rel))

    def test_copier_header_offset(self):
        rel = self.write("x.png", rom_bytes(offset=0x7FDC + 0x200,
                                              size=64 * 1024 + 0x200))
        self.assertIn("snes-header", self.rules(rel))

    def test_snes_header_requires_plausible_fields(self):
        bad_map = self.write("a.bin", rom_bytes(map_mode=0x00))
        bad_size = self.write("b.bin", rom_bytes(rom_size=0x07))
        self.assertNotIn("snes-header", self.rules(bad_map))
        self.assertNotIn("snes-header", self.rules(bad_size))

    def test_archive_rejected(self):
        by_magic = self.write("notes.txt", b"PK\x03\x04" + b"\x00" * 16)
        by_ext = self.write("bundle.zip", "not really a zip\n")
        self.assertIn("archive", self.rules(by_magic))
        self.assertIn("archive", self.rules(by_ext))

    def test_xz_bz2_zstd_tar_magics(self):
        for i, magic in enumerate((b"\xfd7zXZ\x00", b"BZh9", b"\x28\xb5\x2f\xfd")):
            rel = self.write(f"blob{i}.bin", magic + b"\x00" * 16)
            self.assertIn("archive", self.rules(rel), magic)
        tar = self.write("tar.bin", b"\x00" * 257 + b"ustar" + b"\x00" * 16)
        self.assertIn("archive", self.rules(tar))

    def test_png_with_appended_zip(self):
        rel = self.write("assets/sprite.png", PNG + IEND + zip_bytes())
        self.assertIn("archive", self.rules(rel))

    def test_too_large(self):
        rel = self.write("big.bin", b"\x00" * (1024 * 1024 + 1))
        self.assertEqual(self.rules(rel), {"too-large"})

    def test_text_too_large(self):
        rel = self.write("long.txt", "lorem ipsum! " * 21000)
        self.assertEqual(self.rules(rel), {"too-large"})

    def test_png_under_assets_ok(self):
        rel = self.write("assets/sprites/hero.png", PNG + IEND)
        self.assertEqual(scan.scan_file(self.root, rel), [])

    def test_png_trailing_data(self):
        rel = self.write("assets/sprites/hero.png", PNG + IEND + b"payload")
        self.assertIn("trailing-data", self.rules(rel))

    def test_image_magic_must_match_extension(self):
        rel = self.write("assets/a.png", "plain text, not an image\n")
        self.assertIn("magic-mismatch", self.rules(rel))

    def test_zeroed_checksum_rom_named_png(self):
        data = bytearray(rom_bytes())
        data[0x7FDC:0x7FDC + 4] = b"\x00" * 4  # zeroed checksum pair
        rel = self.write("assets/sprite.png", bytes(data))
        rules = self.rules(rel)
        self.assertNotIn("snes-header", rules)
        self.assertIn("magic-mismatch", rules)

    def test_jpeg_rejected(self):
        for name in ("assets/p.jpg", "assets/p.jpeg", "assets/p.webp"):
            rel = self.write(name, b"\xff\xd8\xff" + b"\x00" * 16)
            self.assertIn("unsupported-image", self.rules(rel), name)

    def test_svg_must_be_utf8(self):
        good = self.write("assets/logo.svg", "<svg xmlns='http://www.w3.org/2000/svg'/>\n")
        bad = self.write("assets/evil.svg", b"\x89PNG\r\n\x1a\n" + b"\x00" * 8)
        self.assertEqual(scan.scan_file(self.root, good), [])
        self.assertIn("magic-mismatch", self.rules(bad))

    def test_base64_blob(self):
        rel = self.write("notes.txt", "payload:\n" + "QUJDRA" * 800)
        self.assertIn("encoded-blob", self.rules(rel))

    def test_hex_blob(self):
        rel = self.write("notes.txt", "deadbeef" * 600)
        self.assertIn("encoded-blob", self.rules(rel))

    def test_wrapped_base64_flagged(self):
        blob = "QUJDRA" * 850  # 5100 chars, but every line is 76 chars
        wrapped = "\n".join(blob[i:i + 76] for i in range(0, len(blob), 76))
        rel = self.write("notes.txt", wrapped + "\n")
        self.assertIn("encoded-blob", self.rules(rel))

    def test_c_byte_array_flagged(self):
        arr = ", ".join(f"0x{i % 256:02X}" for i in range(2100))
        rel = self.write("data.h", f"const char x[] = {{{arr}}};\n")
        self.assertIn("encoded-blob", self.rules(rel))

    def test_svg_with_base64_flagged(self):
        blob = "QUJDRA" * 850
        svg = f"<svg xmlns='http://www.w3.org/2000/svg'><!-- {blob} --></svg>\n"
        rel = self.write("assets/logo.svg", svg)
        self.assertIn("encoded-blob", self.rules(rel))

    def test_lfs_pointer_rejected(self):
        rel = self.write("assets/big.png",
                         b"version https://git-lfs.github.com/spec/v1\n"
                         b"oid sha256:" + b"a" * 64 + b"\nsize 123\n")
        self.assertIn("lfs-pointer", self.rules(rel))

    def test_png_fake_iend_after_payload(self):
        rel = self.write("assets/sprites/hero.png",
                         PNG + IEND + b"\x00" * 100 + IEND)
        self.assertIn("trailing-data", self.rules(rel))

    def test_png_private_chunk_rejected(self):
        payload = b"\x00" * 32
        priv = len(payload).to_bytes(4, "big") + b"vpAg" + payload + b"\x00" * 4
        rel = self.write("assets/sprites/hero.png", PNG + priv + IEND)
        self.assertIn("png-chunk", self.rules(rel))

    def test_real_branding_pngs_pass(self):
        repo = Path(__file__).resolve().parents[2]
        for name in ("banner.png", "logo-32.png", "logo-512.png",
                     "social-preview.png"):
            rel = f"assets/branding/{name}"
            self.assertEqual(scan.scan_file(repo, rel), [], rel)

    def test_src_assets_binary(self):
        rel = self.write("src/assets/x.bin", b"\xff\xfe\x00\x01")
        self.assertIn("binary-outside-assets", self.rules(rel))

    def test_unknown_binary_in_assets(self):
        rel = self.write("assets/blob.bin", b"\xff\xfe\x00\x01")
        self.assertIn("unknown-binary", self.rules(rel))

    def test_branding_limit_only_under_assets_branding(self):
        data = PNG + b"\x00" * (2 * 1024 * 1024)
        branding = self.write("assets/branding/big.png", data)
        other = self.write("assets/other/big.png", data)
        self.assertNotIn("too-large", self.rules(branding))
        self.assertIn("too-large", self.rules(other))

    def test_scan_takes_rel_paths(self):
        self.write("assets/logo.svg", "<svg/>\n")
        self.assertEqual(scan.scan(self.root, ["assets/logo.svg"]), [])
        rel = self.write("game.sfc", "not a rom\n")
        findings = scan.scan(self.root, [rel])
        self.assertEqual(findings[0].path, "game.sfc")


if __name__ == "__main__":
    unittest.main()
