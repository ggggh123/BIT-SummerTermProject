"""Part2 预测结果发布（#5 PE，分支 feat/part2-ml）。

把 HDFS 上的 ``ads_forecast_24h`` 与训练产出的 ``metrics.json`` 导出为交接包
``handoff/forecast/``：

  * ``forecast_result.json`` / ``forecast_result.csv`` —— 预测明细（严格为契约列集）
  * ``forecast_metric.json`` —— ADS 契约表 ``ads_forecast_metric`` 的数据（wape ×100）
  * ``metrics.json`` —— 训练评估报告（随包交付）
  * ``manifest.json`` —— 行数 + sha256 + run_id + 生成时间 + 生成机主机名（《02》§2.4）

可选 ``--sqlite`` 把 ``ads_forecast_24h`` 与 ``ads_forecast_metric`` 两张契约表写入
SQLite 单文件，供 #4 合并进 ``handoff/ads/ads.db``。

    spark-submit --master yarn publish.py \
        --src     hdfs://pangxiangzhen01:8020/ev-charging/ads/ads_forecast_24h \
        --handoff ./handoff/forecast \
        --metrics ./tmp/metrics.json \
        --sqlite  ./handoff/forecast/forecast.db
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import socket
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pyspark.sql import SparkSession

#: 契约表列集（#4 `warehouse/sql/ads_schema.sql` §7）
FORECAST_COLUMNS = [
    "run_id", "station_id", "forecast_at", "horizon_h", "predicted_load_kw",
    "predicted_busy_count", "predicted_idle_count", "congestion_level", "is_peak",
]
METRIC_COLUMNS = ["horizon_h", "mae", "rmse", "wape", "baseline_wape"]


def sha256_of(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description="导出预测结果交接包")
    ap.add_argument("--src", required=True, help="HDFS 上的 ads_forecast_24h")
    ap.add_argument("--handoff", default="./handoff/forecast", help="交接包输出目录")
    ap.add_argument("--metrics", default="", help="metrics.json（含 ads_forecast_metric）")
    ap.add_argument("--sqlite", default="", help="可选：写入 SQLite 单文件（两张契约表）")
    args = ap.parse_args()

    spark = SparkSession.builder.appName("part2-forecast-publish").getOrCreate()
    spark.conf.set("spark.sql.shuffle.partitions", "8")

    df = spark.read.parquet(args.src)
    missing = [c for c in FORECAST_COLUMNS if c not in df.columns]
    if missing:
        raise SystemExit(f"ads_forecast_24h 缺少契约列: {missing}（实际列={df.columns}）")

    rows = df.count()
    if rows == 0:
        raise SystemExit(f"ads_forecast_24h 为空：{args.src}")

    pdf = df.select(*FORECAST_COLUMNS).orderBy("station_id", "horizon_h").toPandas()
    run_ids = [str(x) for x in pdf["run_id"].dropna().unique().tolist()]

    out_dir = os.path.abspath(args.handoff)
    os.makedirs(out_dir, exist_ok=True)

    produced: list[str] = []

    json_path = os.path.join(out_dir, "forecast_result.json")
    csv_path = os.path.join(out_dir, "forecast_result.csv")
    pdf.to_json(json_path, orient="records", force_ascii=False, indent=2)
    pdf.to_csv(csv_path, index=False, encoding="utf-8")
    produced += [json_path, csv_path]

    # ---- ads_forecast_metric（来自 metrics.json 的契约块）----
    metric_rows: list[dict] = []
    if args.metrics and os.path.exists(args.metrics):
        with open(args.metrics, encoding="utf-8") as fh:
            report = json.load(fh)
        metric_rows = report.get("ads_forecast_metric", []) or []
        dst = os.path.join(out_dir, "metrics.json")
        shutil.copyfile(args.metrics, dst)
        produced.append(dst)

    if metric_rows:
        metric_path = os.path.join(out_dir, "forecast_metric.json")
        with open(metric_path, "w", encoding="utf-8") as fh:
            json.dump(metric_rows, fh, ensure_ascii=False, indent=2)
        produced.append(metric_path)
        print(f"[publish] ads_forecast_metric rows={len(metric_rows)}")
    else:
        print("[publish] 未取得 ads_forecast_metric（metrics.json 缺该块），跳过")

    # ---- 可选 SQLite（供 #4 合并进 ads.db）----
    if args.sqlite:
        sqlite_path = os.path.abspath(args.sqlite)
        os.makedirs(os.path.dirname(sqlite_path) or ".", exist_ok=True)
        con = sqlite3.connect(sqlite_path)
        try:
            pdf.to_sql("ads_forecast_24h", con, if_exists="replace", index=False)
            n1 = con.execute("SELECT COUNT(*) FROM ads_forecast_24h").fetchone()[0]
            if metric_rows:
                import pandas as pd
                pd.DataFrame(metric_rows, columns=METRIC_COLUMNS).to_sql(
                    "ads_forecast_metric", con, if_exists="replace", index=False
                )
                n2 = con.execute("SELECT COUNT(*) FROM ads_forecast_metric").fetchone()[0]
            else:
                n2 = 0
        finally:
            con.close()
        produced.append(sqlite_path)
        print(f"[publish] SQLite: ads_forecast_24h={n1} 行, ads_forecast_metric={n2} 行 -> {sqlite_path}")

    manifest = {
        "package": "forecast",
        "contract": "ads-flask-v1 / ads_forecast_24h + ads_forecast_metric",
        "run_id": run_ids[0] if run_ids else "",
        "run_ids": run_ids,
        "generated_at": dt.datetime.now().isoformat(timespec="seconds"),
        "hostname": socket.gethostname(),
        "source": args.src,
        "rows": int(rows),
        "metric_rows": len(metric_rows),
        "files": {
            os.path.basename(p): {"sha256": sha256_of(p), "bytes": os.path.getsize(p)}
            for p in produced
        },
    }
    manifest_path = os.path.join(out_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, ensure_ascii=False, indent=2)

    print(f"[publish] rows={rows} -> {out_dir}")
    for name, info in manifest["files"].items():
        print(f"[publish]   {name:24s} {info['bytes']:>8d} B  sha256={info['sha256'][:16]}…")
    print(f"[publish] manifest -> {manifest_path}")

    spark.stop()
    print("PUBLISH_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
