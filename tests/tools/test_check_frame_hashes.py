"""Offline tests for tests/roms/check_frame_hashes.py. Fixtures are b"test"."""
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tests" / "roms" / "check_frame_hashes.py"
BYTES = b"test"
SHA = hashlib.sha256(BYTES).hexdigest()


@unittest.skipIf(os.name == "nt", "shebang fake needs POSIX")
class CheckFrameHashesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.roms = self.tmp / "roms"
        self.roms.mkdir()
        (self.roms / "hello.sfc").write_bytes(BYTES)
        self.golden = self.tmp / "golden.txt"
        self.manifest = self.tmp / "test-roms.json"
        self.manifest.write_text(json.dumps([{
            "file": "hello.sfc",
            "url": "https://example.invalid/hello.sfc",
            "sha256": SHA,
            "license": "MIT",
            "license_url": "https://example.invalid/LICENSE",
            "source": "https://example.invalid/repo",
            "frames": 300,
        }]), encoding="utf-8")
        self.headless = self.tmp / "fake-headless"
        self.headless.write_text(
            "#!/usr/bin/env python3\n"
            f"print('frame 300 256x224 sha256={SHA}')\n",
            encoding="utf-8")
        self.headless.chmod(self.headless.stat().st_mode | stat.S_IXUSR)

    def invoke(self):
        return subprocess.run(
            [sys.executable, str(SCRIPT),
             "--headless", str(self.headless),
             "--roms", str(self.roms),
             "--golden", str(self.golden),
             "--manifest", str(self.manifest)],
            capture_output=True, text=True, timeout=60)

    def test_matching_golden_passes(self):
        self.golden.write_text(
            f"# frame hashes\n\nhello.sfc {SHA}\n", encoding="utf-8")

        result = self.invoke()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ok hello.sfc\n", result.stdout)
        self.assertIn("frame hashes: 1/1 ok\n", result.stdout)

    def test_mismatch_fails_and_prints_golden_lines(self):
        self.golden.write_text(f"hello.sfc {'0' * 64}\n", encoding="utf-8")

        result = self.invoke()

        self.assertEqual(result.returncode, 1)
        self.assertIn("FAIL hello.sfc\n", result.stdout)
        self.assertIn("golden lines for this build:\n", result.stdout)
        self.assertIn(f"hello.sfc {SHA}\n", result.stdout)

    def test_missing_golden_fails_and_prints_golden_lines(self):
        result = self.invoke()

        self.assertEqual(result.returncode, 1)
        self.assertIn("golden lines for this build:\n", result.stdout)
        self.assertIn(f"hello.sfc {SHA}\n", result.stdout)

    def test_missing_rom_fails(self):
        (self.roms / "hello.sfc").unlink()

        result = self.invoke()

        self.assertEqual(result.returncode, 1)
        self.assertIn("missing hello.sfc", result.stderr)
        self.assertIn("tools/roms/fetch_test_roms.py", result.stderr)

    def test_malformed_golden_line_fails(self):
        self.golden.write_text("hello.sfc not-a-hash\n", encoding="utf-8")

        result = self.invoke()

        self.assertEqual(result.returncode, 1)
        self.assertIn("malformed golden line", result.stderr)


if __name__ == "__main__":
    unittest.main()
