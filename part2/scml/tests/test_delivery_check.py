"""SCML 交付体检脚本测试。"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "data_generator"
JOBS = ROOT / "warehouse" / "jobs"
SCRIPT = ROOT / "scripts" / "check_scml_delivery.py"

sys.path.insert(0, str(GENERATOR))


class DeliveryCheckTest(unittest.TestCase):
    def setUp(self):
        from generator import GenerationConfig, generate_handoff

        self._temp = tempfile.TemporaryDirectory()
        base = Path(self._temp.name)
        self.ods = base / "ods"
        self.dws = base / "dws"
        self.ads = base / "ads"

        generate_handoff(
            GenerationConfig(
                seed=20260914,
                station_count=2,
                chargers_per_station=4,
                user_count=20,
                order_count=50,
                telemetry_count=120,
                history_days=2,
                event_count=30,
                start_date="2026-09-01",
            ),
            self.ods,
        )
        build = subprocess.run(
            [
                sys.executable, str(JOBS / "build_local.py"),
                "--ods", str(self.ods), "--dws", str(self.dws), "--ads", str(self.ads),
                "--generated-at", "2026-09-14T14:30:00+08:00",
            ],
            capture_output=True, text=True, encoding="utf-8",
        )
        self.assertEqual(build.returncode, 0, build.stderr + build.stdout)

    def tearDown(self):
        self._temp.cleanup()

    def run_check(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable, str(SCRIPT),
                "--ods", str(self.ods),
                "--dws", str(self.dws),
                "--ads", str(self.ads),
                "--json",
                *extra,
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
        )

    def test_complete_local_delivery_passes_with_dwd_skipped(self):
        result = self.run_check()

        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "ready")
        self.assertEqual(report["checks"]["ods"]["status"], "ok")
        self.assertEqual(report["checks"]["dws"]["status"], "ok")
        self.assertEqual(report["checks"]["ads"]["status"], "ok")
        self.assertEqual(report["checks"]["dwd"]["status"], "skip")
        self.assertEqual(report["checks"]["reconcile"]["dwdGroup"], "skip")

    def test_missing_ads_db_fails_clearly(self):
        (self.ads / "ads.db").unlink()

        result = self.run_check()

        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "fail")
        self.assertEqual(report["checks"]["ads"]["status"], "fail")
        self.assertIn("ads.db", report["checks"]["ads"]["message"])

    def test_require_full_rejects_sample_fixture(self):
        result = self.run_check("--require-full")

        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "fail")
        self.assertEqual(report["checks"]["scale"]["status"], "fail")
        self.assertIn("ods-handoff", report["checks"]["scale"]["message"])


if __name__ == "__main__":
    unittest.main()
