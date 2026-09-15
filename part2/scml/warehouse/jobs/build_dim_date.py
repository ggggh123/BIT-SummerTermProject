from __future__ import annotations

import argparse
from datetime import date, datetime, timedelta

from pyspark.sql import SparkSession


def build_rows(start: date, days: int) -> list[tuple[str, int, int, int, int]]:
    rows = []
    for offset in range(days):
        current = start + timedelta(days=offset)
        rows.append(
            (
                current.isoformat(),
                current.year,
                current.month,
                current.isoweekday(),
                1 if current.weekday() >= 5 else 0,
            )
        )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Build dim_date for DWD handoff.")
    parser.add_argument("--start", default="2026-09-01")
    parser.add_argument("--days", type=int, default=90)
    parser.add_argument("--out", default="/ev-charging/dwd/dim_date")
    args = parser.parse_args()

    spark = (
        SparkSession.builder.appName("ev-scml-dim-date")
        .enableHiveSupport()
        .getOrCreate()
    )
    start = datetime.strptime(args.start, "%Y-%m-%d").date()
    rows = build_rows(start, args.days)
    df = spark.createDataFrame(
        rows,
        "dt string, year int, month int, day_of_week int, is_weekend int",
    )
    df.write.mode("overwrite").parquet(args.out)
    spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
