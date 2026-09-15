"""从自测数据中人为挖出整点缺口，用于验证 #5 的缺口防护（对照实验）。

正式批次的 dwd_station_hourly 存在 47 个缺失小时（PRL 证据
``hourlyCoverage.rowOffsetMlSafe=false``），但本机自测数据是连续的。
本脚本在最小区间内注入**可预期的**缺口，使 `verify_coverage.py` 与
`align_hourly_grid()` 的行为可以在本地被精确复现与断言。

用法：
    spark-submit --master yarn part2/tmp/make_gapped.py \\
        --src hdfs://pangxiangzhen01:8020/ev-charging/tmp/dwd_station_hourly_synth \\
        --out hdfs://pangxiangzhen01:8020/ev-charging/tmp/dwd_station_hourly_gapped
"""

from __future__ import annotations

import argparse

from pyspark.sql import SparkSession
from pyspark.sql import functions as F

#: 注入的缺口：(station_id, 起始整点, 连续缺失小时数)
PLANNED_GAPS: tuple[tuple[int, str, int], ...] = (
    (1, "2026-07-01 00:00:00", 3),   # 连续 3 小时，最长缺口
    (2, "2026-07-15 08:00:00", 1),
    (3, "2026-08-01 12:00:00", 1),
    (4, "2026-08-10 18:00:00", 1),
)


def gap_condition():
    """把计划缺口转成一条过滤条件（保留条件取反即为「挖洞后的数据」）。"""
    ts = F.to_timestamp("observed_at")
    condition = None
    for station_id, start, hours in PLANNED_GAPS:
        begin = F.lit(start).cast("timestamp")
        end = begin + F.expr(f"INTERVAL {hours} HOURS")
        piece = (F.col("station_id") == F.lit(station_id)) & (ts >= begin) & (ts < end)
        condition = piece if condition is None else (condition | piece)
    return condition


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", required=True, help="连续的自测 dwd_station_hourly")
    ap.add_argument("--out", required=True, help="输出路径（会覆盖）")
    args = ap.parse_args()

    spark = SparkSession.builder.appName("part2-make-gapped").getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")

    source = spark.read.parquet(args.src)
    expected_removed = sum(hours for _, _, hours in PLANNED_GAPS)
    before = source.count()
    kept = source.filter(~gap_condition())
    after = kept.count()
    removed = before - after
    if removed != expected_removed:
        raise ValueError(f"缺口注入数量不符：removed={removed} expected={expected_removed}")

    kept.write.mode("overwrite").parquet(args.out)
    print(f"[gapped] before={before} after={after} removed={removed} -> {args.out}")
    for station_id, start, hours in PLANNED_GAPS:
        print(f"[gapped] station={station_id} start={start} hours={hours}")
    print("GAPPED_DONE")
    spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
