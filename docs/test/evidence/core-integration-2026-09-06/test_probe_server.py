import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


PROBE = Path(__file__).with_name("probe_server.py")


class ProbeSafetyTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory(prefix="ev-probe-safety-")
        self.workspace = Path(self.temporary_directory.name)
        self.probe = self.workspace / "probe_server.py"
        self.probe.write_bytes(PROBE.read_bytes())

    def tearDown(self):
        self.temporary_directory.cleanup()

    def run_probe(self, *arguments):
        return subprocess.run(
            [sys.executable, str(self.probe), *map(str, arguments)],
            cwd=self.workspace,
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

    def install_mutating_server(self):
        server = self.workspace / "build/apps/admin-server/ev_admin_server"
        server.parent.mkdir(parents=True)
        server.write_text(
            """#!/usr/bin/env python3
import pathlib
import sys

database = pathlib.Path(sys.argv[sys.argv.index("--db") + 1])
database.write_bytes(b"server-mutated-source")
""",
            encoding="utf-8",
        )
        server.chmod(server.stat().st_mode | stat.S_IXUSR)

    def test_refuses_preexisting_fresh_database_before_server_start(self):
        database = self.workspace / "probe-fresh.db"
        database.write_bytes(b"existing-diagnostic-database")

        result = self.run_probe()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("拒绝复用已有诊断库", result.stderr)
        self.assertEqual(database.read_bytes(), b"existing-diagnostic-database")

    def test_rejects_existing_copy_without_touching_source_database(self):
        self.install_mutating_server()
        source_database = self.workspace / "source.db"
        source_database.write_bytes(b"archived-source-database")

        result = self.run_probe("--existing-copy", source_database)

        self.assertEqual(source_database.read_bytes(), b"archived-source-database")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unrecognized arguments: --existing-copy", result.stderr)

    def test_rejects_unknown_database_option(self):
        source_database = self.workspace / "source.db"
        source_database.write_bytes(b"runtime-database")

        result = self.run_probe("--database", source_database)

        self.assertEqual(result.returncode, 2)
        self.assertIn("unrecognized arguments: --database", result.stderr)
        self.assertEqual(source_database.read_bytes(), b"runtime-database")


if __name__ == "__main__":
    unittest.main()
