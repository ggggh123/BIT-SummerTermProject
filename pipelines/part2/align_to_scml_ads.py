#!/usr/bin/env python3
"""把本骨架产出的 ADS 结果**对齐到 #4 的 ads_schema.sql**（以 SCML 分支为准）。

背景：`part2/scml/warehouse/sql/ads_schema.sql`（分支 `feat/part2_SCML`）定义了权威 ADS 表结构。
本脚本读我方 DWS/DWD 结果，按他的字段名与粒度重写为同名 ADS 表，便于：
  1) 我方 `export_api.py`（接口 JSON）与他的数仓共用同一份表；
  2) 未来他的作业产出后，直接替换本脚本，下游不动。

差异对照（我方 → 他的）：
  ads_station_ranking：name→station_name、utilization_rate→avg_utilization、idle_cnt→idle_charger_cnt（+dt 日粒度 + rank_no）
  ads_peak_hour：avg_busy→busy_count，补 pile_count / load_kw / utilization（+dt）
  ads_charger_health：站级透视 → 桩级明细（charger_code/status/fault_cnt/order_cnt/energy_kwh/charge_duration_sec）
  ads_gov_service：co2_saved_ton → carbon_reduction_kg；补 served_user_cnt
  ads_user_profile_rfm：补 avg_recency_days / avg_frequency / avg_monetary_fen
  ads_revenue_overview：单行 KPI → 日粒度（dt 主键）+ active_user_cnt
"""
import os

from pyspark.sql import SparkSession, Window, functions as F

BASE = os.path.dirname(os.path.abspath(__file__))
DWD = os.environ.get("DWD_URI", "hdfs://niyujun01:8020/ev-charging/dwd")
DWS = os.environ.get("DWS_URI", "hdfs://niyujun01:8020/ev-charging/dws")
ADS_OUT = os.environ.get("SCML_ADS_URI", "hdfs://niyujun01:8020/ev-charging/ads_scml")
CUTOFF = "2026-09-14"


def main():
    spark = (SparkSession.builder.appName("part2-align-scml-ads")
             .config("spark.sql.shuffle.partitions", "8").getOrCreate())
    spark.sparkContext.setLogLevel("ERROR")

    orders = spark.read.parquet(f"{DWD}/dwd_order_detail")
    stations = spark.read.parquet(f"{DWD}/dim_stations")
    chargers = spark.read.parquet(f"{DWD}/dim_chargers")
    hourly = spark.read.parquet(f"{DWD}/dwd_station_hourly")
    station_day = spark.read.parquet(f"{DWS}/dws_station_day")

    # ---- ads_revenue_overview（日粒度）----
    daily = (orders.withColumn("dt", F.substring("started_at", 1, 10))
             .groupBy("dt").agg(F.sum("amount_fen").alias("revenue_fen"),
                                F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"),
                                F.count("*").alias("order_cnt"),
                                F.countDistinct("user_id").alias("active_user_cnt")))
    # 注意：DWD 已按「单价×电量」重算金额，这里的 revenue_fen 即订单金额合计
    daily = daily.withColumn("revenue_fen", F.col("revenue_fen").cast("long"))

    # ---- ads_station_ranking（日粒度 + rank_no）----
    rank = (station_day.join(stations.select("id", "name", "district"), station_day.station_id == stations.id)
            .withColumn("avg_utilization", F.lit(0.0)))  # 占位：利用率来自 hourly，见下
    util = (hourly.withColumn("dt", F.substring("observed_at", 1, 10))
            .groupBy("dt", "station_id")
            .agg(F.round(F.avg(F.col("busy_count") / F.col("pile_count")) * 100, 2).alias("avg_utilization")))
    idle = (chargers.filter("status = 'idle'").groupBy("station_id").agg(F.count("*").alias("idle_charger_cnt")))
    ranking = (station_day.join(stations.select("id", "name", "district"), station_day.station_id == stations.id)
               .join(util, ["dt", "station_id"], "left")
               .join(idle, "station_id", "left")
               .select(F.col("dt"), F.col("station_id"), F.col("name").alias("station_name"),
                       F.col("district"), F.col("revenue_fen").cast("long").alias("revenue_fen"),
                       F.col("energy_kwh"), F.col("order_cnt").cast("int"),
                       F.coalesce("avg_utilization", F.lit(0.0)).alias("avg_utilization"),
                       F.coalesce("idle_charger_cnt", F.lit(0)).cast("int").alias("idle_charger_cnt"))
               .withColumn("rank_no", F.dense_rank().over(Window.partitionBy("dt").orderBy(F.col("revenue_fen").desc()))))

    # ---- ads_peak_hour（补 pile_count / load_kw / utilization）----
    peak = (hourly.withColumn("dt", F.substring("observed_at", 1, 10))
            .withColumn("hour", F.substring("observed_at", 12, 2).cast("int"))
            .groupBy("dt", "station_id", "hour")
            .agg(F.round(F.avg("busy_count"), 0).cast("int").alias("busy_count"),
                 F.round(F.avg("pile_count"), 0).cast("int").alias("pile_count"),
                 F.round(F.avg("load_kw"), 2).alias("load_kw"))
            .withColumn("utilization", F.round(F.col("busy_count") / F.col("pile_count") * 100, 2)))

    # ---- ads_charger_health（桩级明细）----
    ch_orders = (orders.groupBy("charger_id").agg(F.count("*").alias("order_cnt"),
                                                  F.round(F.sum("energy_kwh"), 2).alias("energy_kwh")))
    health = (chargers.join(ch_orders, chargers.id == ch_orders.charger_id, "left")
              .select(F.lit(CUTOFF).alias("dt"), F.col("id").cast("int").alias("charger_id"),
                      F.col("station_id").cast("int"), F.col("code").alias("charger_code"),
                      F.col("status"),
                      F.when(F.col("status") == "fault", 1).otherwise(0).alias("fault_cnt"),
                      F.coalesce("order_cnt", F.lit(0)).cast("int").alias("order_cnt"),
                      F.coalesce("energy_kwh", F.lit(0.0)).alias("energy_kwh"),
                      F.col("total_duration_sec").cast("long").alias("charge_duration_sec")))

    # ---- ads_gov_service（carbon_reduction_kg + served_user_cnt）----
    od = orders.join(stations.select(F.col("id").alias("station_id"), "district"), "station_id")
    gov = (od.groupBy("district").agg(F.countDistinct("station_id").alias("station_cnt"),
                                      F.count("*").alias("order_cnt"),
                                      F.round(F.sum("energy_kwh"), 2).alias("energy_kwh"),
                                      F.sum("amount_fen").cast("long").alias("revenue_fen"),
                                      F.countDistinct("user_id").alias("served_user_cnt")))
    ch_cnt = (chargers.join(stations.select("id", "district"), chargers.station_id == stations.id)
              .groupBy("district").agg(F.count("*").alias("charger_cnt")))
    gov = (gov.join(ch_cnt, "district", "left")
           .withColumn("dt", F.lit(CUTOFF))
           .withColumn("carbon_reduction_kg", F.round(F.col("energy_kwh") / 1000 * 581, 1))
           .select("dt", "district", "station_cnt", "charger_cnt", "order_cnt", "energy_kwh",
                   "revenue_fen", "served_user_cnt", "carbon_reduction_kg"))

    # ---- ads_user_profile_rfm（补 R/F/M 均值）----
    rfm = (spark.read.parquet(f"{DWS}/dws_user_day")
           .groupBy("user_id").agg(F.sum("amount_fen").alias("m"), F.countDistinct("dt").alias("f"),
                                   F.max("dt").alias("last_dt"))
           .withColumn("r_days", F.datediff(F.lit(CUTOFF), F.to_date("last_dt")))
           .withColumn("r_rank", F.ntile(2).over(Window.orderBy("r_days")))
           .withColumn("f_rank", F.ntile(2).over(Window.orderBy(F.col("f").desc())))
           .withColumn("m_rank", F.ntile(2).over(Window.orderBy(F.col("m").desc()))))
    # 与 export_api.py 相同的 8 段命名口径
    seg = (F.when((F.col("m_rank") == 1) & (F.col("r_rank") == 1) & (F.col("f_rank") == 1), "重要价值客户")
           .when((F.col("m_rank") == 1) & (F.col("r_rank") == 2) & (F.col("f_rank") == 1), "重要保持客户")
           .when((F.col("m_rank") == 1) & (F.col("r_rank") == 1) & (F.col("f_rank") == 2), "重要发展客户")
           .when((F.col("m_rank") == 1), "重要挽留客户")
           .when((F.col("r_rank") == 1) & (F.col("f_rank") == 1), "一般价值客户")
           .when((F.col("r_rank") == 2) & (F.col("f_rank") == 1), "一般保持客户")
           .when((F.col("r_rank") == 1), "一般发展客户")
           .otherwise("一般挽留客户"))
    rfm = (rfm.withColumn("segment", seg).groupBy("segment")
           .agg(F.count("*").alias("user_cnt"), F.round(F.avg("r_days"), 1).alias("avg_recency_days"),
                F.round(F.avg("f"), 2).alias("avg_frequency"), F.round(F.avg("m"), 1).alias("avg_monetary_fen"))
           .withColumn("dt", F.lit(CUTOFF))
           .select("dt", "segment", "user_cnt", "avg_recency_days", "avg_frequency", "avg_monetary_fen"))

    for name, df in [("ads_revenue_overview", daily), ("ads_station_ranking", ranking),
                     ("ads_peak_hour", peak), ("ads_charger_health", health),
                     ("ads_gov_service", gov), ("ads_user_profile_rfm", rfm)]:
        df.write.mode("overwrite").parquet(f"{ADS_OUT}/{name}")
        print(f"  {name}: {df.count()} 行 → {ADS_OUT}/{name}")

    print(f"\n已按 ads_schema.sql 对齐输出到 {ADS_OUT}")
    spark.stop()


if __name__ == "__main__":
    main()
