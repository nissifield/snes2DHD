"""Offline tests for tools/roms/fetch_test_roms.py. Fixtures are b"test"."""
import contextlib
import hashlib
import importlib.util
import io
import json
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "roms" / "fetch_test_roms.py"
BYTES = b"test"
SHA = hashlib.sha256(BYTES).hexdigest()


def load_script():
    spec = importlib.util.spec_from_file_location("fetch_test_roms_under_test",
                                                  SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class FetchTestRomsTest(unittest.TestCase):
    def setUp(self):
        self.module = load_script()
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.dest = self.tmp / "roms"
        self.manifest = self.tmp / "test-roms.json"
        self.module.DEFAULT_MANIFEST = self.manifest

    def entry(self, **overrides):
        entry = {
            "file": "hello.sfc",
            "url": "https://example.invalid/hello.sfc",
            "sha256": SHA,
            "license": "MIT",
            "license_url": "https://example.invalid/LICENSE",
            "source": "https://example.invalid/repo",
            "frames": 300,
        }
        entry.update(overrides)
        return entry

    def write(self, *entries):
        self.manifest.write_text(json.dumps(list(entries)), encoding="utf-8")

    def invoke(self):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = self.module.main(["--dest", str(self.dest)])
        return code, out.getvalue(), err.getvalue()

    def test_download_writes_file(self):
        self.write(self.entry())
        response = Response(BYTES)
        with mock.patch.object(self.module.urllib.request, "urlopen",
                               return_value=response) as urlopen:
            code, out, err = self.invoke()

        self.assertEqual(code, 0, err)
        self.assertEqual(out, "fetched hello.sfc\n")
        self.assertEqual((self.dest / "hello.sfc").read_bytes(), BYTES)
        urlopen.assert_called_once_with("https://example.invalid/hello.sfc",
                                        timeout=60)

    def test_cached_file_is_not_downloaded(self):
        self.write(self.entry())
        self.dest.mkdir()
        (self.dest / "hello.sfc").write_bytes(BYTES)
        with mock.patch.object(self.module.urllib.request, "urlopen",
                               side_effect=AssertionError("downloaded")):
            code, out, err = self.invoke()

        self.assertEqual(code, 0, err)
        self.assertEqual(out, "ok hello.sfc\n")

    def test_wrong_hash_writes_no_file(self):
        self.write(self.entry(sha256="0" * 64))
        with mock.patch.object(self.module.urllib.request, "urlopen",
                               return_value=Response(BYTES)):
            code, out, err = self.invoke()

        self.assertEqual(code, 1)
        self.assertIn("hello.sfc", err)
        self.assertFalse((self.dest / "hello.sfc").exists())

    def test_http_url_is_rejected(self):
        self.write(self.entry(url="http://example.invalid/hello.sfc"))
        with mock.patch.object(self.module.urllib.request, "urlopen") as urlopen:
            code, out, err = self.invoke()

        self.assertEqual(code, 1)
        self.assertIn("hello.sfc", err)
        urlopen.assert_not_called()
        self.assertFalse(self.dest.exists())

    def test_gpl_license_is_rejected(self):
        self.write(self.entry(license="GPL-3.0-only"))
        with mock.patch.object(self.module.urllib.request, "urlopen") as urlopen:
            code, out, err = self.invoke()

        self.assertEqual(code, 1)
        self.assertIn("hello.sfc", err)
        urlopen.assert_not_called()

    def test_parent_path_is_rejected(self):
        self.write(self.entry(file="../x.sfc"))
        with mock.patch.object(self.module.urllib.request, "urlopen") as urlopen:
            code, out, err = self.invoke()

        self.assertEqual(code, 1)
        self.assertIn("../x.sfc", err)
        urlopen.assert_not_called()

    def test_oversize_download_is_rejected(self):
        self.write(self.entry())
        oversize = b"x" * (8 * 1024 * 1024 + 1)
        with mock.patch.object(self.module.urllib.request, "urlopen",
                               return_value=Response(oversize)):
            code, out, err = self.invoke()

        self.assertEqual(code, 1)
        self.assertIn("hello.sfc", err)
        self.assertFalse((self.dest / "hello.sfc").exists())

    def test_all_entries_are_checked_before_download(self):
        self.write(self.entry(),
                   self.entry(file="other.sfc", license="GPL-3.0-only"))
        with mock.patch.object(self.module.urllib.request, "urlopen") as urlopen:
            code, out, err = self.invoke()

        self.assertEqual(code, 1)
        self.assertIn("other.sfc", err)
        urlopen.assert_not_called()
        self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    unittest.main()
