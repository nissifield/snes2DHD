"""Offline tests for tools/nightly/package.py. Fixtures are fake binaries."""
import contextlib
import importlib.util
import io
import shutil
import stat
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "nightly" / "package.py"


def load_script():
    spec = importlib.util.spec_from_file_location("package_under_test", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.module = load_script()
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)

        # A temp root stands in for the repo so licence content is copied, not
        # hardcoded.
        self.root = self.tmp / "repo"
        (self.root / "external" / "ares").mkdir(parents=True)
        (self.root / "LICENSE").write_text("project licence\n")
        (self.root / "NOTICE").write_text("notice\n")
        (self.root / "external" / "ares" / "LICENSE").write_text("ares licence\n")

        self.app = self.tmp / "app"
        self.headless = self.tmp / "headless"
        self.app.write_bytes(b"app")
        self.headless.write_bytes(b"headless")
        self.app.chmod(0o755)
        self.headless.chmod(0o644)

    def run_package(self, out, files):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = self.module.package(out, files, self.root)
        return code, stdout.getvalue(), stderr.getvalue()

    def test_tar_gz_exact_members_and_exec_mode(self):
        out = self.tmp / "dist" / "snes2dhd-linux-x86_64.tar.gz"

        code, stdout, stderr = self.run_package(out, [self.app, self.headless])

        self.assertEqual(code, 0, stderr)
        self.assertEqual(stdout, f"wrote {out} (5 files)\n")
        self.assertEqual(stat.S_IMODE(out.stat().st_mode), 0o644)
        with tarfile.open(out) as archive:
            self.assertEqual(archive.getnames(), [
                "snes2dhd/app",
                "snes2dhd/headless",
                "snes2dhd/LICENSE",
                "snes2dhd/NOTICE",
                "snes2dhd/licenses/ares-LICENSE",
            ])
            self.assertTrue(archive.getmember("snes2dhd/app").mode & 0o111)
            self.assertFalse(archive.getmember("snes2dhd/headless").mode & 0o111)
            self.assertEqual(archive.extractfile("snes2dhd/LICENSE").read(),
                             b"project licence\n")
            self.assertEqual(
                archive.extractfile("snes2dhd/licenses/ares-LICENSE").read(),
                b"ares licence\n")

    def test_tar_gz_follows_symlinked_input_and_licence(self):
        out = self.tmp / "dist" / "snes2dhd-linux-x86_64.tar.gz"
        real_app = self.tmp / "real-app"
        real_app.write_bytes(b"app")
        links = self.tmp / "links"
        links.mkdir()
        linked_app = links / "app"
        linked_app.symlink_to(real_app)
        real_licence = self.tmp / "real-LICENSE"
        real_licence.write_bytes(b"project licence\n")
        (self.root / "LICENSE").unlink()
        (self.root / "LICENSE").symlink_to(real_licence)

        code, stdout, stderr = self.run_package(out, [linked_app])

        self.assertEqual(code, 0, stderr)
        with tarfile.open(out) as archive:
            for member in archive.getmembers():
                self.assertTrue(member.isfile(), member.name)
                self.assertFalse(member.issym(), member.name)
                self.assertFalse(member.islnk(), member.name)
            self.assertEqual(archive.extractfile("snes2dhd/app").read(), b"app")
            self.assertEqual(archive.extractfile("snes2dhd/LICENSE").read(),
                             b"project licence\n")

    def test_zip_exact_members_and_exec_mode(self):
        out = self.tmp / "dist" / "snes2dhd-windows-x86_64.zip"

        code, stdout, stderr = self.run_package(out, [self.app, self.headless])

        self.assertEqual(code, 0, stderr)
        self.assertEqual(stdout, f"wrote {out} (5 files)\n")
        self.assertEqual(stat.S_IMODE(out.stat().st_mode), 0o644)
        with zipfile.ZipFile(out) as archive:
            self.assertEqual(archive.namelist(), [
                "snes2dhd/app",
                "snes2dhd/headless",
                "snes2dhd/LICENSE",
                "snes2dhd/NOTICE",
                "snes2dhd/licenses/ares-LICENSE",
            ])
            self.assertTrue(
                (archive.getinfo("snes2dhd/app").external_attr >> 16) & 0o111)
            self.assertFalse(
                (archive.getinfo("snes2dhd/headless").external_attr >> 16) & 0o111)
            self.assertEqual(archive.read("snes2dhd/NOTICE"), b"notice\n")

    def test_bad_extension_is_usage_error(self):
        out = self.tmp / "dist" / "snes2dhd.tar"

        code, stdout, stderr = self.run_package(out, [self.app])

        self.assertEqual(code, 2)
        self.assertIn("usage:", stderr)
        self.assertFalse(out.exists())

    def test_main_without_arguments_is_usage_error(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = self.module.main([])

        self.assertEqual(code, 2)
        self.assertIn("usage:", stderr.getvalue())

    def test_missing_input_writes_no_archive(self):
        out = self.tmp / "dist" / "snes2dhd.tar.gz"
        missing = self.tmp / "missing-bin"

        code, stdout, stderr = self.run_package(out, [self.app, missing])

        self.assertEqual(code, 1)
        self.assertIn("missing-bin", stderr)
        self.assertFalse(out.exists())
        self.assertFalse(out.parent.exists())

    def test_output_directory_failure_leaves_no_temporary_archive(self):
        out = self.tmp / "dist" / "snes2dhd.tar.gz"
        out.mkdir(parents=True)

        code, stdout, stderr = self.run_package(out, [self.app])

        self.assertEqual(code, 1)
        self.assertTrue(out.is_dir())
        self.assertEqual([p.name for p in out.parent.iterdir()], [out.name])

    def test_duplicate_base_names_write_no_archive(self):
        other = self.tmp / "other"
        other.mkdir()
        duplicate = other / "app"
        duplicate.write_bytes(b"duplicate")
        out = self.tmp / "dist" / "snes2dhd.tar.gz"

        code, stdout, stderr = self.run_package(out, [self.app, duplicate])

        self.assertEqual(code, 1)
        self.assertIn("app", stderr)
        self.assertFalse(out.exists())

    def test_input_clashing_with_bundled_licence_writes_no_archive(self):
        out = self.tmp / "dist" / "snes2dhd.tar.gz"
        (self.tmp / "clash").mkdir()
        for name in ("LICENSE", "NOTICE", "licenses"):
            with self.subTest(name=name):
                clash = self.tmp / "clash" / name
                clash.write_bytes(b"clash")

                code, stdout, stderr = self.run_package(out, [clash])

                self.assertEqual(code, 1)
                self.assertIn(name, stderr)
                self.assertFalse(out.exists())

    def test_missing_licence_file_is_named(self):
        out = self.tmp / "dist" / "snes2dhd.tar.gz"
        (self.root / "NOTICE").unlink()

        code, stdout, stderr = self.run_package(out, [self.app])

        self.assertEqual(code, 1)
        self.assertIn("NOTICE", stderr)
        self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
