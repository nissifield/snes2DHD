"""Behavioral tests for tools/nightly/publish.sh with a recording fake gh.

No network and no real credentials: every gh call is replayed from routes.json.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "nightly" / "publish.sh"

SHA = "0123456789abcdef0123456789abcdef01234567"
TAG = "nightly-0123456789ab"
REPO = "owner/repo"
RELEASE_URL = f"repos/{REPO}/releases/tags/{TAG}"
TAG_URL = f"repos/{REPO}/git/ref/tags/{TAG}"
ASSETS = ("SHA256SUMS", "snes2dhd-linux-x86_64.tar.gz", "snes2dhd-source.tar.gz")

FOUND_RELEASE = {"code": 0, "stdout": "\n".join(ASSETS) + "\n"}
OK_TAG = {"code": 0, "stdout": f"commit {SHA}\n"}
NOT_FOUND = {"code": 1, "stderr": "gh: Not Found (HTTP 404)\n"}
CREATED = {"code": 0, "stdout": "created release\n"}
OTHER_SHA = "f" * 40
SERVER_ERROR = {"code": 1, "stderr": "gh: Server Error (HTTP 500)\n"}

FAKE_GH = '''#!/usr/bin/env python3
"""Record argv, then replay the next canned response for the call's key."""
import json
import os
import sys

args = sys.argv[1:]
with open(os.environ["FAKE_GH_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(args) + "\\n")
if args[:2] == ["release", "create"]:
    key = "release create"
else:
    key = args[1]
with open(os.environ["FAKE_GH_ROUTES"], encoding="utf-8") as f:
    responses = json.load(f).get(key)
if responses is None:
    print(f"fake gh: unexpected call {args}", file=sys.stderr)
    sys.exit(99)
state_path = os.environ["FAKE_GH_STATE"]
state = {}
if os.path.exists(state_path):
    with open(state_path, encoding="utf-8") as f:
        state = json.load(f)
index = state.get(key, 0)
response = responses[min(index, len(responses) - 1)]
state[key] = index + 1
with open(state_path, "w", encoding="utf-8") as f:
    json.dump(state, f)
sys.stdout.write(response.get("stdout", ""))
sys.stderr.write(response.get("stderr", ""))
sys.exit(response.get("code", 0))
'''


class PublishTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        bindir = self.tmp / "bin"
        bindir.mkdir()
        fake_gh = bindir / "gh"
        fake_gh.write_text(FAKE_GH)
        fake_gh.chmod(0o755)
        self.dist = self.tmp / "dist"
        self.dist.mkdir()
        for name in ASSETS:
            (self.dist / name).write_text(name)
        self.log = self.tmp / "gh.log"
        self.routes_file = self.tmp / "routes.json"
        self.state_file = self.tmp / "state.json"
        # Explicit minimal env; nothing is inherited from the test runner.
        self.env = {
            "PATH": os.pathsep.join([str(bindir),
                                     str(Path(sys.executable).parent),
                                     "/usr/bin", "/bin"]),
            "GITHUB_SHA": SHA,
            "GH_REPO": REPO,
            "TMPDIR": str(self.tmp),
            "FAKE_GH_LOG": str(self.log),
            "FAKE_GH_ROUTES": str(self.routes_file),
            "FAKE_GH_STATE": str(self.state_file),
        }

    def run_publish(self, routes):
        self.routes_file.write_text(json.dumps(routes))
        self.log.write_text("")
        if self.state_file.exists():
            self.state_file.unlink()
        return subprocess.run(["bash", str(SCRIPT), str(self.dist)],
                              capture_output=True, text=True, env=self.env)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def creates(self):
        return [call for call in self.calls() if call[:2] == ["release", "create"]]

    def assert_create(self, argv, mode):
        self.assertEqual(argv[2], TAG)
        self.assertEqual(argv[3:6], [str(self.dist / name) for name in ASSETS])
        self.assertEqual(len([arg for arg in argv if arg.startswith(str(self.dist))]), 3)
        self.assertIn("--latest=false", argv)
        self.assertIn("--prerelease", argv)
        self.assertNotIn("--clobber", argv)
        self.assertNotIn("--force", argv)
        if mode == "verify":
            self.assertIn("--verify-tag", argv)
            self.assertNotIn("--target", argv)
        else:
            self.assertNotIn("--verify-tag", argv)
            self.assertEqual(argv[argv.index("--target") + 1], SHA)

    def test_existing_release_with_matching_target_and_assets_skips(self):
        result = self.run_publish({RELEASE_URL: [{"code": 0}, FOUND_RELEASE],
                                   TAG_URL: [OK_TAG]})

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("already exists", result.stdout)
        self.assertEqual(self.creates(), [])

    def test_existing_release_target_mismatch_fails(self):
        result = self.run_publish({
            RELEASE_URL: [{"code": 0}, FOUND_RELEASE],
            TAG_URL: [{"code": 0, "stdout": f"commit {OTHER_SHA}\n"}],
        })

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("refusing", result.stderr)
        self.assertEqual(self.creates(), [])

    def test_existing_release_asset_mismatch_fails(self):
        result = self.run_publish({
            RELEASE_URL: [{"code": 0},
                          {"code": 0, "stdout": ASSETS[0] + "\n"}],
            TAG_URL: [OK_TAG],
        })

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("refusing", result.stderr)
        self.assertEqual(self.creates(), [])

    def test_release_lookup_non404_error_fails(self):
        result = self.run_publish({RELEASE_URL: [SERVER_ERROR]})

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("could not query release", result.stderr)
        self.assertEqual([call[:2] for call in self.calls()], [["api", RELEASE_URL]])
        self.assertEqual(self.creates(), [])

    def test_missing_release_with_exact_lightweight_tag_uses_verify_tag(self):
        result = self.run_publish({RELEASE_URL: [NOT_FOUND],
                                   TAG_URL: [OK_TAG, OK_TAG],
                                   "release create": [CREATED]})

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(["api", TAG_URL], [call[:2] for call in self.calls()])
        creates = self.creates()
        self.assertEqual(len(creates), 1)
        self.assert_create(creates[0], "verify")

    def test_mismatched_tag_fails(self):
        result = self.run_publish({
            RELEASE_URL: [NOT_FOUND],
            TAG_URL: [{"code": 0, "stdout": f"commit {OTHER_SHA}\n"}],
        })

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("lightweight tag", result.stderr)
        self.assertEqual(self.creates(), [])

    def test_annotated_tag_is_rejected(self):
        result = self.run_publish({
            RELEASE_URL: [NOT_FOUND],
            TAG_URL: [{"code": 0, "stdout": f"tag {SHA}\n"}],
        })

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("lightweight tag", result.stderr)
        self.assertEqual(self.creates(), [])

    def test_missing_release_and_missing_tag_creates_targeted_release(self):
        result = self.run_publish({RELEASE_URL: [NOT_FOUND],
                                   TAG_URL: [NOT_FOUND, OK_TAG],
                                   "release create": [CREATED]})

        self.assertEqual(result.returncode, 0, result.stderr)
        creates = self.creates()
        self.assertEqual(len(creates), 1)
        self.assert_create(creates[0], "target")

    def test_tag_lookup_non404_error_fails(self):
        result = self.run_publish({RELEASE_URL: [NOT_FOUND],
                                   TAG_URL: [SERVER_ERROR]})

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("could not query tag", result.stderr)
        self.assertEqual(self.creates(), [])

    def test_creation_failure_propagates_nonzero(self):
        result = self.run_publish({RELEASE_URL: [NOT_FOUND],
                                   TAG_URL: [NOT_FOUND],
                                   "release create": [SERVER_ERROR]})

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("HTTP 500", result.stderr)
        self.assertEqual(len(self.creates()), 1)
        self.assertFalse(any("delete" in call for call in self.calls()))

    def test_tag_race_after_create_fails_without_deleting(self):
        result = self.run_publish({
            RELEASE_URL: [NOT_FOUND],
            TAG_URL: [NOT_FOUND, {"code": 0, "stdout": f"commit {OTHER_SHA}\n"}],
            "release create": [CREATED],
        })

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("after publish", result.stderr)
        self.assertFalse(any("delete" in call for call in self.calls()))


if __name__ == "__main__":
    unittest.main()
