#!/usr/bin/env python3
"""逐层对账（《04》§3.4 硬指标）：

  DWS 营收合计 = DWD 订单金额合计（误差 0）
  ADS 各指标可由 DWD 重算复现

全部断言失败会以非零退出码结束，便于 CI/答辩现场一键验证。
输出 handoff/quality/reconcile_report.json。
"""
import json
import os
import sys

from pyspark.sql import SparkSession, functions as F

BASE = os.path.dirname(os.path.abspath(__file__))
DWD = os.environ.get("DWD_URI", "hdfs://niyujun01:8020/ev-charging/dwd")
DWS = os.environ.get("DWS_URI", "hdfs://niyujun01:8020/ev-charging/dws")
ADS = os.environ.get("ADS_URI", "hdfs://niyujun01:8020/ev-charging/ads")
OUT = os.path.join(BASE, "handoff", "quality")


def main():
    spark = (SparkSession.builder.appName("part2-reconcile")
             .config("spark.sql.shuffle.partitions", "8").getOrCreate())
    spark.sparkContext.setLogLevel("ERROR")

    orders = spark.read.parquet(f"{DWD}/dwd_order_detail")
    station_day = spark.read.parquet(f"{DWS}/dws_station_day")
    region_day = spark.read.parquet(f"{DWS}/dws_region_day")

    checks = []

    def check(name, expect, actual, tolerance=0.0):
        ok = abs(float(expect) - float(actual)) <= tolerance
        checks.append({"check": name, "expected": round(float(expect), 2),
                       "actual": round(float(actual), 2), "ok": bool(ok)})
        print(f"  [{'OK' if ok else 'FAIL'}] {name}: 期望 {expect} / 实际 {actual}")
        return ok

    # DWD 侧真值
    o_fen = orders.agg(F.sum("amount_fen")).first()[0] or 0
    o_kwh = orders.agg(F.sum("energy_kwh")).first()[0] or 0
    o_cnt = orders.count()

    # 1) DWS 站点日 vs DWD 明细
    check("DWS 营收合计 = DWD 金额合计", o_fen, station_day.agg(F.sum("revenue_fen")).first()[0] or 0)
    check("DWS 电量合计 = DWD 电量合计", o_kwh, station_day.agg(F.sum("energy_kwh")).first()[0] or 0, 1.0)
    check("DWS 订单数合计 = DWD 行数", o_cnt, station_day.agg(F.sum("order_cnt")).first()[0] or 0)

    # 2) 区域日汇总同样要能对上
    check("区域日营收合计 = DWD 金额合计", o_fen, region_day.agg(F.sum("revenue_fen")).first()[0] or 0)
    check("区域日订单合计 = DWD 行数", o_cnt, region_day.agg(F.sum("order_cnt")).first()[0] or 0)

    # 3) 粒度唯一性（业务主键不重复）
    dup = station_day.groupBy("dt", "station_id").count().filter("count > 1").count()
    check("DWS 站点日粒度唯一", 0, dup)

    # 4) 抽验：任取 3 个 (dt, station_id) 与 DWD 重算比对
    sample = station_day.orderBy(F.rand(seed=20260914)).limit(3).collect()
    for row in sample:
        recalc = (orders.filter((F.substring("started_at", 1, 10) == row["dt"]) & (F.col("station_id") == row["station_id"]))
                  .agg(F.sum("amount_fen").alias("fen"), F.count("*").alias("cnt")).first())
        check(f"抽验 {row['dt']} 站{row['station_id']} 营收", recalc["fen"] or 0, row["revenue_fen"] or 0)
        check(f"抽验 {row['dt']} 站{row['station_id']} 订单数", recalc["cnt"] or 0, row["order_cnt"] or 0)

    # 5) ADS 站点排行合计应与 DWD 对上（营收列）
    ranking = spark.read.parquet(f"{ADS}/ads_station_ranking")
    check("ADS 站点营收合计 = DWD 金额合计", o_fen, ranking.agg(F.sum("revenue_fen")).first()[0] or 0)

    failed = [c for c in checks if not c["ok"]]
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "reconcile_report.json"), "w", encoding="utf-8") as fh:
        json.dump({"checks": checks, "passed": len(checks) - len(failed), "failed": len(failed)}, fh, ensure_ascii=False, indent=2)

    print(f"\n对账：{len(checks) - len(failed)}/{len(checks)} 项通过")
    if failed:
        print("失败项：")
        for c in failed:
            print(f"  - {c['check']}（期望 {c['expected']} / 实际 {c['actual']}）")
    else:
        print("逐层对账全绿（《04》§3.4 误差 0）")
    spark.stop()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
