"""#5 预测交接包归一测试。

ADS 侧的契约是三张表：ads_forecast_batch / ads_forecast_24h /
ads_forecast_metric。#5 可能交付显式三文件格式，也可能交付 publish.py 当前生成的
manifest.json + forecast_result.json + metrics.json。这里固定 #4 应该接受的边界。
"""

import json
import sys
import tempfile
import unittest
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JOBS = ROOT / "warehouse" / "jobs"
sys.path.insert(0, str(JOBS))

import _ads_extras as extras  # noqa: E402


class ForecastHandoffTest(unittest.TestCase):
    def test_loads_explicit_forecast_handoff_tables(self):
        with tempfile.TemporaryDirectory() as tmp:
            handoff = Path(tmp)
            (handoff / "forecast_batch.json").write_text(
                json.dumps(
                    {
                        "run_id": "ml-run-001",
                        "model_version": "mllib-gbt-v1",
                        "activated_at": "2026-09-14T10:00:00+08:00",
                        "source": "part2/ml",
                        "horizon_h_max": 24,
                        "is_baseline": 0,
                        "note": "real model",
                    },
                    ensure_ascii=False,
                ),
                encoding="utf-8",
            )
            (handoff / "forecast_result.json").write_text(
                json.dumps(
                    [
                        {
                            "station_id": 1,
                            "forecast_at": "2026-09-15T00:00:00+08:00",
                            "horizon_h": 1,
                            "predicted_load_kw": 88.5,
                            "predicted_busy_count": 3,
                            "predicted_idle_count": 33,
                            "congestion_level": "low",
                            "is_peak": False,
                        }
                    ],
                    ensure_ascii=False,
                ),
                encoding="utf-8",
            )
            (handoff / "forecast_metric.json").write_text(
                json.dumps(
                    [{"horizon_h": 1, "mae": 9.1, "rmse": 12.3, "wape": 17.6, "baseline_wape": 67.6}],
                    ensure_ascii=False,
                ),
                encoding="utf-8",
            )

            batch, points, metrics = extras.load_forecast_handoff(
                handoff, datetime.fromisoformat("2026-09-14T12:00:00+08:00")
            )

        self.assertEqual(batch["run_id"], "ml-run-001")
        self.assertEqual(batch["is_baseline"], 0)
        self.assertEqual(points[0]["is_peak"], 0)
        self.assertEqual(metrics[0]["wape"], 17.6)

    def test_loads_publish_py_forecast_handoff_shape(self):
        with tempfile.TemporaryDirectory() as tmp:
            handoff = Path(tmp)
            (handoff / "manifest.json").write_text(
                json.dumps(
                    {
                        "package": "forecast",
                        "run_id": "ml-run-002",
                        "generated_at": "2026-09-14T11:00:00",
                        "source": "hdfs:///ev-charging/ads/ads_forecast_result",
                        "rows": 144,
                    },
                    ensure_ascii=False,
                ),
                encoding="utf-8",
            )
            (handoff / "forecast_result.json").write_text(
                json.dumps(
                    [
                        {
                            "run_id": "ml-run-002",
                            "station_id": 2,
                            "forecast_at": "2026-09-15T01:00:00+08:00",
                            "horizon_h": 2,
                            "predicted_load_kw": 101.25,
                            "predicted_busy_count": 5,
                            "predicted_idle_count": 31,
                            "congestion_level": "medium",
                            "is_peak": 1,
                        }
                    ],
                    ensure_ascii=False,
                ),
                encoding="utf-8",
            )
            (handoff / "metrics.json").write_text(
                json.dumps(
                    {
                        "model_version": "mllib-gbt-v1",
                        "h1": {"mae": 8.5, "rmse": 11.0, "wape": 0.176, "baseline_wape": 0.676},
                    },
                    ensure_ascii=False,
                ),
                encoding="utf-8",
            )

            batch, points, metrics = extras.load_forecast_handoff(
                handoff, datetime.fromisoformat("2026-09-14T12:00:00+08:00")
            )

        self.assertEqual(batch["run_id"], "ml-run-002")
        self.assertEqual(batch["model_version"], "mllib-gbt-v1")
        self.assertEqual(batch["is_baseline"], 0)
        self.assertEqual(batch["horizon_h_max"], 2)
        self.assertEqual(points[0]["run_id"], "ml-run-002")
        self.assertEqual(metrics[0]["horizon_h"], 1)
        self.assertEqual(metrics[0]["wape"], 17.6)
        self.assertEqual(metrics[0]["baseline_wape"], 67.6)


if __name__ == "__main__":
    unittest.main()
