#!/usr/bin/env python3
"""把 ADS/DWD/DWS 结果导出为**全部**接口的同构 JSON（供 Flask 直接回源）。

与 docs/design/part2-api-contract.md 字段一一对应；输出 handoff/ads/json/。
build_warehouse.py 负责 5 个概览/企业接口，本脚本补齐用户、充电站、政府与其余企业接口。
"""
import json
import math
import os

from pyspark.sql import SparkSession, Window, functions as F

BASE = os.path.dirname(os.path.abspath(__file__))
DWD = os.environ.get("DWD_URI", "hdfs://TimeMachine:8020/ev-charging/dwd")
DWS = os.environ.get("DWS_URI", "hdfs://TimeMachine:8020/ev-charging/dws")
ADS = os.environ.get("ADS_URI", "hdfs://TimeMachine:8020/ev-charging/ads")
OUT = os.path.join(BASE, "handoff", "ads", "json")
GEN_AT = "2026-09-14T10:05:00+08:00"
CUTOFF = "2026-09-14"
TIANANMEN = (39.9087, 116.3975)
# 各区常住人口（万人→人），项目假设值，答辩需注明来源假设
POPULATION = {"朝阳区": 3450000, "海淀区": 3130000, "丰台区": 2010000,
              "通州区": 1840000, "大兴区": 1990000}


def envelope(data):
    return {"code": 0, "message": "ok", "data": data, "generatedAt": GEN_AT}


def haversine_km(lat1, lng1, lat2, lng2):
    r = 6371
    d_lat = math.radians(lat2 - lat1)
    d_lng = math.radians(lng2 - lng1)
    a = math.sin(d_lat / 2) ** 2 + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(d_lng / 2) ** 2
    return round(2 * r * math.asin(math.sqrt(a)), 2)


def rfm_segment(r_recent, f_high, m_high):
    if m_high:
        if r_recent and f_high:
            return "重要价值客户"
        if not r_recent and f_high:
            return "重要保持客户"
        if r_recent and not f_high:
            return "重要发展客户"
        return "重要挽留客户"
    if r_recent and f_high:
        return "一般价值客户"
    if not r_recent and f_high:
        return "一般保持客户"
    if r_recent and not f_high:
        return "一般发展客户"
    return "一般挽留客户"


def main():
    spark = (SparkSession.builder.appName("part2-export-api")
             .config("spark.sql.shuffle.partitions", "8").getOrCreate())
    spark.sparkContext.setLogLevel("ERROR")
    os.makedirs(OUT, exist_ok=True)

    stations = spark.read.parquet(f"{DWD}/dim_stations")
    chargers = spark.read.parquet(f"{DWD}/dim_chargers")
    users = spark.read.parquet(f"{DWD}/dim_users")
    orders = spark.read.parquet(f"{DWD}/dwd_order_detail")
    hourly = spark.read.parquet(f"{DWD}/dwd_station_hourly")
    station_day = spark.read.parquet(f"{DWS}/dws_station_day")
    user_day = spark.read.parquet(f"{DWS}/dws_user_day")
    peak = spark.read.parquet(f"{ADS}/ads_peak_hour")

    def dump(name, data):
        with open(os.path.join(OUT, f"{name}.json"), "w", encoding="utf-8") as fh:
            json.dump(envelope(data), fh, ensure_ascii=False, indent=2)
        print(f"  {name}.json")

    st_rows = stations.collect()
    st_list = [{"stationId": r["id"], "name": r["name"], "district": r["district"],
                "longitude": r["longitude"], "latitude": r["latitude"],
                "priceFenPerKwh": r["price_fen_per_kwh"]} for r in st_rows]
    ch_by_station = {r["station_id"]: r for r in
                     chargers.groupBy("station_id").agg(
                         F.count("*").alias("cnt"),
                         F.sum(F.when(F.col("status") == "idle", 1).otherwise(0)).alias("idle")).collect()}
    price_avg = int(sum(s["priceFenPerKwh"] for s in st_list) / max(1, len(st_list)))

    # ---- 用户视角 ----
    dump("user_price-compare", {"cityAvgFenPerKwh": price_avg,
                                "stations": [{"stationId": s["stationId"], "name": s["name"],
                                              "priceFenPerKwh": s["priceFenPerKwh"]} for s in st_list]})
    dump("user_price-distance", sorted([{
        "stationId": s["stationId"], "name": s["name"],
        "distanceKm": haversine_km(TIANANMEN[0], TIANANMEN[1], s["latitude"], s["longitude"]),
        "priceFenPerKwh": s["priceFenPerKwh"],
        "idleCount": int((ch_by_station.get(s["stationId"]) or {"idle": 0})["idle"] or 0),
    } for s in st_list], key=lambda x: x["distanceKm"]))
    dump("user_idle-ranking", sorted([{
        "stationId": s["stationId"], "name": s["name"],
        "idleCount": int((ch_by_station.get(s["stationId"]) or {"idle": 0})["idle"] or 0),
        "chargerCount": int((ch_by_station.get(s["stationId"]) or {"cnt": 0})["cnt"] or 0),
        "idleRate": round(100.0 * ((ch_by_station.get(s["stationId"]) or {"idle": 0})["idle"] or 0)
                          / max(1, (ch_by_station.get(s["stationId"]) or {"cnt": 1})["cnt"] or 1), 1),
    } for s in st_list], key=lambda x: -x["idleCount"]))
    peak_rows = peak.orderBy("station_id", "hour").collect()
    sid_index = {s["stationId"]: i for i, s in enumerate(st_list)}
    dump("user_peak-heatmap", {
        "hours": [f"{h:02d}:00" for h in range(24)],
        "names": [s["name"] for s in st_list],
        "values": [[int(r["hour"]), sid_index[r["station_id"]], float(r["avg_busy"] or 0)]
                   for r in peak_rows if r["station_id"] in sid_index]})

    # ---- 充电站视角（每站明细，前端按 station/{id}/... 取用）----
    util_by_hour = (hourly.withColumn("hour", F.substring("observed_at", 12, 2).cast("int"))
                    .groupBy("station_id", "hour")
                    .agg(F.round(F.avg(F.col("busy_count") / F.col("pile_count")) * 100, 1).alias("util")))
    util_map = {}
    for r in util_by_hour.collect():
        util_map.setdefault(r["station_id"], {})[r["hour"]] = float(r["util"] or 0)
    mix_rows = chargers.groupBy("station_id", "type").agg(F.count("*").alias("cnt"),
                                                          F.max("power_kw").alias("kw")).collect()
    mix_map = {}
    for r in mix_rows:
        mix_map.setdefault(r["station_id"], {})[r["type"]] = (int(r["cnt"]), float(r["kw"]))
    fast_share = {r["station_id"]: float(r["share"] or 0) for r in
                  orders.join(chargers.select(F.col("id").alias("charger_id"), F.col("type").alias("charger_type")), "charger_id")
                  .groupBy("station_id").agg(
                      F.round(F.sum(F.when(F.col("charger_type") == "fast", 1).otherwise(0)) / F.count("*"), 2).alias("share")).collect()}
    health_map = {}
    for r in chargers.collect():
        e = health_map.setdefault(r["station_id"], {"faultCount": 0, "chargers": []})
        if r["status"] == "fault":
            e["faultCount"] += 1
        e["chargers"].append({"code": r["code"], "chargeCount": r["charge_count"],
                              "totalDurationSec": r["total_duration_sec"],
                              "faultFlag": 1 if r["status"] == "fault" else 0})
    detail = {}
    for s in st_list:
        sid = s["stationId"]
        cnt = int((ch_by_station.get(sid) or {"cnt": 0})["cnt"] or 0)
        fast = mix_map.get(sid, {}).get("fast", (0, 0.0))
        slow = mix_map.get(sid, {}).get("slow", (0, 0.0))
        h = health_map.get(sid, {"faultCount": 0, "chargers": []})
        detail[str(sid)] = {
            "stationId": sid, "name": s["name"], "district": s["district"], "chargerCount": cnt,
            "utilization": {"points": [{"observedAt": f"{CUTOFF}T{hr:02d}:00:00+08:00",
                                        "utilizationRate": util_map.get(sid, {}).get(hr, 0.0)}
                                       for hr in range(24)]},
            "mix": {"fastCount": fast[0], "slowCount": slow[0],
                    "fastPowerKw": fast[1] or 120.0, "slowPowerKw": slow[1] or 7.0,
                    "fastOrderShare": fast_share.get(sid, 0.0)},
            "health": {"faultRate": round(100.0 * h["faultCount"] / max(1, cnt), 1),
                       "faultCount": h["faultCount"],
                       "topChargers": sorted(h["chargers"], key=lambda x: -x["chargeCount"])[:5]},
        }
    dump("station_detail", detail)
    dump("station_coverage", [{
        "stationId": s["stationId"], "name": s["name"], "district": s["district"],
        "longitude": s["longitude"], "latitude": s["latitude"],
        "chargerCount": int((ch_by_station.get(s["stationId"]) or {"cnt": 0})["cnt"] or 0),
        # 服务半径按桩数估算（项目假设：桩越多服务半径越大），答辩需注明
        "serviceRadiusKm": round(0.8 + (int((ch_by_station.get(s["stationId"]) or {"cnt": 0})["cnt"] or 0)) / 20, 1),
    } for s in st_list])

    # ---- 企业视角（补齐 3 个）----
    growth = (user_day.groupBy("dt").agg(F.countDistinct("user_id").alias("active"))
              .join(users.withColumn("dt", F.substring("registered_at", 1, 10)).groupBy("dt")
                    .agg(F.count("*").alias("new_users")), "dt", "outer").fillna(0).orderBy("dt"))
    dump("enterprise_user-growth", {"days": growth.count(), "points": [
        {"date": r["dt"], "newUsers": int(r["new_users"]), "activeUsers": int(r["active"])} for r in growth.collect()]})
    dump("enterprise_monthly", [{
        "month": r["month"], "revenueFen": int(r["revenue_fen"]), "energyKwh": float(r["energy_kwh"]),
        "orderCount": int(r["order_cnt"]),
        "revenuePerChargerFen": int(r["revenue_fen"] / max(1, chargers.count())),
    } for r in station_day.withColumn("month", F.substring("dt", 1, 7)).groupBy("month").agg(
        F.sum("revenue_fen").alias("revenue_fen"), F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"),
        F.sum("order_cnt").alias("order_cnt")).orderBy("month").collect()])

    rfm = (user_day.groupBy("user_id").agg(
        F.sum("amount_fen").alias("m"), F.countDistinct("dt").alias("f"), F.max("dt").alias("last_dt"))
        .withColumn("r_days", F.datediff(F.lit(CUTOFF), F.to_date("last_dt"))))
    w = Window.orderBy(F.col("r_days").asc())
    rfm = rfm.withColumn("r_rank", F.ntile(2).over(w))
    rfm = rfm.withColumn("f_rank", F.ntile(2).over(Window.orderBy(F.col("f").desc())))
    rfm = rfm.withColumn("m_rank", F.ntile(2).over(Window.orderBy(F.col("m").desc())))
    seg_rows = rfm.collect()
    seg = {}
    for r in seg_rows:
        name = rfm_segment(r["r_rank"] == 1, r["f_rank"] == 1, r["m_rank"] == 1)
        e = seg.setdefault(name, {"segment": name, "userCount": 0, "revenueFen": 0})
        e["userCount"] += 1
        e["revenueFen"] += int(r["m"] or 0)
    dump("enterprise_user-rfm", sorted(seg.values(), key=lambda x: -x["userCount"]))

    # ---- 政府视角 ----
    station_by_district = stations.groupBy("district").agg(F.countDistinct("id").alias("station_cnt"))
    charger_by_district = (chargers.join(stations.select("id", "district"), chargers.station_id == stations.id, "inner")
                           .groupBy("district").agg(F.count("*").alias("charger_cnt")))
    gov_cov = station_by_district.join(charger_by_district, "district").collect()
    dump("gov_coverage", [{
        "district": r["district"], "stationCount": int(r["station_cnt"]), "chargerCount": int(r["charger_cnt"]),
        "population": POPULATION.get(r["district"], 0),
        "chargersPer10k": round(int(r["charger_cnt"]) / max(1, POPULATION.get(r["district"], 1)) * 10000, 2),
    } for r in gov_cov])
    # orders 没有 district（第一阶段 schema），需经 station_id 关联 dim_stations
    orders_district = orders.join(
        stations.select(F.col("id").alias("station_id"), "district"), "station_id")
    wait = (orders_district.filter("reserved_at IS NOT NULL AND started_at IS NOT NULL")
            .withColumn("wait_min", (F.unix_timestamp("started_at") - F.unix_timestamp("reserved_at")) / 60))
    dump("gov_service-stats", [{
        "district": r["district"], "orderCount": int(r["c"]), "servedUserCnt": int(r["u"]),
        "avgWaitMin": round(float(r["w"] or 0), 1),
    } for r in orders_district.groupBy("district").agg(
        F.count("*").alias("c"), F.countDistinct("user_id").alias("u")).join(
        wait.groupBy("district").agg(F.avg("wait_min").alias("w")), "district").collect()])
    total_kwh = float(orders.agg(F.sum("energy_kwh")).first()[0] or 0)
    dump("gov_carbon", {"totalEnergyKwh": round(total_kwh, 1),
                        "co2SavedTon": round(total_kwh / 1000 * 0.581, 1),
                        "factorTonPerMwh": 0.581,
                        "factorNote": "按全国电网平均排放因子 0.581 tCO₂/MWh 折算（项目假设，答辩需注明来源）",
                        "equivalentTrees": int(total_kwh / 1000 * 0.581 * 1000 / 18)})
    load_by_hour = (hourly.withColumn("hour", F.substring("observed_at", 12, 2))
                    .groupBy("hour").agg(F.round(F.sum("load_kw"), 1).alias("kw")).orderBy("hour"))
    dump("gov_peak-load", {"points": [{"hour": f"{r['hour']}:00", "loadKw": float(r["kw"] or 0)}
                                      for r in load_by_hour.collect()]})
    util_by_district = (hourly.join(stations.select("id", "district"), hourly.station_id == stations.id)
                        .withColumn("u", F.col("busy_count") / F.col("pile_count"))
                        .groupBy("district").agg(F.round(F.avg("u") * 100, 1).alias("util")).collect())
    ch_cnt_map = {r["district"]: int(r["charger_cnt"]) for r in charger_by_district.collect()}
    dump("gov_utilization", [{
        "district": r["district"], "utilizationRate": float(r["util"] or 0),
        "chargerCount": ch_cnt_map.get(r["district"], 0),
        "stationCount": next((int(x["station_cnt"]) for x in gov_cov if x["district"] == r["district"]), 0),
    } for r in util_by_district])

    print(f"\n全部接口 JSON 已导出到 {OUT}")
    spark.stop()


if __name__ == "__main__":
    main()
