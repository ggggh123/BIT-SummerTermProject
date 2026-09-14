"""Part2 预测结果发布（#5 PE，分支 feat/part2-ml）。

把 HDFS 上的 ``ads_forecast_result`` 导出为交接包 ``handoff/forecast/``：

  * ``forecast_result.json`` / ``forecast_result.csv`` —— 供 #2 的 Flask 读取、大屏消费
  * ``metrics.json``（若提供）—— 训练评估报告
  * ``manifest.json`` —— 行数 + sha256 + run_id + 生成时间 + 生成机主机名（《02》§2.4 交接规范）

可选 ``--sqlite`` 把预测结果写入 SQLite 单文件中的 ``ads_forecast_result`` 表。

    spark-submit --master yarn publish.py \
        --src     hdfs://pangxiangzhen01:8020/ev-charging/ads/ads_forecast_result \
        --handoff ./handoff/forecast \
        --metrics ./tmp/metrics.json
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


def sha256_of(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description="导出预测结果交接包")
    ap.add_argument("--src", required=True, help="HDFS 上的 ads_forecast_result")
    ap.add_argument("--handoff", default="./handoff/forecast", help="交接包输出目录")
    ap.add_argument("--metrics", default="", help="metrics.json 路径（随包交付）")
    ap.add_argument("--sqlite", default="", help="可选：写入 SQLite 单文件")
    args = ap.parse_args()

    spark = SparkSession.builder.appName("part2-forecast-publish").getOrCreate()
    spark.conf.set("spark.sql.shuffle.partitions", "8")

    df = spark.read.parquet(args.src)
    rows = df.count()
    if rows == 0:
        raise SystemExit(f"ads_forecast_result 为空：{args.src}")

    pdf = df.orderBy("station_id", "horizon_h").toPandas()
    run_ids = [str(x) for x in pdf["run_id"].dropna().unique().tolist()] if "run_id" in pdf.columns else []

    out_dir = os.path.abspath(args.handoff)
    os.makedirs(out_dir, exist_ok=True)

    json_path = os.path.join(out_dir, "forecast_result.json")
    csv_path = os.path.join(out_dir, "forecast_result.csv")
    pdf.to_json(json_path, orient="records", force_ascii=False, indent=2, date_format="iso")
    pdf.to_csv(csv_path, index=False, encoding="utf-8")
    produced = [json_path, csv_path]

    if args.metrics and os.path.exists(args.metrics):
        dst = os.path.join(out_dir, "metrics.json")
        shutil.copyfile(args.metrics, dst)
        produced.append(dst)
    else:
        print(f"[publish] 未找到 metrics.json（{args.metrics}），跳过")

    sqlite_path = ""
    if args.sqlite:
        sqlite_path = os.path.abspath(args.sqlite)
        os.makedirs(os.path.dirname(sqlite_path) or ".", exist_ok=True)
        table = pdf.copy()
        if "is_peak" in table.columns:
            table["is_peak"] = table["is_peak"].astype(int)
        con = sqlite3.connect(sqlite_path)
        try:
            table.to_sql("ads_forecast_result", con, if_exists="replace", index=False)
            n = con.execute("SELECT COUNT(*) FROM ads_forecast_result").fetchone()[0]
        finally:
            con.close()
        produced.append(sqlite_path)
        print(f"[publish] SQLite ads_forecast_result 行数={n} -> {sqlite_path}")

    manifest = {
        "package": "forecast",
        "run_id": run_ids[0] if run_ids else "",
        "run_ids": run_ids,
        "generated_at": dt.datetime.now().isoformat(timespec="seconds"),
        "hostname": socket.gethostname(),
        "source": args.src,
        "rows": int(rows),
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
        print(f"[publish]   {name:26s} {info['bytes']:>8d} B  sha256={info['sha256'][:16]}…")
    print(f"[publish] manifest -> {manifest_path}")

    spark.stop()
    print("PUBLISH_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
