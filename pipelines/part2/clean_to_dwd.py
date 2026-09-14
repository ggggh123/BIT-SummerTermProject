#!/usr/bin/env python3
"""PySpark 数据清洗（老师第 4 步）：ODS -> DWD（Parquet on HDFS）。

策略依据《03》：关键字段缺失/异常/矛盾/孤儿引用 -> 剔除；可修复项（空格、全半角、金额口径）-> 修正。
输出 handoff/quality/cleaning_report.json（清洗前后行数与处置量）。
"""
import json
import os

from pyspark.sql import SparkSession, functions as F

BASE = os.path.dirname(os.path.abspath(__file__))
ODS = os.environ.get("ODS_URI", f"{BASE}/handoff/ods")
DWD = os.environ.get("DWD_URI", "hdfs://TimeMachine:8020/ev-charging/dwd")
OUT = os.path.join(BASE, "handoff", "quality")


def read(spark, name):
    return spark.read.option("header", True).option("inferSchema", False).csv(f"{ODS}/{name}.csv")


def to_iso(col):
    """把 ISO / yyyy/MM/dd HH:mm:ss / epoch / yyyyMMddHHmmss 统一为 +08:00 ISO 8601（失败返回 null）"""
    return F.when(col.rlike(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+08:00$"), col) \
        .when(col.rlike(r"^\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}$"),
              F.date_format(F.to_timestamp(col, "yyyy/MM/dd HH:mm:ss"), "yyyy-MM-dd'T'HH:mm:ss'+08:00'")) \
        .when(col.rlike(r"^\d{10}$"),
              F.date_format(F.to_timestamp(col.cast("long")), "yyyy-MM-dd'T'HH:mm:ss'+08:00'")) \
        .when(col.rlike(r"^\d{14}$"),
              F.date_format(F.to_timestamp(col, "yyyyMMddHHmmss"), "yyyy-MM-dd'T'HH:mm:ss'+08:00'")) \
        .otherwise(F.lit(None))


def clean_dims(spark, report):
    stations = read(spark, "stations")
    st = (stations
          .withColumn("name", F.trim(F.regexp_replace("name", "[　\\s]+", "")))
          .withColumn("address", F.trim(F.regexp_replace("address", "[　]+", " ")))
          .withColumn("district", F.trim("district"))
          .withColumn("latitude", F.col("latitude").cast("double"))
          .withColumn("longitude", F.col("longitude").cast("double"))
          .withColumn("price_fen_per_kwh", F.col("price_fen_per_kwh").cast("int"))
          .withColumn("forecast_enabled", F.col("forecast_enabled").cast("int"))
          .filter("latitude BETWEEN 39.4 AND 41.1 AND longitude BETWEEN 115.4 AND 117.5")
          .dropDuplicates(["id"]))
    report.append(("dim_stations", stations.count(), st.count()))

    users = read(spark, "users")
    us = (users
          .withColumn("mobile", F.trim("mobile"))
          .withColumn("nickname", F.trim("nickname"))
          .withColumn("nickname", F.when(F.coalesce("nickname", F.lit("")) == "", F.concat(F.lit("用户"), F.col("id"))).otherwise("nickname"))
          .withColumn("balance_fen", F.col("balance_fen").cast("int"))
          .filter("mobile RLIKE '^1[0-9]{10}$'")
          .filter("status IN ('active','frozen')")
          .dropDuplicates(["id"]))
    report.append(("dim_users", users.count(), us.count()))

    chargers = read(spark, "chargers")
    ch = (chargers
          .withColumn("power_kw", F.col("power_kw").cast("double"))
          .withColumn("charge_count", F.col("charge_count").cast("int"))
          .withColumn("total_duration_sec", F.col("total_duration_sec").cast("long"))
          .filter("status IN ('idle','reserved','charging','fault','restarting')")
          .dropDuplicates(["id"]))
    report.append(("dim_chargers", chargers.count(), ch.count()))
    return st, us, ch


def clean_orders(spark, st, us, ch, report):
    orders = read(spark, "orders")
    price = st.select(F.col("id").alias("sid"), F.col("price_fen_per_kwh").cast("double").alias("price"))
    sid_of = ch.select(F.col("id").alias("charger_id"), F.col("station_id").alias("sid"))

    df = (orders
          .withColumn("started_at", to_iso(F.col("started_at")))
          .withColumn("ended_at", to_iso(F.col("ended_at")))
          .withColumn("energy_kwh", F.col("energy_kwh").cast("double"))
          .withColumn("amount_fen", F.col("amount_fen").cast("double"))
          .dropDuplicates(["id"])
          .filter("energy_kwh IS NOT NULL AND energy_kwh >= 0 AND energy_kwh <= 500")
          .filter("status IN ('reserved','charging','completed','cancelled')")
          .filter("NOT (status = 'completed' AND ended_at IS NULL)")
          .filter("ended_at IS NULL OR started_at IS NULL OR ended_at >= started_at"))
    after_rules = df.count()

    df = (df.join(sid_of, "charger_id", "left").join(price, "sid", "left")
          .filter(F.col("sid").isNotNull())            # 孤儿引用（charger）
          .join(us.select(F.col("id").alias("uid")), df.user_id == F.col("uid"), "left")
          .filter(F.col("uid").isNotNull())            # 孤儿引用（user）
          # 金额口径：以「单价 × 电量」为准重算（元/分混入自动被纠正）
          .withColumn("amount_fen", F.round(F.col("energy_kwh") * F.col("price")))
          .drop("sid", "price", "uid"))
    report.append(("dwd_order_detail", orders.count(), df.count()))
    return df


def clean_telemetry(spark, ch, report):
    telemetry = read(spark, "telemetry")
    rated = ch.select(F.col("id").alias("charger_id"), F.col("power_kw").alias("rated_kw"))
    df = (telemetry
          .withColumn("recorded_at", to_iso(F.col("recorded_at")))
          .withColumn("power_kw", F.col("power_kw").cast("double"))
          .withColumn("energy_increment_kwh", F.col("energy_increment_kwh").cast("double"))
          .filter("recorded_at IS NOT NULL")
          .dropDuplicates(["charger_id", "recorded_at"])
          .join(rated, "charger_id", "inner")
          .filter("power_kw IS NOT NULL AND power_kw >= 0 AND power_kw <= rated_kw * 1.2")
          .filter("energy_increment_kwh IS NOT NULL AND energy_increment_kwh >= 0")
          .drop("rated_kw"))
    report.append(("dwd_telemetry_detail", telemetry.count(), df.count()))
    return df


def clean_hourly(spark, report):
    hourly = read(spark, "station_hourly_history")
    df = (hourly
          .withColumn("observed_at", to_iso(F.col("observed_at")))
          .withColumn("pile_count", F.col("pile_count").cast("int"))
          .withColumn("busy_count", F.col("busy_count").cast("int"))
          .withColumn("load_kw", F.col("load_kw").cast("double"))
          .withColumn("temperature_c", F.col("temperature_c").cast("double"))
          .withColumn("is_holiday", F.col("is_holiday").cast("int"))
          .filter("observed_at IS NOT NULL AND pile_count > 0")
          .filter("busy_count BETWEEN 0 AND pile_count")
          .filter("temperature_c BETWEEN -40 AND 60 AND load_kw >= 0")
          .dropDuplicates(["station_id", "observed_at"]))
    report.append(("dwd_station_hourly", hourly.count(), df.count()))
    return df


def main():
    spark = (SparkSession.builder.appName("part2-clean-to-dwd")
             .config("spark.sql.shuffle.partitions", "8").getOrCreate())
    spark.sparkContext.setLogLevel("ERROR")

    report = []
    st, us, ch = clean_dims(spark, report)
    orders = clean_orders(spark, st, us, ch, report)
    telemetry = clean_telemetry(spark, ch, report)
    hourly = clean_hourly(spark, report)

    targets = {"dim_stations": st, "dim_users": us, "dim_chargers": ch,
               "dwd_order_detail": orders, "dwd_telemetry_detail": telemetry,
               "dwd_station_hourly": hourly}
    for name, df in targets.items():
        df.write.mode("overwrite").parquet(f"{DWD}/{name}")
        print(f"  写出 {DWD}/{name}")

    os.makedirs(OUT, exist_ok=True)
    payload = {"tables": [{"table": t, "rows_before": b, "rows_after": a,
                           "removed": b - a, "keep_rate": round(a / max(1, b), 4)} for t, b, a in report]}
    with open(os.path.join(OUT, "cleaning_report.json"), "w", encoding="utf-8") as fh:
        json.dump(payload, fh, ensure_ascii=False, indent=2)
    print("\n清洗前后：")
    for t, b, a in report:
        print(f"  {t}: {b} -> {a} (剔除 {b - a})")
    spark.stop()


if __name__ == "__main__":
    main()
