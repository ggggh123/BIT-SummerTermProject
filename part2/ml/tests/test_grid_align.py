"""缺口防护回归：整点网格对齐、维度回填与特征可用性解析。

覆盖《03》未冻结事项中的「小时缺口策略」：#5 需要证明
``dwd_station_hourly`` 存在缺失整点时，行偏移特征不会静默错位。
"""

from __future__ import annotations

import unittest

from pyspark.sql import SparkSession
from pyspark.sql import functions as F
from pyspark.sql.types import (
    DoubleType,
    IntegerType,
    StringType,
    StructField,
    StructType,
)

from part2.ml import features as ft


class GridAlignTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spark = (
            SparkSession.builder.master("local[1]")
            .appName("part2-ml-grid-align-tests")
            .config("spark.ui.enabled", "false")
            .getOrCreate()
        )
        cls.spark.sparkContext.setLogLevel("ERROR")

    @classmethod
    def tearDownClass(cls):
        cls.spark.stop()

    def _frame(self, rows):
        """构造 dwd_station_hourly 的三个必要列，时间列按契约以字符串传入。"""
        schema = StructType(
            [
                StructField("station_id", IntegerType()),
                StructField("observed_at", StringType()),
                StructField("pile_count", IntegerType()),
                StructField("load_kw", DoubleType()),
                StructField("busy_count", IntegerType()),
                StructField("temperature_c", DoubleType()),
            ]
        )
        # read_hourly 会为缺失的可选列补默认值；这里直接构造 DataFrame，需同样补齐
        return (
            self.spark.createDataFrame(rows, schema)
            .withColumn("observed_at", F.to_timestamp("observed_at"))
            .withColumn("is_holiday", F.lit(False))
        )

    def _gapped(self):
        """站点 1 的 4 小时序列，缺 02:00；桩数为站点常数 10。"""
        return self._frame(
            [
                (1, "2026-06-01 00:00:00", 10, 1.0, 1, 20.0),
                (1, "2026-06-01 01:00:00", 10, 2.0, 2, 21.0),
                (1, "2026-06-01 03:00:00", 10, 4.0, 4, 23.0),
            ]
        )

    def test_align_fills_missing_hour(self):
        aligned = ft.align_hourly_grid(self._gapped())
        self.assertEqual(aligned.count(), 4)
        self.assertEqual(
            [row.observed_at.strftime("%H:%M") for row in aligned.orderBy("observed_at").collect()],
            ["00:00", "01:00", "02:00", "03:00"],
        )

    def test_aligned_gap_row_facts_are_null(self):
        """缺口行必须显式表现为 NULL，而不是伪造观测。"""
        row = (
            ft.align_hourly_grid(self._gapped())
            .filter(F.hour("observed_at") == 2)
            .first()
        )
        self.assertIsNone(row["load_kw"])
        self.assertIsNone(row["busy_count"])
        self.assertIsNone(row["temperature_c"])

    def test_station_dimension_is_backfilled(self):
        """桩数是站点缓变维度，缺口行也要能取到，否则物理约束会失真。"""
        row = (
            ft.align_hourly_grid(self._gapped())
            .filter(F.hour("observed_at") == 2)
            .first()
        )
        self.assertEqual(row["pile_count"], 10)

    def test_lag_stops_at_gap_instead_of_skipping_it(self):
        """核心断言：行偏移特征在缺口处必须停止，而不是跨过缺口取「上一行」。

        未对齐时 03:00 的 ``lag_1h`` 会取到 01:00 的值（实际是 2 小时前的观测），
        这类错位不会抛异常，只会让指标虚高。对齐后缺口成为真实的一行，
        02:00 的 ``lag_1h`` 才是真正的「1 小时前」，而 03:00 因缺口行事实为 NULL
        而拿不到滞后值，交由训练侧 ``dropna`` / ``handleInvalid="skip"`` 丢弃。
        """
        gapped = self._gapped()

        before = {
            row["observed_at"].strftime("%H:%M"): row
            for row in ft.build_features(gapped, horizons=(1,)).collect()
        }
        self.assertEqual(before["03:00"]["lag_1h"], 2.0)  # 跨过缺口 -> 错位

        after = {
            row["observed_at"].strftime("%H:%M"): row
            for row in ft.build_features(
                ft.align_hourly_grid(gapped), horizons=(1,)
            ).collect()
        }
        self.assertEqual(after["02:00"]["lag_1h"], 2.0)  # 真正的 1 小时前
        self.assertIsNone(after["03:00"]["lag_1h"])  # 缺口未提供可用的滞后值

    def test_label_is_not_shifted_by_gap(self):
        """标签侧同样依赖行偏移：对齐后 h=1 的标签必须来自真实下一小时。

        未对齐时 01:00 的 ``y_load_h1`` 会取到 03:00 的值（实为 2 小时后），
        属于典型的静默标签错位 —— 指标会虚高且没有任何异常提示。
        """
        gapped = self._gapped()
        before = {
            row["observed_at"].strftime("%H:%M"): row
            for row in ft.build_features(gapped, horizons=(1,)).collect()
        }
        self.assertEqual(before["01:00"]["y_load_h1"], 4.0)  # 跨过缺口 -> 错位

        after = {
            row["observed_at"].strftime("%H:%M"): row
            for row in ft.build_features(
                ft.align_hourly_grid(gapped), horizons=(1,)
            ).collect()
        }
        self.assertEqual(after["00:00"]["y_load_h1"], 2.0)  # 真实的 1 小时后
        self.assertIsNone(after["01:00"]["y_load_h1"])  # 目标小时本身是缺口

    def test_resolve_feature_cols_drops_all_null_column(self):
        """整列为空时必须剔除，否则 VectorAssembler 会跳过全部样本。"""
        frame = self._frame(
            [
                (1, "2026-06-01 00:00:00", 10, 1.0, 1, None),
                (1, "2026-06-01 01:00:00", 10, 2.0, 2, None),
            ]
        )
        frame = ft.build_features(frame, horizons=(1,))
        kept, dropped = ft.resolve_feature_cols(frame)
        self.assertIn("temperature_c", dropped)
        self.assertNotIn("temperature_c", kept)
        self.assertIn("lag_1h", kept)

    def test_fill_missing_features_backfills_temperature(self):
        frame = self._frame(
            [
                (1, "2026-06-01 00:00:00", 10, 1.0, 1, 20.0),
                (1, "2026-06-01 01:00:00", 10, 2.0, 2, None),
                (2, "2026-06-01 00:00:00", 10, 3.0, 3, None),
            ]
        )
        filled, stats = ft.fill_missing_features(frame)
        self.assertEqual(stats["temperature_c"]["missing_rows"], 2)
        self.assertEqual(
            filled.filter(F.col("temperature_c").isNull()).count(), 0
        )
        # 站点 2 自身无观测，只能落到全局均值
        self.assertEqual(
            filled.filter(F.col("station_id") == 2).first()["temperature_c"], 20.0
        )


if __name__ == "__main__":
    unittest.main()
