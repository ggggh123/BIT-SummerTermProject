"""执行 #5 分担的两张数仓表（dws_region_day / ads_gov_service）并做对账校验。

对账约束（《04》§3.4）：DWS 营收合计 == DWD 已完成订单金额合计（误差 0）。

    # 正式（#3 的 handoff/dwd 就位后）
    spark-submit --master yarn run_share_tables.py \
        --dwd-base hdfs://pangxiangzhen01:8020/ev-charging/dwd \
        --dws-out  hdfs://pangxiangzhen01:8020/ev-charging/dws/dws_region_day \
        --ads-out  hdfs://pangxiangzhen01:8020/ev-charging/ads/ads_gov_service

    # 自测（合成最小数据集）
    spark-submit --master yarn run_share_tables.py \
        --input-base hdfs://pangxiangzhen01:8020/ev-charging/tmp/synth_dwd \
        --dws-out hdfs://pangxiangzhen01:8020/ev-charging/tmp/dws_region_day \
        --ads-out hdfs://pangxiangzhen01:8020/ev-charging/tmp/ads_gov_service
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pyspark.sql import SparkSession

#: DWD 层三张最小输入（按《04》§3.1 命名）
DWD_TABLES = {
    "v_dwd_order": "dwd_order_detail",
    "v_dim_stations": "dim_stations",
    "v_dim_chargers": "dim_chargers",
}

#: 电网平均碳排放因子（kgCO2/kWh），来源假设见 ads_gov_service.sql 抬头
DEFAULT_CO2_FACTOR = 0.581


def register_views(spark: SparkSession, base: str) -> None:
    for view, table in DWD_TABLES.items():
        path = f"{base.rstrip('/')}/{table}"
        spark.sql(
            f"CREATE OR REPLACE TEMPORARY VIEW {view} USING parquet OPTIONS (path '{path}')"
        )
        print(f"[share] view {view:16s} -> {path}")


def render(sql_path: str, co2_factor: float, dws_view_ready: bool) -> str:
    with open(sql_path, encoding="utf-8") as fh:
        text = fh.read()
    text = (
        text.replace("{{DWD_ORDER}}", "v_dwd_order")
        .replace("{{DIM_STATIONS}}", "v_dim_stations")
        .replace("{{DIM_CHARGERS}}", "v_dim_chargers")
        .replace("{{DWS_REGION_DAY}}", "v_dws_region_day")
        .replace("{{CO2_FACTOR}}", str(co2_factor))
    )
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description="#5 分担数仓表执行与对账")
    ap.add_argument("--input-base", default="", help="DWD 三张输入所在根目录（自测时指向合成数据）")
    ap.add_argument("--dwd-base", default="", help="正式 DWD 根目录（--input-base 未给时使用）")
    ap.add_argument("--dws-out", required=True)
    ap.add_argument("--ads-out", required=True)
    ap.add_argument("--sql-dir", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--co2-factor", type=float, default=DEFAULT_CO2_FACTOR)
    ap.add_argument("--report", default="", help="对账报告 JSON 输出路径")
    args = ap.parse_args()

    base = args.input_base or args.dwd_base
    if not base:
        raise SystemExit("必须提供 --input-base 或 --dwd-base")

    spark = SparkSession.builder.appName("part2-share-tables").getOrCreate()
    spark.conf.set("spark.sql.shuffle.partitions", "8")
    print(f"[share] master={spark.sparkContext.master} app={spark.sparkContext.applicationId} base={base}")

    register_views(spark, base)

    dws_sql_path = os.path.join(args.sql_dir, "dws_region_day.sql")
    dws_sql = render(dws_sql_path, args.co2_factor, False)
    dws_df = spark.sql(dws_sql)
    dws_df.write.mode("overwrite").parquet(args.dws_out)
    dws_n = spark.read.parquet(args.dws_out).count()
    print(f"[share] dws_region_day rows={dws_n} -> {args.dws_out}")

    spark.sql(
        f"CREATE OR REPLACE TEMPORARY VIEW v_dws_region_day USING parquet OPTIONS (path '{args.dws_out}')"
    )

    ads_sql_path = os.path.join(args.sql_dir, "ads_gov_service.sql")
    ads_sql = render(ads_sql_path, args.co2_factor, True)
    ads_df = spark.sql(ads_sql)
    ads_df.write.mode("overwrite").parquet(args.ads_out)
    ads_n = spark.read.parquet(args.ads_out).count()
    print(f"[share] ads_gov_service rows={ads_n} -> {args.ads_out}")

    # ---------------- 对账（《04》§3.4 硬指标） ----------------
    dwd_revenue = spark.sql(
        "SELECT COALESCE(SUM(amount_fen), 0) AS s FROM v_dwd_order WHERE status = 'completed'"
    ).first()["s"]
    dws_revenue = spark.read.parquet(args.dws_out).agg({"revenue_fen": "sum"}).first()[0] or 0
    dwd_energy = spark.sql(
        "SELECT COALESCE(SUM(energy_kwh), 0) AS s FROM v_dwd_order WHERE status = 'completed'"
    ).first()["s"]
    dws_energy = spark.read.parquet(args.dws_out).agg({"energy_kwh": "sum"}).first()[0] or 0

    diff_revenue = int(dwd_revenue) - int(dws_revenue)
    diff_energy = round(float(dwd_energy) - float(dws_energy), 6)
    ok = diff_revenue == 0 and abs(diff_energy) < 1e-6

    report = {
        "input_base": base,
        "dws_out": args.dws_out,
        "ads_out": args.ads_out,
        "rows": {"dws_region_day": dws_n, "ads_gov_service": ads_n},
        "reconcile": {
            "dwd_revenue_fen": int(dwd_revenue),
            "dws_revenue_fen": int(dws_revenue),
            "diff_revenue_fen": diff_revenue,
            "dwd_energy_kwh": round(float(dwd_energy), 3),
            "dws_energy_kwh": round(float(dws_energy), 3),
            "diff_energy_kwh": diff_energy,
            "passed": ok,
        },
    }
    print(json.dumps(report["reconcile"], ensure_ascii=False, indent=2))

    if args.report:
        with open(args.report, "w", encoding="utf-8") as fh:
            json.dump(report, fh, ensure_ascii=False, indent=2)
        print(f"[share] 对账报告 -> {args.report}")

    spark.stop()
    print("RECONCILE_PASS" if ok else "RECONCILE_FAIL")
    print("SHARE_TABLES_DONE")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
