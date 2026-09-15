"""SCML 交付体检脚本测试。"""

from __future__ import annotations

import json
import sqlite3
import csv
import subprocess
import sys
import tempfile
import unittest
from contextlib import closing
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
            capture_output=True, text=True, encoding="utf-8", errors="replace",
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
            errors="replace",
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

    def test_orphan_forecast_point_fails_scale_check(self):
        with closing(sqlite3.connect(self.ads / "ads.db")) as connection:
            connection.execute(
                "UPDATE ads_forecast_24h SET station_id=99999 "
                "WHERE rowid=(SELECT MIN(rowid) FROM ads_forecast_24h)"
            )
            connection.commit()
        ods_manifest = json.loads((self.ods / "manifest.json").read_text(encoding="utf-8"))
        ods_manifest["kind"] = "ods-handoff"
        (self.ods / "manifest.json").write_text(json.dumps(ods_manifest), encoding="utf-8")

        result = self.run_check("--require-full")

        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertEqual(report["checks"]["scale"]["status"], "fail")
        self.assertEqual(report["checks"]["scale"]["orphanForecastPointCount"], 1)

    def test_delivery_check_passes_hour_gap_allowance_to_reconcile(self):
        with closing(sqlite3.connect(self.ads / "ads.db")) as connection:
            connection.execute(
                "DELETE FROM ads_station_hourly WHERE rowid="
                "(SELECT rowid FROM ads_station_hourly ORDER BY dt, station_id, hour LIMIT 1)"
            )
            connection.commit()

        result = self.run_check("--allow-missing-hours", "1")

        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "ready")
        self.assertEqual(report["checks"]["reconcile"]["status"], "ok")

    def test_require_full_rejects_sample_fixture(self):
        result = self.run_check("--require-full")

        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "fail")
        self.assertEqual(report["checks"]["scale"]["status"], "fail")
        self.assertIn("ods-handoff", report["checks"]["scale"]["message"])

    def test_formal_dwd_uses_nested_manifest_layout_and_canonical_cleaning(self):
        dwd = self.ods.parent / "dwd-handoff"
        orders = dwd / "dwd" / "dwd_order_detail" / "dt=2026-09-01"
        reports = dwd / "reports"
        orders.mkdir(parents=True)
        reports.mkdir(parents=True)

        dws_station = self.dws / "dws_station_day" / "part-00000.csv"
        with dws_station.open(encoding="utf-8", newline="") as source:
            rows = list(csv.DictReader(source))
        order_count = sum(int(row["order_cnt"]) for row in rows)
        revenue = sum(int(row["revenue_fen"]) for row in rows)
        with (orders / "part-00000.csv").open("w", encoding="utf-8", newline="") as target:
            writer = csv.DictWriter(target, fieldnames=("status", "amount_fen"))
            writer.writeheader()
            for index in range(order_count):
                writer.writerow({"status": "completed", "amount_fen": revenue if index == 0 else 0})

        ods_manifest = json.loads((self.ods / "manifest.json").read_text(encoding="utf-8"))
        source_run_id = ods_manifest["runId"]
        table_rows = {
            "dwd_order_detail": order_count,
            "dim_chargers": sqlite_count(self.ads / "ads.db", "ads_charger"),
        }
        (dwd / "manifest.json").write_text(json.dumps({
            "source_run_id": source_run_id,
            "table_rows": table_rows,
        }), encoding="utf-8")
        tables = {
            name.removeprefix("ods_"): {"before": entry["rows"]}
            for name, entry in ods_manifest["tables"].items()
        }
        (reports / "cleaning_report.json").write_text(json.dumps({
            "tables": tables,
            "dwd_assertions": [{"table": str(index), "ok": True} for index in range(7)],
        }), encoding="utf-8")

        result = self.run_check("--dwd", str(dwd), "--require-dwd")

        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "ready")
        self.assertEqual(report["checks"]["reconcile"]["dwdGroup"], "ok")


def sqlite_count(database: Path, table: str) -> int:
    with closing(sqlite3.connect(database)) as connection:
        return int(connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0])


if __name__ == "__main__":
    unittest.main()
