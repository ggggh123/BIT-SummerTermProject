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
            # 显式关闭：Windows 上未关闭的连接会让 TemporaryDirectory 清理失败
            connection = sqlite3.connect(db_path)
            try:
                connection.execute("CREATE TABLE marker(value INTEGER)")
                connection.commit()
            finally:
                connection.close()
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

    def test_forecast_is_baseline_follows_batch_flag_not_batch_presence(self):
        """存在批次 ≠ 基线：只有 is_baseline=1 才写 isBaseline=true，与 build_local.py 同口径。

        回归背景：原实现写 bool(batch)，导致真实 MLlib 批次（is_baseline=0）的 ads_manifest.json
        标成 isBaseline=true，与 ads_forecast_batch / 接口返回的 false 自相矛盾。
        """
        with tempfile.TemporaryDirectory() as temporary:
            db_path = Path(temporary) / "ads.db"
            # 显式关闭：Windows 上未关闭的连接会让 TemporaryDirectory 清理失败
            connection = sqlite3.connect(db_path)
            try:
                connection.execute("CREATE TABLE marker(value INTEGER)")
                connection.commit()
            finally:
                connection.close()
            common = dict(
                db_path=db_path,
                payload={"ads_forecast_24h": [{"horizon_h": 1}]},
                meta={
                    "runId": "ads-1",
                    "generatedAt": "2026-09-15T23:56:03+08:00",
                    "sourceRunId": "scml-20260914",
                },
                quality_issue=[],
                data_window={"start": "2026-06-17", "end": "2026-09-14", "days": 90},
            )
            real_ml = build_ads_manifest(
                **common, batch={"model_version": "gbt-3.5.7", "is_baseline": 0}
            )
            self.assertEqual(
                real_ml["forecast"], {"source": "gbt-3.5.7", "isBaseline": False}
            )
            degraded = build_ads_manifest(
                **common, batch={"model_version": "seasonal-naive", "is_baseline": 1}
            )
            self.assertEqual(
                degraded["forecast"], {"source": "seasonal-naive", "isBaseline": True}
            )
            self.assertEqual(build_ads_manifest(**common, batch=None)["forecast"],
                             {"source": "none", "isBaseline": False})


if __name__ == "__main__":
    unittest.main()
