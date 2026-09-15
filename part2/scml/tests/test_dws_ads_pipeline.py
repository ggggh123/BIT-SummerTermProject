"""DWS/ADS 端到端管线测试：小样例 ODS -> build_local -> reconcile 全绿。

这是 #4 交付物最重要的一条自测：**在没有 Hadoop/Spark 的机器上，用纯标准库把
「ODS 交接包」跑成「DWS + ads.db + 对账全绿」**。它覆盖：

* `warehouse/jobs/build_local.py` 的清洗与聚合；
* `warehouse/sql/ads_schema.sql` 作为唯一 DDL 真源能被正确装载；
* `warehouse/jobs/reconcile.py` 的 A/B/C 三组断言（A 内部自洽、B DWS↔ADS、
  C 从 ODS 独立重算）；
* 「契约自洽」的几条硬约束（KPI = 逐日汇总、五状态之和 = 桩数、分层用户数之和…）。

样例规模很小（2 站 / 8 桩 / 50 单），跑一次不到 2 秒，所以可以放心放进 CI。
"""

import json
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from contextlib import closing
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JOBS = ROOT / "warehouse" / "jobs"
GENERATOR = ROOT / "data_generator"

sys.path.insert(0, str(GENERATOR))
sys.path.insert(0, str(JOBS))


class PipelineEndToEndTest(unittest.TestCase):
    """整条链路只跑一次，各条断言共用产物，避免重复生成拖慢测试。"""

    @classmethod
    def setUpClass(cls):
        from generator import GenerationConfig, generate_handoff

        cls._temp = tempfile.TemporaryDirectory()
        base = Path(cls._temp.name)
        cls.ods = base / "ods"
        cls.dws = base / "dws"
        cls.ads = base / "ads"

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
            cls.ods,
        )

        result = subprocess.run(
            [
                sys.executable, str(JOBS / "build_local.py"),
                "--ods", str(cls.ods), "--dws", str(cls.dws), "--ads", str(cls.ads),
                "--generated-at", "2026-09-14T14:30:00+08:00",
            ],
            capture_output=True, text=True, encoding="utf-8",
        )
        cls.build_stdout = result.stdout
        cls.build_stderr = result.stderr
        assert result.returncode == 0, f"build_local.py 失败：{result.stderr}\n{result.stdout}"

        cls.reconcile = subprocess.run(
            [
                sys.executable, str(JOBS / "reconcile.py"),
                "--ods", str(cls.ods), "--dws", str(cls.dws),
                "--ads", str(cls.ads / "ads.db"),
            ],
            capture_output=True, text=True, encoding="utf-8",
        )
        cls.db = sqlite3.connect(f"{(cls.ads / 'ads.db').resolve().as_uri()}?mode=ro", uri=True)
        cls.db.row_factory = sqlite3.Row

    @classmethod
    def tearDownClass(cls):
        cls.db.close()
        cls._temp.cleanup()

    def scalar(self, sql: str) -> int | float:
        return self.db.execute(sql).fetchone()[0]

    # ------------------------------------------------------------ 产物完整性
    def test_build_produced_dws_and_ads(self):
        for table in ("dws_station_day", "dws_charger_day", "dws_user_day", "dws_region_day"):
            self.assertTrue((self.dws / table / "part-00000.csv").is_file(), f"缺 {table}")
        self.assertTrue((self.dws / "manifest.json").is_file())
        self.assertTrue((self.ads / "ads.db").is_file())
        self.assertTrue((self.ads / "ads_manifest.json").is_file())

    def test_dws_manifest_records_row_counts_and_hashes(self):
        manifest = json.loads((self.dws / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["kind"], "dws-handoff")
        for table in ("dws_station_day", "dws_charger_day", "dws_user_day", "dws_region_day"):
            entry = manifest["tables"][table]
            self.assertGreater(entry["rows"], 0, f"{table} 没有行")
            self.assertEqual(len(entry["sha256"]), 64)

    def test_ads_db_has_all_contract_tables(self):
        names = {
            row[0]
            for row in self.db.execute("SELECT name FROM sqlite_master WHERE type = 'table'")
        }
        self.assertEqual(
            names,
            {
                "ads_meta", "ads_station", "ads_charger", "ads_daily", "ads_station_day",
                "ads_station_hourly", "ads_user_rfm", "ads_district", "ads_quality_table",
                "ads_quality_issue", "ads_quality_meta", "ads_forecast_batch",
                "ads_forecast_24h", "ads_forecast_metric", "ads_event",
            },
        )

    # ------------------------------------------------------------ 对账
    def test_reconcile_exits_green(self):
        self.assertEqual(
            self.reconcile.returncode, 0,
            f"对账未通过：\n{self.reconcile.stdout}\n{self.reconcile.stderr}",
        )
        self.assertIn("对账全绿", self.reconcile.stdout)
        # DWD 尚未交付，D 组必须是 SKIP 而不是 FAIL 或干脆不报
        self.assertIn("DWD 订单表", self.reconcile.stdout)

    def test_reconcile_actually_checks_the_ods_recompute(self):
        """C 组（从 ODS 独立重算）必须真的跑了，不能因为路径写错静默跳过。"""
        self.assertIn("重算总营收", self.reconcile.stdout)
        self.assertIn("✓ 重算总营收", self.reconcile.stdout)

    # ------------------------------------------------------------ 口径自洽
    def test_kpi_equals_daily_rollup(self):
        meta = dict(self.db.execute("SELECT key, value FROM ads_meta"))
        self.assertEqual(
            self.scalar("SELECT SUM(revenue_fen) FROM ads_daily"), int(meta["totalRevenueFen"])
        )
        self.assertEqual(
            self.scalar("SELECT SUM(order_cnt) FROM ads_daily"), int(meta["orderCount"])
        )

    def test_station_day_matches_daily(self):
        self.assertEqual(
            self.scalar("SELECT SUM(revenue_fen) FROM ads_station_day"),
            self.scalar("SELECT SUM(revenue_fen) FROM ads_daily"),
        )

    def test_charger_state_counts_are_conserved(self):
        bad = self.scalar(
            """SELECT COUNT(1) FROM ads_station
                WHERE idle_cnt + reserved_cnt + charging_cnt + fault_cnt + restarting_cnt
                      <> charger_cnt"""
        )
        self.assertEqual(bad, 0)

    def test_fast_plus_slow_equals_charger_count(self):
        bad = self.scalar("SELECT COUNT(1) FROM ads_station WHERE fast_cnt + slow_cnt <> charger_cnt")
        self.assertEqual(bad, 0)

    def test_rfm_user_total_equals_users_with_orders(self):
        meta = dict(self.db.execute("SELECT key, value FROM ads_meta"))
        self.assertEqual(
            self.scalar("SELECT SUM(user_cnt) FROM ads_user_rfm"),
            int(meta["firstOrderUserCount"]),
        )

    def test_co2_follows_the_frozen_factor(self):
        meta = dict(self.db.execute("SELECT key, value FROM ads_meta"))
        energy = float(meta["totalEnergyKwh"])
        factor = float(meta["carbonFactorTonPerMwh"])
        expected = energy / 1000.0 * factor * 1000
        actual = self.scalar("SELECT SUM(co2_saved_kg) FROM ads_district")
        self.assertLess(abs(actual - expected), 1.0)

    def test_hourly_table_covers_every_station_hour(self):
        stations = self.scalar("SELECT COUNT(1) FROM ads_station")
        days = self.scalar("SELECT COUNT(1) FROM ads_daily")
        self.assertEqual(
            self.scalar("SELECT COUNT(1) FROM ads_station_hourly"), stations * 24 * days
        )

    def test_utilization_stays_in_range(self):
        bad = self.scalar(
            "SELECT COUNT(1) FROM ads_station_hourly WHERE utilization < 0 OR utilization > 100"
        )
        self.assertEqual(bad, 0)

    # ------------------------------------------------------------ 如实标注
    def test_forecast_batch_is_flagged_as_baseline(self):
        row = self.db.execute(
            "SELECT model_version, is_baseline, note FROM ads_forecast_batch"
        ).fetchone()
        self.assertEqual(row["is_baseline"], 1)
        self.assertEqual(row["model_version"], "seasonal-naive-baseline")
        self.assertIn("降级", row["note"])

    def test_reconcile_accepts_real_forecast_batch(self):
        copied_ads = Path(self._temp.name) / "ads_real_forecast"
        shutil.copytree(self.ads, copied_ads)
        db_path = copied_ads / "ads.db"
        with closing(sqlite3.connect(db_path)) as db:
            db.execute(
                "UPDATE ads_forecast_batch SET is_baseline = 0, model_version = ?",
                ("mllib-gbt-v1",),
            )
            db.commit()
        result = subprocess.run(
            [
                sys.executable, str(JOBS / "reconcile.py"),
                "--ods", str(self.ods), "--dws", str(self.dws),
                "--ads", str(db_path),
            ],
            capture_output=True, text=True, encoding="utf-8",
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)

    def test_build_local_can_replace_baseline_with_forecast_handoff(self):
        from build_local import build

        forecast_dir = Path(self._temp.name) / "forecast_handoff"
        forecast_dir.mkdir()
        (forecast_dir / "manifest.json").write_text(
            json.dumps(
                {
                    "package": "forecast",
                    "run_id": "ml-run-local",
                    "generated_at": "2026-09-14T11:00:00",
                    "source": "hdfs:///ev-charging/ads/ads_forecast_result",
                    "rows": 48,
                },
                ensure_ascii=False,
            ),
            encoding="utf-8",
        )
        (forecast_dir / "forecast_result.json").write_text(
            json.dumps(
                [
                    {
                        "run_id": "ml-run-local",
                        "station_id": station_id,
                        "forecast_at": f"2026-09-03T{hour:02d}:00:00+08:00",
                        "horizon_h": hour + 1,
                        "predicted_load_kw": 50 + station_id + hour,
                        "predicted_busy_count": 2,
                        "predicted_idle_count": 2,
                        "congestion_level": "medium",
                        "is_peak": hour in (17, 18),
                    }
                    for station_id in (1, 2)
                    for hour in range(24)
                ],
                ensure_ascii=False,
            ),
            encoding="utf-8",
        )
        (forecast_dir / "metrics.json").write_text(
            json.dumps({"model_version": "mllib-gbt-v1", "h1": {"wape": 0.176}}),
            encoding="utf-8",
        )

        copied_ads = Path(self._temp.name) / "ads_with_ml_forecast"
        build(self.ods, Path(self._temp.name) / "dws_with_ml_forecast", copied_ads,
              forecast_handoff=forecast_dir)

        with closing(sqlite3.connect(copied_ads / "ads.db")) as db:
            db.row_factory = sqlite3.Row
            batch = db.execute(
                "SELECT run_id, model_version, is_baseline FROM ads_forecast_batch"
            ).fetchone()
            forecast_count = db.execute("SELECT COUNT(1) FROM ads_forecast_24h").fetchone()[0]
        self.assertEqual(batch["run_id"], "ml-run-local")
        self.assertEqual(batch["model_version"], "mllib-gbt-v1")
        self.assertEqual(batch["is_baseline"], 0)
        self.assertEqual(forecast_count, 48)

    def test_meta_carries_the_assumption_notes(self):
        meta = dict(self.db.execute("SELECT key, value FROM ads_meta"))
        for key in (
            "cleaningSource", "populationSource", "serviceRadiusNote",
            "avgWaitNote", "newUserNote", "faultNote", "carbonFactorNote",
        ):
            self.assertIn(key, meta, f"ads_meta 缺少如实标注字段 {key}")

    def test_quality_rules_are_recorded_even_when_injection_log_lacks_summary(self):
        """小样例的 injection_log 没有 summary 汇总块，注入量仍须非零。"""
        rows = {row["rule"]: row for row in self.db.execute("SELECT * FROM ads_quality_issue")}
        self.assertEqual(len(rows), 10)
        self.assertGreater(sum(row["injected"] for row in rows.values()), 0)

    # ------------------------------------------------------------ 清洗生效
    def test_dirty_rows_are_dropped_not_silently_kept(self):
        table = {row["name"]: row for row in self.db.execute("SELECT * FROM ads_quality_table")}
        self.assertLess(table["ods_orders"]["rows_after"], table["ods_orders"]["rows_before"])
        self.assertLessEqual(
            table["ods_telemetry"]["rows_after"], table["ods_telemetry"]["rows_before"]
        )

    def test_district_population_is_annotated_as_external(self):
        meta = dict(self.db.execute("SELECT key, value FROM ads_meta"))
        self.assertIn("人口普查", meta["populationSource"])


if __name__ == "__main__":
    unittest.main()
