"""临时自测数据生成（#5 自测用，非交付物）。

在 #3 的 handoff/dwd 到位之前，生成符合 dwd_station_hourly schema 的
8 站 × 90 天 × 24h = 17280 行小时级数据，本地落 CSV 后写入 HDFS Parquet，
用于验证 ML 全链路（特征 → 训练 → 评估 → 落库）在 Spark on YARN 上可跑通。

    spark-submit --master yarn make_synth_hourly.py \
        --csv ./dwd_station_hourly_synth.csv \
        --out-hdfs hdfs://pangxiangzhen01:8020/ev-charging/tmp/dwd_station_hourly_synth

数据含真实结构：早晚双峰、周末/节假日差异、季节温度、站点间差异（确定性种子）。
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import math
import os
import random

from pyspark.sql import SparkSession

SEED = 20260914
START = dt.datetime(2026, 6, 1, 0, 0, 0)
DAYS = 90

# 2026 年部分法定节假日（自测用近似值，正式数据以 #4 生成为准）
HOLIDAYS = {
    "2026-06-19", "2026-06-20", "2026-06-21",   # 端午
    "2026-08-15", "2026-08-16", "2026-08-17",   # 暑期调休样例
}

HEADER = [
    "station_id", "observed_at", "pile_count", "rated_power_kw",
    "temperature_c", "is_holiday", "busy_count", "load_kw",
]


def daily_profile(hour: int) -> float:
    """日内双峰曲线：早高峰 8:30、晚高峰 20:00，午间次峰，凌晨低谷。"""
    morning = math.exp(-((hour - 8.5) ** 2) / (2 * 2.0 ** 2))
    evening = math.exp(-((hour - 20.0) ** 2) / (2 * 2.2 ** 2))
    noon = 0.35 * math.exp(-((hour - 13.0) ** 2) / (2 * 2.5 ** 2))
    return 0.12 + 0.80 * morning + 1.00 * evening + noon


def temperature(day_index: int, hour: int) -> float:
    season = 26.0 + 7.0 * math.sin(2 * math.pi * (day_index + 60) / 365.0)
    diurnal = 4.5 * math.sin(2 * math.pi * (hour - 15) / 24.0)
    return round(season + diurnal, 1)


def generate(csv_path: str) -> int:
    rng = random.Random(SEED)

    stations = []
    for sid in range(1, 9):
        pile = rng.choice([24, 30, 36, 40, 48])
        rated = rng.choice([60, 120, 30, 7])
        util = rng.uniform(0.22, 0.50)          # 站点基础利用率
        stall = 0.75 + 0.25 * (sid / 8.0)       # 新站爬坡
        stations.append((sid, pile, rated, util, stall))

    rows = 0
    with open(csv_path, "w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(HEADER)
        for day in range(DAYS):
            date = START + dt.timedelta(days=day)
            date_str = date.strftime("%Y-%m-%d")
            is_holiday = date_str in HOLIDAYS
            for hour in range(24):
                ts = date + dt.timedelta(hours=hour)
                observed_at = ts.strftime("%Y-%m-%dT%H:%M:%S+08:00")
                temp = temperature(day, hour)
                for sid, pile, rated, util, stall in stations:
                    weekend = 0.86 if ts.weekday() >= 5 else 1.0
                    holiday = 0.92 if is_holiday else 1.0
                    heat = 1.0 + max(0.0, (temp - 30.0)) * 0.02
                    factor = daily_profile(hour) * weekend * holiday * heat * stall
                    util_now = min(0.95, util * factor + rng.gauss(0, 0.02))
                    busy = int(round(pile * util_now))
                    busy = max(0, min(pile, busy))
                    load = round(busy * rated * rng.uniform(0.55, 0.85), 2)
                    writer.writerow([
                        sid, observed_at, pile, rated, temp,
                        "true" if is_holiday else "false", busy, load,
                    ])
                    rows += 1
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="./dwd_station_hourly_synth.csv")
    ap.add_argument("--out-hdfs", default="")
    args = ap.parse_args()

    os.makedirs(os.path.dirname(os.path.abspath(args.csv)), exist_ok=True)
    rows = generate(args.csv)
    print(f"[synth] local csv = {args.csv} rows={rows}")

    if args.out_hdfs:
        spark = SparkSession.builder.appName("part2-synth-hourly").getOrCreate()
        spark.conf.set("spark.sql.shuffle.partitions", "8")
        # 注意：defaultFS 为 HDFS 时，相对路径会被解析到 HDFS，必须显式指定本地 file://
        csv_uri = "file://" + os.path.abspath(args.csv)
        df = spark.read.option("header", True).option("inferSchema", True).csv(csv_uri)
        df.write.mode("overwrite").parquet(args.out_hdfs)
        print(f"[synth] parquet -> {args.out_hdfs} count={spark.read.parquet(args.out_hdfs).count()}")
        spark.stop()

    print("SYNTH_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
