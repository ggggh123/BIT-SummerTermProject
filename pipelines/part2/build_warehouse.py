#!/usr/bin/env python3
"""SparkSQL 分层：DWD -> DWS -> ADS（老师第 6 步）。

产出：
  HDFS /ev-charging/dws、/ev-charging/ads（Parquet，真源）
  handoff/ads/ads.db（SQLite 单文件，供 Flask 只读与答辩查数，不用 MySQL）
  handoff/ads/json/*.json（与 docs/design/part2-api-contract.md 同构，供 Flask 回源）
"""
import json
import os
import sqlite3

from pyspark.sql import SparkSession, functions as F

BASE = os.path.dirname(os.path.abspath(__file__))
DWD = os.environ.get("DWD_URI", "hdfs://TimeMachine:8020/ev-charging/dwd")
DWS = os.environ.get("DWS_URI", "hdfs://TimeMachine:8020/ev-charging/dws")
ADS = os.environ.get("ADS_URI", "hdfs://TimeMachine:8020/ev-charging/ads")
OUT = os.path.join(BASE, "handoff", "ads")
GENERATED_AT = "2026-09-14T10:05:00+08:00"


def envelope(data):
    return {"code": 0, "message": "ok", "data": data, "generatedAt": GENERATED_AT}


def export_station(rows):
    return [{
        "stationId": r["id"], "name": r["name"], "district": r["district"],
        "longitude": r["longitude"], "latitude": r["latitude"],
        "chargerCount": int(r["charger_cnt"] or 0), "idleCount": int(r["idle_cnt"] or 0),
        "utilizationRate": float(r["utilization_rate"] or 0),
        "revenueFen": int(r["revenue_fen"] or 0), "priceFenPerKwh": int(r["price_fen_per_kwh"]),
        "orderCount": int(r["order_cnt"] or 0), "forecastEnabled": bool(r["forecast_enabled"]),
    } for r in rows]


def main():
    spark = (SparkSession.builder.appName("part2-build-warehouse")
             .config("spark.sql.shuffle.partitions", "8").getOrCreate())
    spark.sparkContext.setLogLevel("ERROR")

    orders = spark.read.parquet(f"{DWD}/dwd_order_detail")
    stations = spark.read.parquet(f"{DWD}/dim_stations")
    chargers = spark.read.parquet(f"{DWD}/dim_chargers")
    hourly = spark.read.parquet(f"{DWD}/dwd_station_hourly")

    st_small = stations.select("id", "district", "price_fen_per_kwh").withColumnRenamed("id", "station_id")
    # 注意：orders 没有 station_id（第一阶段 schema 如此），必须经 chargers 关联
    charger_station = chargers.select(F.col("id").alias("charger_id"), "station_id")
    o = (orders.withColumn("dt", F.substring("started_at", 1, 10))
         .join(charger_station, "charger_id", "left")
         .join(st_small, "station_id", "left"))

    dws_station_day = o.groupBy("dt", "station_id").agg(
        F.count("*").alias("order_cnt"),
        F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"),
        F.sum("amount_fen").alias("revenue_fen"))
    dws_charger_day = o.groupBy("dt", "charger_id").agg(
        F.count("*").alias("order_cnt"),
        F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"))
    dws_user_day = o.groupBy("dt", "user_id").agg(
        F.count("*").alias("order_cnt"),
        F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"),
        F.sum("amount_fen").alias("amount_fen"))
    dws_region_day = o.groupBy("dt", "district").agg(
        F.countDistinct("station_id").alias("station_cnt"),
        F.count("*").alias("order_cnt"),
        F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"),
        F.sum("amount_fen").alias("revenue_fen"))

    for name, df in [("dws_station_day", dws_station_day), ("dws_charger_day", dws_charger_day),
                     ("dws_user_day", dws_user_day), ("dws_region_day", dws_region_day)]:
        df.write.mode("overwrite").parquet(f"{DWS}/{name}")
        print(f"  {name}: {df.count()} 行")

    total_fen = orders.agg(F.sum("amount_fen")).first()[0] or 0
    total_kwh = orders.agg(F.sum("energy_kwh")).first()[0] or 0
    charger_cnt = chargers.count()
    fault_cnt = chargers.filter("status = 'fault'").count()
    status_rows = {r["status"]: r["cnt"] for r in chargers.groupBy("status").agg(F.count("*").alias("cnt")).collect()}

    station_util = hourly.groupBy("station_id").agg(
        F.round(F.avg(F.col("busy_count") / F.col("pile_count")) * 100, 1).alias("utilization_rate"))
    station_rev = dws_station_day.groupBy("station_id").agg(
        F.sum("revenue_fen").alias("revenue_fen"),
        F.sum("order_cnt").alias("order_cnt"),
        F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"))
    idle = chargers.filter("status = 'idle'").groupBy("station_id").agg(F.count("*").alias("idle_cnt"))
    cnt = chargers.groupBy("station_id").agg(F.count("*").alias("charger_cnt"))
    base = stations.select("id", "name", "district", "longitude", "latitude", "price_fen_per_kwh", "forecast_enabled")

    def join_on(df):
        nonlocal base
        j = base.join(df, base.id == df.station_id, "left").drop("station_id")
        base = j
        return j

    base = join_on(station_rev)
    base = join_on(station_util)
    base = join_on(idle)
    base = join_on(cnt)
    ads_station_ranking = base.fillna({"revenue_fen": 0, "order_cnt": 0, "energy_kwh": 0.0,
                                       "utilization_rate": 0.0, "idle_cnt": 0, "charger_cnt": 0})

    ads_charger_health = (chargers.groupBy("station_id", "status").agg(F.count("*").alias("cnt"))
                          .groupBy("station_id").pivot("status").sum("cnt"))
    peak_hour = (hourly.withColumn("hour", F.substring("observed_at", 12, 2))
                 .groupBy("station_id", "hour")
                 .agg(F.round(F.avg("busy_count"), 1).alias("avg_busy"))
                 .orderBy("station_id", "hour"))
    gov_service = (dws_region_day.groupBy("district").agg(
        F.sum("order_cnt").alias("order_cnt"),
        F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"),
        F.sum("revenue_fen").alias("revenue_fen"),
        F.max("station_cnt").alias("station_cnt"))
        .withColumn("co2_saved_ton", F.round(F.col("energy_kwh") / 1000 * 0.581, 1)))

    for name, df in [("ads_station_ranking", ads_station_ranking), ("ads_charger_health", ads_charger_health),
                     ("ads_peak_hour", peak_hour), ("ads_gov_service", gov_service)]:
        df.write.mode("overwrite").parquet(f"{ADS}/{name}")

    os.makedirs(os.path.join(OUT, "json"), exist_ok=True)
    db_path = os.path.join(OUT, "ads.db")
    if os.path.exists(db_path):
        os.remove(db_path)
    con = sqlite3.connect(db_path)

    def save_df(df, table):
        cols = df.columns
        rows = [list(r) for r in df.collect()]
        con.execute(f'CREATE TABLE "{table}" ({", ".join(chr(34) + c + chr(34) for c in cols)})')
        if rows:
            con.executemany(f'INSERT INTO "{table}" VALUES ({",".join("?" * len(cols))})', rows)
        print(f"  ads.db <- {table} ({len(rows)} 行)")
        # 返回字典行，便于后续按列名导出同构 JSON
        return [dict(zip(cols, r)) for r in rows]

    ranking_rows = save_df(ads_station_ranking, "ads_station_ranking")
    save_df(gov_service, "ads_gov_service")
    save_df(peak_hour, "ads_peak_hour")
    save_df(ads_charger_health, "ads_charger_health")
    save_df(dws_region_day, "dws_region_day")
    con.commit()
    con.close()

    def dump(name, data):
        with open(os.path.join(OUT, "json", f"{name}.json"), "w", encoding="utf-8") as fh:
            json.dump(envelope(data), fh, ensure_ascii=False, indent=2)

    dump("overview_kpis", {
        "totalRevenueFen": int(total_fen), "totalEnergyKwh": round(float(total_kwh), 1),
        "totalOrders": int(orders.count()), "chargerCount": charger_cnt,
        "idleCount": int(status_rows.get("idle", 0)),
        "onlineRate": round((charger_cnt - fault_cnt) / max(1, charger_cnt) * 100, 1),
        "windowDays": 90})
    dump("overview_stations", export_station(ranking_rows))
    dump("overview_charger-status", {
        "idle": int(status_rows.get("idle", 0)), "reserved": int(status_rows.get("reserved", 0)),
        "charging": int(status_rows.get("charging", 0)), "fault": fault_cnt,
        "restarting": int(status_rows.get("restarting", 0))})
    dump("enterprise_revenue-trend", {
        "days": 90,
        "points": [{"date": r["dt"], "revenueFen": int(r["revenue_fen"]), "orderCount": int(r["order_cnt"]),
                    "energyKwh": float(r["energy_kwh"])}
                   for r in dws_station_day.groupBy("dt").agg(
                       F.sum("revenue_fen").alias("revenue_fen"),
                       F.sum("order_cnt").alias("order_cnt"),
                       F.round(F.sum("energy_kwh"), 2).alias("energy_kwh")).orderBy("dt").collect()]})
    dump("enterprise_station-ranking", [{
        "stationId": r["id"], "name": r["name"], "utilizationRate": float(r["utilization_rate"] or 0),
        "revenueFen": int(r["revenue_fen"] or 0), "idleCount": int(r["idle_cnt"] or 0),
        "chargerCount": int(r["charger_cnt"] or 0), "orderCount": int(r["order_cnt"] or 0),
    } for r in ranking_rows])

    print(f"\nADS 导出完成：{db_path} 与 {OUT}/json/")
    spark.stop()


if __name__ == "__main__":
    main()
