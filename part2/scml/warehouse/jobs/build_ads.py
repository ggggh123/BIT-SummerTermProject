#!/usr/bin/env python3
"""ADS 层 SparkSQL 作业（在伪分布式 Hadoop 上以 `spark-submit` 运行）。

    spark-submit warehouse/jobs/build_ads.py \\
        --sql-dir warehouse/sql \\
        --hdfs-root /ev-charging

执行内容 = `sql/ads_etl.sql`（7 张业务指标表的 Hive DDL + 灌数），产出 Parquet 落在
`<hdfs-root>/ads/*`。

★ 只产出 7 张业务指标表。另外 8 张（ads_meta / ads_quality_* / ads_forecast_* /
ads_event）由 `export_ads_db.py` 用 Python 侧逻辑产出，合并后才能得到完整的
`handoff/ads/ads.db` —— 理由写在 `ads_etl.sql` 的文件头。

前置条件：`build_dws.py` 已跑完（DWS 四表就位）、#3 的 DWD 维表与
`dwd_order_detail` / `dwd_station_hourly` / `dwd_event` 已就位。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _lib import split_sql_statements  # noqa: E402

DEFAULT_SQL_DIR = Path(__file__).resolve().parents[1] / "sql"
OBJECTS = [
    "ads_station",
    "ads_charger",
    "ads_daily",
    "ads_station_day",
    "ads_station_hourly",
    "ads_user_rfm",
    "ads_district",
]


def main() -> int:
    parser = argparse.ArgumentParser(description="ADS 业务指标表 SparkSQL 作业")
    parser.add_argument("--sql-dir", type=Path, default=DEFAULT_SQL_DIR)
    parser.add_argument("--hdfs-root", default="/ev-charging")
    parser.add_argument("--dry-run", action="store_true",
                        help="只打印将要执行的语句，不连 Spark")
    args = parser.parse_args()

    sql = (args.sql_dir / "ads_etl.sql").read_text(encoding="utf-8")
    if args.hdfs_root != "/ev-charging":
        sql = sql.replace("/ev-charging", args.hdfs_root)
    statements = split_sql_statements(sql)

    if args.dry_run:
        print(f"[dry-run] {len(statements)} 条语句")
        for statement in statements:
            print("-" * 70)
            print(statement[:400])
        return 0

    from pyspark.sql import SparkSession

    spark = (
        SparkSession.builder.appName("ev-scml-ads")
        .enableHiveSupport()
        .config("spark.sql.sources.partitionOverwriteMode", "dynamic")
        .getOrCreate()
    )
    spark.sparkContext.setLogLevel("WARN")
    try:
        for index, statement in enumerate(statements, start=1):
            head = " ".join(statement.split())[:90]
            print(f"[{index}/{len(statements)}] {head}", flush=True)
            spark.sql(statement)
        for table in OBJECTS:
            count = spark.sql(f"SELECT COUNT(1) AS n FROM ev_charging.{table}").collect()[0]["n"]
            print(f"[ok] ev_charging.{table}: {count} rows")
    finally:
        spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
