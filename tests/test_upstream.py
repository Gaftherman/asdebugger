"""Exercise update decisions and real Git publication without GitHub writes."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/upstream.py"
SPEC = importlib.util.spec_from_file_location("upstream", SCRIPT)
upstream = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(upstream)
SHA_A = "a" * 40
SHA_B = "b" * 40
RELEASE = {"id": 42, "tag_name": "v1.0", "html_url": "https://github.com/metamod-fallguys/asext/releases/tag/v1.0"}


class Decisions(unittest.TestCase):
    def candidate(self, release=RELEASE, exists=False):
        return upstream.release_candidate("owner/template", "main", SHA_A, release,
                                          lambda tag: exists, lambda tag: (SHA_A, SHA_B))

    def test_no_release(self):
        self.assertIsNone(self.candidate(None))

    def test_existing_release(self):
        self.assertIsNone(self.candidate(exists=True))

    def test_prerelease_and_draft(self):
        for key in ("prerelease", "draft"):
            self.assertIsNone(self.candidate(dict(RELEASE, **{key: True})))

    def test_new_release_has_immutable_refs(self):
        candidate = self.candidate()
        self.assertEqual("asext-release-42", candidate["template_tag"])
        self.assertEqual(SHA_B, candidate["metamod_sha"])

    def test_bad_commit_is_rejected(self):
        with self.assertRaises(ValueError):
            upstream.sha("main")

    def test_api_only_treats_404_as_missing(self):
        for status in (404, 403, 500):
            error = urllib.error.HTTPError("https://api.github.com/test", status, "error", {}, None)
            with patch.object(upstream.urllib.request, "urlopen", side_effect=error):
                if status == 404:
                    self.assertIsNone(upstream.api("test", missing_ok=True))
                else:
                    with self.assertRaises(RuntimeError):
                        upstream.api("test", missing_ok=True)


if __name__ == "__main__":
    unittest.main()
