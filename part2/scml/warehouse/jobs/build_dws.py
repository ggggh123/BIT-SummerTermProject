#!/usr/bin/env python3
"""DWS 层 SparkSQL 作业（在伪分布式 Hadoop 上以 `spark-submit` 运行）。

    spark-submit warehouse/jobs/build_dws.py \\
        --sql-dir warehouse/sql \\
        --hdfs-root /ev-charging

执行内容 = `sql/dws_schema.sql`（建库建表） + `sql/dws_etl.sql`（四表灌数），
逐条交 `spark.sql()` 执行。产出 Parquet 落在
`<hdfs-root>/dws/dws_{station,charger,user,region}_day`。

前置条件：`dim_date` 已就绪（`scripts/run_dim_date.sh`）、#3 的 DWD 已
`hdfs dfs -put` 到 `<hdfs-root>/dwd`。缺 DWD 时作业会直接失败——这是刻意的，
不允许「静默出一份空表」。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _lib import split_sql_statements  # noqa: E402

DEFAULT_SQL_DIR = Path(__file__).resolve().parents[1] / "sql"
FILES = ["dws_schema.sql", "dws_etl.sql"]


def main() -> int:
    parser = argparse.ArgumentParser(description="DWS 四表 SparkSQL 作业")
    parser.add_argument("--sql-dir", type=Path, default=DEFAULT_SQL_DIR)
    parser.add_argument("--hdfs-root", default="/ev-charging",
                        help="HDFS 根目录（SQL 里写死 /ev-charging，此处做整体替换）")
    parser.add_argument("--dry-run", action="store_true",
                        help="只打印将要执行的语句条数与首条，不连 Spark（用于语法自查）")
    args = parser.parse_args()

    statements: list[str] = []
    for name in FILES:
        path = args.sql_dir / name
        sql = path.read_text(encoding="utf-8")
        if args.hdfs_root != "/ev-charging":
            sql = sql.replace("/ev-charging", args.hdfs_root)
        statements += split_sql_statements(sql)

    if args.dry_run:
        print(f"[dry-run] {len(statements)} 条语句")
        for statement in statements:
            print("-" * 70)
            print(statement[:400])
        return 0

    from pyspark.sql import SparkSession  # 延迟导入：让 --dry-run 在没有 pyspark 的机器上也能跑

    spark = (
        SparkSession.builder.appName("ev-scml-dws")
        .enableHiveSupport()
        .config("spark.sql.sources.partitionOverwriteMode", "dynamic")
        .getOrCreate()
    )
    spark.sparkContext.setLogLevel("WARN")
    try:
        for index, statement in enumerate(statements, start=1):
            head = " ".join(statement.split())[:90]
            print(f"[{index}/{len(statements)}] {head}", flush=True)
            try:
                spark.sql(statement)
            except Exception as exc:  # noqa: BLE001 —— 要把出错的是第几条语句带出来
                # 在虚拟机上跑时日志很长，不指明条数就只能靠翻 plan dump 找
                print(f"[FAIL] 第 {index}/{len(statements)} 条语句执行失败：{exc}", flush=True)
                raise
        # 用行数自证不是空跑
        for table in ("dws_station_day", "dws_charger_day", "dws_user_day", "dws_region_day"):
            count = spark.sql(f"SELECT COUNT(1) AS n FROM ev_charging.{table}").collect()[0]["n"]
            print(f"[ok] ev_charging.{table}: {count} rows")
    finally:
        spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
