"""DWD 小时级覆盖核验：判定 ``dwd_station_hourly`` 是否满足「按行偏移」前提。

背景
----
PRL 交接说明与交付证据（``docs/evidence/2026-09-15-formal-full.json``）明确给出
``hourlyCoverage.rowOffsetMlSafe = false``，并注明：

    小时缺口不能直接用于按行偏移的 ML 特征。

而本模块的特征工程（``features.py``）大量使用行偏移语义 —— ``lag(n)``、``lead(n)``、
``rowsBetween(-N, -1)``。**当站点序列中间缺少整点小时时，行偏移不再等价于时间偏移**：
``lag_24h`` 会静默取到「第 24 行之前」的值而不是「24 小时前」的值，``lead(h)`` 生成的
标签同理错位。这类错误不会抛异常，只会让指标虚高，因此必须在训练之前显式核验。

本脚本输出机器可读报告，作为：

* #5 训练门禁 —— ``row_offset_safe=false`` 时不得直接进入特征工程，须先做网格对齐；
* #2/#4/#5 冻结「小时缺口策略」的事实依据（《03》§未冻结事项）。

用法
----
    spark-submit part2/ml/verify_coverage.py \\
        --dwd hdfs://pangxiangzhen01:8020/ev-charging/dwd/dwd_station_hourly \\
        --report ./tmp/coverage_report.json
"""

from __future__ import annotations

import argparse
import datetime
import json
from zoneinfo import ZoneInfo

from pyspark.sql import DataFrame, SparkSession, Window
from pyspark.sql import functions as F

SHANGHAI = ZoneInfo("Asia/Shanghai")

#: 期望的小时步长（秒）。非整点或非 1 小时步长都会被计入缺口统计。
HOUR_SECONDS = 3600


def load_hour_index(df: DataFrame) -> DataFrame:
    """抽取「站点 × 整点」去重索引，非法行直接丢弃（上游已由 PRL 隔离）。"""
    return (
        df.select("station_id", "observed_at")
        .distinct()
        .withColumn("station_id", F.col("station_id").cast("int"))
        .withColumn("ts", F.to_timestamp("observed_at"))
        .filter(F.col("station_id").isNotNull() & F.col("ts").isNotNull())
    )


def station_span(index: DataFrame) -> DataFrame:
    """每站的实际覆盖区间、应有小时数与缺失小时数。"""
    return (
        index.groupBy("station_id")
        .agg(
            F.min("ts").alias("first_ts"),
            F.max("ts").alias("last_ts"),
            F.countDistinct("ts").alias("actual_hours"),
        )
        .withColumn(
            "expected_hours",
            (F.unix_timestamp("last_ts") - F.unix_timestamp("first_ts")) / F.lit(HOUR_SECONDS)
            + 1,
        )
        .withColumn("missing_hours", F.col("expected_hours") - F.col("actual_hours"))
    )


def missing_points(span: DataFrame, index: DataFrame) -> DataFrame:
    """用完整小时网格反查缺失的整点（left_anti join）。"""
    grid = span.select(
        "station_id",
        "first_ts",
        "last_ts",
        F.explode(F.sequence("first_ts", "last_ts", F.expr("INTERVAL 1 HOUR"))).alias("ts"),
    )
    have = index.select(
        F.col("station_id").alias("have_station"), F.col("ts").alias("have_ts")
    )
    return (
        grid.join(
            have,
            (grid["station_id"] == have["have_station"]) & (grid["ts"] == have["have_ts"]),
            "left_anti",
        )
        .select("station_id", "ts")
        .orderBy("station_id", "ts")
    )


def gap_runs(missing: DataFrame) -> DataFrame:
    """把缺失点聚成连续缺口段，得到每段长度与每站最长缺口。"""
    w = Window.partitionBy("station_id").orderBy("ts")
    marked = (
        missing.withColumn("prev_ts", F.lag("ts").over(w))
        .withColumn(
            "is_run_start",
            F.when(
                F.col("prev_ts").isNull()
                | (
                    (F.unix_timestamp("ts") - F.unix_timestamp("prev_ts"))
                    > F.lit(HOUR_SECONDS)
                ),
                1,
            ).otherwise(0),
        )
        .withColumn("run_id", F.sum("is_run_start").over(w))
    )
    return marked.groupBy("station_id", "run_id").agg(
        F.count("*").alias("gap_len"),
        F.min("ts").alias("gap_start"),
        F.max("ts").alias("gap_end"),
    )


def _fmt(value) -> str | None:
    return None if value is None else value.strftime("%Y-%m-%d %H:%M")


def build_report(dwd: str, span: DataFrame, runs: DataFrame, missing: DataFrame,
                 tolerance: int, detail_limit: int) -> dict:
    """汇总机器可读报告。判定口径与阈值显式写在报告里，避免结论被误读。"""
    per_station_rows = span.orderBy("station_id").collect()
    worst = {
        row["station_id"]: int(row["max_gap_len"])
        for row in runs.groupBy("station_id")
        .agg(F.max("gap_len").alias("max_gap_len"))
        .collect()
    }
    per_station = [
        {
            "station_id": int(row["station_id"]),
            "first_ts": _fmt(row["first_ts"]),
            "last_ts": _fmt(row["last_ts"]),
            "expected_hours": int(row["expected_hours"]),
            "actual_hours": int(row["actual_hours"]),
            "missing_hours": int(row["missing_hours"]),
            "max_gap_run": worst.get(int(row["station_id"]), 0),
        }
        for row in per_station_rows
    ]

    missing_total = sum(item["missing_hours"] for item in per_station)
    expected_total = sum(item["expected_hours"] for item in per_station)
    gaps_sorted = (
        runs.orderBy(F.col("gap_len").desc(), "station_id", "gap_start").limit(detail_limit).collect()
    )
    gap_details = [
        {
            "station_id": int(row["station_id"]),
            "gap_len": int(row["gap_len"]),
            "gap_start": _fmt(row["gap_start"]),
            "gap_end": _fmt(row["gap_end"]),
        }
        for row in gaps_sorted
    ]

    return {
        "checked_at": datetime.datetime.now(SHANGHAI).isoformat(),
        "dwd": dwd,
        "scope": "hourly-coverage-gate-before-feature-engineering",
        "row_offset_semantics": {
            "features_using_row_offset": [
                "lag_1h", "lag_24h", "busy_lag_1h", "busy_lag_24h",
                "roll_6h", "roll_24h", "busy_roll_6h", "busy_roll_24h",
                "y_load_h{h}", "y_busy_h{h}", "naive_load_h{h}", "naive_busy_h{h}",
            ],
            "note": "行偏移仅在逐小时连续序列上等价于时间偏移；存在缺口时须先用网格对齐。",
        },
        "stations": len(per_station),
        "expected_hours_total": expected_total,
        "actual_hours_total": expected_total - missing_total,
        "missing_hours_total": missing_total,
        "missing_ratio": None if expected_total == 0 else round(missing_total / expected_total, 8),
        "max_gap_tolerance": tolerance,
        "row_offset_safe": missing_total <= tolerance,
        "per_station": per_station,
        "largest_gaps": gap_details,
        "detail_truncated": missing_total > 0 and len(gap_details) >= detail_limit,
        "recommendation": (
            "可直接进入特征工程。"
            if missing_total <= tolerance
            else "禁止直接进入特征工程：须先按站点补齐整点网格（缺失小时置 NULL 并在训练时丢弃），"
                 "或改用时间戳对齐的窗口计算；本判定需由 #2/#4/#5 共同冻结后写入契约。"
        ),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dwd", required=True, help="dwd_station_hourly 的 Parquet 路径")
    ap.add_argument("--report", required=True, help="输出 JSON 报告路径（会覆盖）")
    ap.add_argument("--max-gap-tolerance", type=int, default=0,
                    help="允许的缺失小时总数上限，超过则 row_offset_safe=false（默认 0）")
    ap.add_argument("--detail-limit", type=int, default=50, help="缺口明细保留的条数上限")
    args = ap.parse_args()

    if args.max_gap_tolerance < 0:
        raise SystemExit("--max-gap-tolerance 不能为负数")

    spark = SparkSession.builder.appName("part2-ml-coverage-check").getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")
    print(f"[coverage] master={spark.sparkContext.master} app={spark.sparkContext.applicationId}")

    raw = spark.read.parquet(args.dwd)
    index = load_hour_index(raw)
    span = station_span(index)
    missing = missing_points(span, index)
    runs = gap_runs(missing)

    report = build_report(args.dwd, span, runs, missing,
                          args.max_gap_tolerance, args.detail_limit)
    with open(args.report, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")

    print(f"[coverage] stations={report['stations']} expected={report['expected_hours_total']} "
          f"actual={report['actual_hours_total']} missing={report['missing_hours_total']} "
          f"safe={report['row_offset_safe']}")
    if report["largest_gaps"]:
        print(f"[coverage] 最大缺口：{report['largest_gaps'][0]}")
    print("COVERAGE_DONE")
    spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
