"""诊断网格对齐是否改变了数据内容（#5 缺口防护回归排查）。"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ml"))

from pyspark.sql import SparkSession, Window
from pyspark.sql import functions as F

import features as ft

SRC = "hdfs://pangxiangzhen01:8020/ev-charging/tmp/dwd_station_hourly_synth"


def profile(name, df):
    row = df.agg(
        F.count("*").alias("rows"),
        F.countDistinct("station_id").alias("stations"),
        F.countDistinct("observed_at").alias("hours"),
        F.round(F.sum("load_kw"), 4).alias("sum_load"),
        F.round(F.sum("busy_count"), 4).alias("sum_busy"),
        F.round(F.avg("temperature_c"), 4).alias("avg_temp"),
        F.count("temperature_c").alias("nonnull_temp"),
        F.count("load_kw").alias("nonnull_load"),
        F.count("is_holiday").alias("nonnull_holiday"),
    ).first()
    print(f"[diag] {name}: {row.asDict()}")
    return row.asDict()


def main() -> int:
    spark = SparkSession.builder.master("local[2]").appName("diag-align").getOrCreate()
    spark.sparkContext.setLogLevel("ERROR")
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")

    raw = ft.read_hourly(spark, SRC)
    aligned = ft.align_hourly_grid(raw)
    a = profile("raw    ", raw)
    b = profile("aligned", aligned)
    print(f"[diag] identical_rows={a['rows'] == b['rows']} identical_load={a['sum_load'] == b['sum_load']}")

    # 对齐是否改变了每个站点的时间起点 / 步长
    step = (
        aligned.select("station_id", "observed_at")
        .distinct()
        .withColumn(
            "delta",
            F.unix_timestamp("observed_at")
            - F.unix_timestamp(
                F.lag("observed_at").over(
                    Window.partitionBy("station_id").orderBy("observed_at")
                )
            ),
        )
        .groupBy("station_id")
        .agg(F.min("delta").alias("min_step"), F.max("delta").alias("max_step"))
        .orderBy("station_id")
    )
    step.show(10, truncate=False)

    # 逐站比对 load 合计，定位是「全站变化」还是「个别站点」
    per = (
        raw.groupBy("station_id").agg(F.round(F.sum("load_kw"), 4).alias("raw_load"))
        .join(
            aligned.groupBy("station_id").agg(F.round(F.sum("load_kw"), 4).alias("aligned_load")),
            "station_id",
        )
        .withColumn("diff", F.col("raw_load") - F.col("aligned_load"))
        .orderBy("station_id")
    )
    per.show(10, truncate=False)

    print("DIAG_DONE")
    spark.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
