"""ML 预测发布到 ADS 的事务与契约回归。"""

from __future__ import annotations

import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

from part2.ml.merge_ads import merge


class MergeAdsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ml-merge-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.forecast = self.root / "forecast.db"
        self.ads = self.root / "ads.db"
        self.backup = self.root / "ads.before-ml.db"
        self.manifest = self.root / "ads_manifest.json"
        with sqlite3.connect(self.ads) as connection:
            connection.executescript("""
CREATE TABLE ads_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE ads_station(station_id INTEGER PRIMARY KEY,charger_cnt INTEGER NOT NULL,forecast_enabled INTEGER NOT NULL);
CREATE TABLE ads_forecast_batch(run_id TEXT PRIMARY KEY,model_version TEXT NOT NULL,activated_at TEXT NOT NULL,source TEXT NOT NULL,horizon_h_max INTEGER NOT NULL,is_baseline INTEGER NOT NULL,note TEXT);
CREATE TABLE ads_forecast_24h(run_id TEXT NOT NULL,station_id INTEGER NOT NULL,forecast_at TEXT NOT NULL,horizon_h INTEGER NOT NULL,predicted_load_kw REAL NOT NULL,predicted_busy_count INTEGER NOT NULL,predicted_idle_count INTEGER NOT NULL,congestion_level TEXT NOT NULL,is_peak INTEGER NOT NULL,PRIMARY KEY(run_id,station_id,horizon_h));
CREATE TABLE ads_forecast_metric(horizon_h INTEGER PRIMARY KEY,mae REAL NOT NULL,rmse REAL NOT NULL,wape REAL NOT NULL,baseline_wape REAL NOT NULL);
INSERT INTO ads_meta VALUES('sourceRunId','source-1');
INSERT INTO ads_station VALUES(1,4,1);
INSERT INTO ads_forecast_batch VALUES('old','old','2026-01-01','baseline',24,1,'降级');
INSERT INTO ads_forecast_24h VALUES('old',1,'2026-01-01T01:00:00+08:00',1,1,1,3,'low',0);
INSERT INTO ads_forecast_metric VALUES(1,1,1,1,2);
""")
        with sqlite3.connect(self.forecast) as connection:
            connection.executescript("""
CREATE TABLE ads_forecast_24h(run_id TEXT,station_id INTEGER,forecast_at TEXT,horizon_h INTEGER,predicted_load_kw REAL,predicted_busy_count INTEGER,predicted_idle_count INTEGER,congestion_level TEXT,is_peak INTEGER);
CREATE TABLE ads_forecast_metric(horizon_h INTEGER,mae REAL,rmse REAL,wape REAL,baseline_wape REAL);
""")
            connection.executemany(
                "INSERT INTO ads_forecast_24h VALUES (?,?,?,?,?,?,?,?,?)",
                [
                    ("ml-1", 1, f"2026-09-16T{hour:02d}:00:00+08:00", hour, float(hour), 2, 2, "medium", int(hour in (7, 8)))
                    for hour in range(1, 25)
                ],
            )
            connection.executemany(
                "INSERT INTO ads_forecast_metric VALUES (?,?,?,?,?)",
                [(hour, 1.0, 2.0, 3.0, 4.0) for hour in range(1, 25)],
            )
        self.manifest.write_text(json.dumps({
            "contractVersion": "ads-flask-v1",
            "sourceRunId": "source-1",
            "database": "ads.db",
            "tables": {},
        }), encoding="utf-8")

    def test_merge_replaces_forecast_and_keeps_backup(self):
        result = merge(
            forecast_db=self.forecast,
            ads_db=self.ads,
            ads_manifest=self.manifest,
            backup=self.backup,
            model_version="test-model",
        )
        self.assertTrue(result["ok"])
        with sqlite3.connect(self.ads) as connection:
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM ads_forecast_24h").fetchone()[0], 24)
            self.assertEqual(connection.execute("SELECT is_baseline FROM ads_forecast_batch").fetchone()[0], 0)
            self.assertEqual(dict(connection.execute("SELECT key,value FROM ads_meta"))["forecastRunId"], "ml-1")
        with sqlite3.connect(self.backup) as connection:
            self.assertEqual(connection.execute("SELECT run_id FROM ads_forecast_batch").fetchone()[0], "old")
        manifest = json.loads(self.manifest.read_text(encoding="utf-8"))
        self.assertFalse(manifest["forecast"]["isBaseline"])
        self.assertEqual(manifest["tables"]["ads_forecast_24h"], 24)

    def test_non_contiguous_peak_is_rejected_before_backup(self):
        with sqlite3.connect(self.forecast) as connection:
            connection.execute("UPDATE ads_forecast_24h SET is_peak=0")
            connection.execute("UPDATE ads_forecast_24h SET is_peak=1 WHERE horizon_h IN (7,9)")
        with self.assertRaisesRegex(ValueError, "连续两个峰值"):
            merge(
                forecast_db=self.forecast,
                ads_db=self.ads,
                ads_manifest=self.manifest,
                backup=self.backup,
                model_version="test-model",
            )
        self.assertFalse(self.backup.exists())

    def test_forecast_station_set_must_match_ads(self):
        with sqlite3.connect(self.forecast) as connection:
            connection.execute("UPDATE ads_forecast_24h SET station_id=2")
        with self.assertRaisesRegex(ValueError, "站点集合"):
            merge(
                forecast_db=self.forecast,
                ads_db=self.ads,
                ads_manifest=self.manifest,
                backup=self.backup,
                model_version="test-model",
            )


if __name__ == "__main__":
    unittest.main()
