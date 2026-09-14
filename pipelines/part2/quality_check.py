#!/usr/bin/env python3
"""PySpark 数据质量探查（老师第 3 步）。

ODS 全部字段按 StringType 读入（避免脏数据污染 schema 推断），输出：
  handoff/quality/quality_report.json   机器可读（供大屏「数据质量」面板）
  handoff/quality/quality_report.md     人读报告（答辩证据）
并与 gen_ods.py 的 injection_log.json 做注入/检出对账。
"""
import json
import os
import sys

from pyspark.sql import SparkSession, functions as F

BASE = os.path.dirname(os.path.abspath(__file__))
ODS = os.environ.get("ODS_URI", f"{BASE}/handoff/ods")
OUT = os.path.join(BASE, "handoff", "quality")
ISO = r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+08:00$"
BEIJING_BOX = "latitude BETWEEN 39.4 AND 41.1 AND longitude BETWEEN 115.4 AND 117.5"


def read(spark, name):
    return spark.read.option("header", True).option("inferSchema", False).csv(f"{ODS}/{name}.csv")


def rules(spark):
    """返回 [{rule, type, table, detected, sample}]"""
    out = []

    def add(rule, typ, table, count, sample_rows):
        out.append({
            "rule": rule,
            "type": typ,
            "table": table,
            "detected": int(count),
            "sample": [{k: (str(v) if v is not None else None) for k, v in r.asDict().items()} for r in sample_rows],
        })

    orders = read(spark, "orders")
    telemetry = read(spark, "telemetry")
    users = read(spark, "users")
    chargers = read(spark, "chargers")
    stations = read(spark, "stations")
    hourly = read(spark, "station_hourly_history")

    # R01 缺失值
    c = orders.filter((F.col("status") == "completed") & (F.coalesce(F.col("ended_at"), F.lit("")) == "")).count()
    add("R01", "缺失值", "orders", c, orders.filter((F.col("status") == "completed") & (F.coalesce(F.col("ended_at"), F.lit("")) == "")).limit(5).collect())
    c = telemetry.filter(F.coalesce(F.col("power_kw"), F.lit("")) == "").count()
    add("R01", "缺失值", "telemetry", c, telemetry.filter(F.coalesce(F.col("power_kw"), F.lit("")) == "").limit(5).collect())

    # R02 重复记录
    dup = orders.groupBy("id").count().filter("count > 1").count()
    add("R02", "重复记录", "orders", dup, [])
    dup_t = telemetry.groupBy("charger_id", "recorded_at").count().filter("count > 1").count()
    add("R02", "重复记录", "telemetry", dup_t, [])

    # R03 异常值
    bad = orders.filter((F.col("energy_kwh").cast("double") < 0) | (F.col("energy_kwh").cast("double") > 500))
    add("R03", "异常值", "orders", bad.count(), bad.limit(5).collect())
    bad_t = telemetry.filter((F.col("power_kw").cast("double") > 200) | (F.col("energy_increment_kwh").cast("double") < 0))
    add("R03", "异常值", "telemetry", bad_t.count(), bad_t.limit(5).collect())

    # R04 时间格式不一致（非 ISO）
    bad = orders.filter(~F.col("started_at").rlike(ISO))
    add("R04", "时间格式混杂", "orders", bad.count(), bad.limit(5).collect())
    bad_t = telemetry.filter(~F.col("recorded_at").rlike(ISO))
    add("R04", "时间格式混杂", "telemetry", bad_t.count(), bad_t.limit(5).collect())

    # R05 逻辑矛盾
    bad = orders.filter(F.col("started_at").rlike(ISO) & F.col("ended_at").rlike(ISO) &
                        (F.to_timestamp("ended_at") < F.to_timestamp("started_at")))
    add("R05", "逻辑矛盾", "orders", bad.count(), bad.limit(5).collect())
    bad_h = hourly.filter(F.col("busy_count").cast("int") > F.col("pile_count").cast("int"))
    add("R05", "逻辑矛盾", "station_hourly_history", bad_h.count(), bad_h.limit(5).collect())

    # R06 金额口径（与 单价×电量 偏差 >1%）
    price = stations.select(F.col("id").alias("sid"), F.col("price_fen_per_kwh").cast("double").alias("price"))
    charge = chargers.select(F.col("id").alias("charger_id"), F.col("station_id").alias("sid"))
    joined = (orders.filter(F.col("energy_kwh").cast("double") > 0)
              .join(charge, "charger_id", "left").join(price, "sid", "left")
              .withColumn("expect", F.col("energy_kwh").cast("double") * F.col("price")))
    bad = joined.filter(F.abs(F.col("amount_fen").cast("double") - F.col("expect")) > F.col("expect") * 0.01)
    add("R06", "金额口径错误", "orders", bad.count(), bad.select("id", "energy_kwh", "amount_fen", "expect").limit(5).collect())

    # R07 孤儿引用
    bad = orders.join(chargers.select(F.col("id").alias("cid")), orders.charger_id == F.col("cid"), "left").filter("cid IS NULL")
    add("R07", "孤儿引用", "orders", bad.count(), bad.select("id", "charger_id").limit(5).collect())
    bad_u = orders.join(users.select(F.col("id").alias("uid")), orders.user_id == F.col("uid"), "left").filter("uid IS NULL")
    add("R07", "孤儿引用", "orders", bad_u.count(), bad_u.select("id", "user_id").limit(5).collect())

    # R08 非法字段值
    bad = users.filter(~F.col("mobile").rlike(r"^1\d{10}$"))
    add("R08", "非法字段值", "users", bad.count(), bad.select("id", "mobile").limit(5).collect())
    bad_c = chargers.filter(~F.col("status").isin("idle", "reserved", "charging", "fault", "restarting"))
    add("R08", "非法字段值", "chargers", bad_c.count(), bad_c.select("id", "status").limit(5).collect())

    # R09 经纬度越界
    bad = stations.filter(f"NOT ({BEIJING_BOX})")
    add("R09", "经纬度越界", "stations", bad.count(), bad.select("id", "latitude", "longitude").limit(5).collect())

    # R10 文本脏数据
    bad = stations.filter((F.trim(F.col("name")) != F.col("name")) | F.col("name").rlike("[　　]"))
    add("R10", "文本脏数据", "stations", bad.count(), bad.select("id", "name").limit(5).collect())
    bad_u = users.filter(F.trim(F.col("nickname")) != F.col("nickname"))
    add("R10", "文本脏数据", "users", bad_u.count(), bad_u.select("id", "nickname").limit(5).collect())

    return out


def main():
    spark = (SparkSession.builder.appName("part2-quality-check")
             .config("spark.sql.shuffle.partitions", "8").getOrCreate())
    spark.sparkContext.setLogLevel("ERROR")

    tables = ["stations", "chargers", "users", "orders", "telemetry", "station_hourly_history", "events"]
    profile = []
    for t in tables:
        df = read(spark, t)
        row = df.count()
        nulls = df.select([F.sum(F.when(F.coalesce(F.col(c), F.lit("")) == "", 1).otherwise(0)).alias(c) for c in df.columns]).first().asDict()
        profile.append({"table": t, "rows": int(row), "null_rate": {k: round((v or 0) / max(1, row), 4) for k, v in nulls.items()}})
        print(f"  画像 {t}: {row} 行")

    detected = rules(spark)
    report = {"profiles": profile, "rules": detected}

    # 与注入日志对账
    inj_path = os.path.join(BASE, "handoff", "ods", "injection_log.json")
    if os.path.exists(inj_path):
        inj = json.load(open(inj_path, encoding="utf-8"))["issues"]
        mapping = {"R01": "Q1", "R02": "Q2", "R03": "Q3", "R04": "Q4", "R05": "Q5",
                   "R06": "Q6", "R07": "Q7", "R08": "Q8", "R09": "Q9", "R10": "Q10"}
        recon = []
        for r, q in mapping.items():
            injected = inj.get(q, {}).get("injected", 0)
            got = sum(x["detected"] for x in detected if x["rule"] == r)
            recon.append({"rule": r, "type": next((x["type"] for x in detected if x["rule"] == r), ""),
                          "injected": injected, "detected": got,
                          "recall": round(min(1.0, got / injected), 2) if injected else None})
        report["reconciliation"] = recon
    else:
        report["reconciliation"] = []

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "quality_report.json"), "w", encoding="utf-8") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2)

    lines = ["# 数据质量探查报告", "",
             "| 规则 | 类型 | 注入 | 检出 | 召回 |", "|---|---|---|---|---|"]
    for r in report["reconciliation"]:
        lines.append(f"| {r['rule']} | {r['type']} | {r['injected']} | {r['detected']} | {r['recall']} |")
    lines += ["", "## 各表画像", "", "| 表 | 行数 | 空值率（前 3 列） |", "|---|---|---|"]
    for p in profile:
        top = sorted(p["null_rate"].items(), key=lambda kv: -kv[1])[:3]
        lines.append(f"| {p['table']} | {p['rows']} | {', '.join(f'{k}={v}' for k, v in top)} |")
    with open(os.path.join(OUT, "quality_report.md"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    total = sum(x["detected"] for x in detected)
    print(f"\n10 类规则累计命中 {total} 条，报告输出到 {OUT}")
    for r in report["reconciliation"]:
        print(f"  {r['rule']} {r['type']}: 注入 {r['injected']} / 检出 {r['detected']}")
    spark.stop()


if __name__ == "__main__":
    sys.exit(main())
