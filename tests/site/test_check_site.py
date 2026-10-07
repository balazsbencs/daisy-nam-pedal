import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools" / "check_site.py"


class SiteCheckerTest(unittest.TestCase):
    def run_checker(self, files):
        with tempfile.TemporaryDirectory() as tmp:
            site = Path(tmp)
            for name, body in files.items():
                path = site / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(body, encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(CHECKER), str(site)],
                text=True,
                capture_output=True,
                check=False,
            )

    def test_accepts_valid_relative_link_and_fragment(self):
        result = self.run_checker({
            "index.html": '<title>Home</title><main id="top"><a href="manual.html#start">Manual</a></main>',
            "manual.html": '<title>Manual</title><main><h1 id="start">Start</h1></main>',
            "developers.html": "<title>Developers</title><main></main>",
            "hardware.html": "<title>Hardware</title><main></main>",
            "404.html": "<title>Not found</title><main></main>",
        })
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_missing_file_and_fragment_and_duplicate_id(self):
        result = self.run_checker({
            "index.html": '<title>Home</title><main id="same"><a href="missing.html">X</a><a href="#nope">Y</a><b id="same"></b></main>',
            "manual.html": "<title>Manual</title><main></main>",
            "developers.html": "<title>Developers</title><main></main>",
            "hardware.html": "<title>Hardware</title><main></main>",
            "404.html": "<title>Not found</title><main></main>",
        })
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing.html", result.stderr)
        self.assertIn("#nope", result.stderr)
        self.assertIn("duplicate id 'same'", result.stderr)


if __name__ == "__main__":
    unittest.main()
