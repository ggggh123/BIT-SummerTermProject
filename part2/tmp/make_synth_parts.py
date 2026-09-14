"""临时自测数据：#5 分担数仓表验证用的最小维度与订单数据（非交付物）。

在 #3 的 handoff/dwd 到位前，生成 ``dwd_order_detail`` / ``dim_stations`` /
``dim_chargers`` 三个最小输入（8 站、约 300 桩、约 1.6 万订单、5 个行政区），
用于验证 ``dws_region_day`` 与 ``ads_gov_service`` 的 SparkSQL 逻辑与对账约束。

    spark-submit --master yarn make_synth_parts.py --out-base hdfs://.../ev-charging/tmp/synth_dwd
"""

from __future__ import annotations

import argparse
import datetime as dt
import random

from pyspark.sql import SparkSession
from pyspark.sql.types import (
    DoubleType, IntegerType, StringType, StructField, StructType,
)

SEED = 20260914
DISTRICTS = ("朝阳", "海淀", "丰台", "通州", "大兴")
START = dt.datetime(2026, 6, 1)
DAYS = 90
N_USERS = 5000
#: 行政区面积（km²，公开资料近似值，用于覆盖密度）
AREA_KM2 = {"朝阳": 470.8, "海淀": 430.7, "丰台": 306.0, "通州": 906.0, "大兴": 1036.0}

STATION_SCHEMA = StructType([
    StructField("station_id", IntegerType()),
    StructField("name", StringType()),
    StructField("district", StringType()),
    StructField("latitude", DoubleType()),
    StructField("longitude", DoubleType()),
    StructField("price_fen_per_kwh", IntegerType()),
])

CHARGER_SCHEMA = StructType([
    StructField("charger_id", IntegerType()),
    StructField("station_id", IntegerType()),
    StructField("rated_power_kw", IntegerType()),
    StructField("status", StringType()),
])

ORDER_SCHEMA = StructType([
    StructField("order_id", IntegerType()),
    StructField("user_id", IntegerType()),
    StructField("charger_id", IntegerType()),
    StructField("station_id", IntegerType()),
    StructField("status", StringType()),
    StructField("started_at", StringType()),
    StructField("ended_at", StringType()),
    StructField("energy_kwh", DoubleType()),
    StructField("amount_fen", IntegerType()),
    StructField("dt", StringType()),
])


def build():
    rng = random.Random(SEED)

    stations = []
    for sid in range(1, 9):
        district = DISTRICTS[(sid - 1) % len(DISTRICTS)]
        stations.append((
            sid,
            f"{district}充电站{sid}号",
            district,
            round(39.4 + rng.random() * 1.6, 6),
            round(115.5 + rng.random() * 1.9, 6),
            rng.choice([80, 100, 120, 140, 160]),
        ))
    price_by_station = {s[0]: s[5] for s in stations}
    district_by_station = {s[0]: s[2] for s in stations}

    chargers = []
    chargers_by_station: dict[int, list[tuple[int, int]]] = {}
    cid = 1
    for sid in range(1, 9):
        n = rng.choice([24, 30, 36, 40, 48])
        bucket = []
        for _ in range(n):
            rated = rng.choice([7, 30, 60, 120])
            chargers.append((cid, sid, rated, rng.choice(["idle", "charging", "fault", "reserved"])))
            bucket.append((cid, rated))
            cid += 1
        chargers_by_station[sid] = bucket

    orders = []
    oid = 1
    for day in range(DAYS):
        date = START + dt.timedelta(days=day)
        dt_str = date.strftime("%Y-%m-%d")
        for sid in range(1, 9):
            n_orders = rng.randint(14, 30)
            for _ in range(n_orders):
                hour = rng.choice([7, 8, 9, 12, 13, 17, 18, 19, 20, 21, 22, 23, 1, 2, 3])
                minute = rng.randint(0, 59)
                charger_id, rated = rng.choice(chargers_by_station[sid])
                # 快充时长短电量高、慢充相反
                if rated >= 60:
                    energy = round(rng.uniform(18, 55), 2)
                    duration_min = int(energy / rated * 60 * rng.uniform(0.85, 1.15))
                else:
                    energy = round(rng.uniform(8, 28), 2)
                    duration_min = int(energy / rated * 60 * rng.uniform(0.85, 1.15))
                started = date + dt.timedelta(hours=hour, minutes=minute)
                ended = started + dt.timedelta(minutes=max(5, duration_min))
                status = rng.choices(["completed", "cancelled", "charging"], weights=[92, 5, 3])[0]
                amount = int(round(energy * price_by_station[sid]))
                orders.append((
                    oid, rng.randint(1, N_USERS), charger_id, sid, status,
                    started.strftime("%Y-%m-%dT%H:%M:%S+08:00"),
                    ended.strftime("%Y-%m-%dT%H:%M:%S+08:00"),
                    energy, amount, dt_str,
                ))
                oid += 1

    return stations, chargers, orders


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-base", required=True, help="自测 DWD 输出根目录（建议 HDFS）")
    args = ap.parse_args()

    stations, chargers, orders = build()
    spark = SparkSession.builder.appName("part2-synth-parts").getOrCreate()
    spark.conf.set("spark.sql.shuffle.partitions", "8")

    base = args.out_base.rstrip("/")
    spark.createDataFrame(stations, STATION_SCHEMA).write.mode("overwrite").parquet(f"{base}/dim_stations")
    spark.createDataFrame(chargers, CHARGER_SCHEMA).write.mode("overwrite").parquet(f"{base}/dim_chargers")
    spark.createDataFrame(orders, ORDER_SCHEMA).write.mode("overwrite").parquet(f"{base}/dwd_order_detail")

    print(f"[synth-parts] stations={len(stations)} chargers={len(chargers)} orders={len(orders)}")
    for name in ("dim_stations", "dim_chargers", "dwd_order_detail"):
        cnt = spark.read.parquet(f"{base}/{name}").count()
        print(f"[synth-parts] {name:18s} rows={cnt} -> {base}/{name}")

    spark.stop()
    print("SYNTH_PARTS_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
