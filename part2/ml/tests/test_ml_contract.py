"""预测参数与峰值标记的轻量回归。"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from pyspark.sql import SparkSession

from part2.ml.predict import load_selection, mark_peak_hours
from part2.ml.train import parse_horizons


class MlContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spark = (
            SparkSession.builder.master("local[1]")
            .appName("part2-ml-contract-tests")
            .config("spark.ui.enabled", "false")
            .getOrCreate()
        )
        cls.spark.sparkContext.setLogLevel("ERROR")

    @classmethod
    def tearDownClass(cls):
        cls.spark.stop()

    def test_horizon_expression_is_sorted_and_deduplicated(self):
        self.assertEqual(parse_horizons("6,1-3,2,24"), [1, 2, 3, 6, 24])

    def test_selection_loads_model_and_naive_entries(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "metrics.json"
            path.write_text(json.dumps({
                "horizons": {
                    "h1": {"load": {"selected": {"algo": "gbt", "model_path": "/m/1"}}},
                    "h2": {"busy": {"selected": {"algo": "naive"}}},
                }
            }), encoding="utf-8")
            selected = load_selection(str(path), "gbt")
            self.assertEqual(selected[(1, "load")], ("gbt", "/m/1"))
            self.assertEqual(selected[(2, "busy")], ("naive", None))

    def test_peak_is_exactly_best_complete_two_hour_window(self):
        rows = [
            (station, horizon, float(load))
            for station, loads in ((1, [1, 9, 9, 1]), (2, [5, 5, 0, 9]))
            for horizon, load in enumerate(loads, start=1)
        ]
        frame = self.spark.createDataFrame(
            rows, ("station_id", "horizon_h", "predicted_load_kw")
        )
        result = mark_peak_hours(frame, 4).orderBy("station_id", "horizon_h").collect()
        peaks = {
            station: [row.horizon_h for row in result if row.station_id == station and row.is_peak]
            for station in (1, 2)
        }
        self.assertEqual(peaks[1], [2, 3])
        # h=4 的单点 9 不构成完整窗口，最佳完整窗口是 h1+h2。
        self.assertEqual(peaks[2], [1, 2])


if __name__ == "__main__":
    unittest.main()
