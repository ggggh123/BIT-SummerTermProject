"""Spark ADS 导出端的交接清单回归（不启动 Spark）。"""

from __future__ import annotations

import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

JOBS = Path(__file__).resolve().parents[1] / "warehouse" / "jobs"
sys.path.insert(0, str(JOBS))

from export_ads_db import build_ads_manifest  # noqa: E402


class SparkExportManifestTest(unittest.TestCase):
    def test_manifest_carries_contract_counts_hash_and_source_batch(self):
        with tempfile.TemporaryDirectory() as temporary:
            db_path = Path(temporary) / "ads.db"
            with sqlite3.connect(db_path) as connection:
                connection.execute("CREATE TABLE marker(value INTEGER)")
            payload = {
                "ads_station": [{"station_id": 1}],
                "ads_quality_table": [{"name": "ods_orders"}],
                "ads_quality_issue": [
                    {"rule": "R01", "injected": 3, "detected": 2}
                ],
                "ads_forecast_24h": [{"horizon_h": 1}],
            }
            manifest = build_ads_manifest(
                db_path=db_path,
                payload=payload,
                meta={
                    "runId": "ads-1",
                    "generatedAt": "2026-09-15T10:55:00+08:00",
                    "sourceRunId": "ods-1",
                },
                quality_issue=payload["ads_quality_issue"],
                batch={"model_version": "seasonal-naive"},
                data_window={"start": "2026-09-01", "end": "2026-09-15", "days": 15},
            )
            self.assertEqual(manifest["contractVersion"], "ads-flask-v1")
            self.assertEqual(manifest["sourceRunId"], "ods-1")
            self.assertEqual(manifest["tables"]["ads_station"], 1)
            self.assertEqual(manifest["quality"], {"injected": 3, "detected": 2})
            self.assertEqual(len(manifest["databaseSha256"]), 64)


if __name__ == "__main__":
    unittest.main()
