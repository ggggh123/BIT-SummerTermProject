"""Part2 预测结果发布（#5 PE，分支 feat/part2-ml）。

把 HDFS 上的 ``ads_forecast_24h`` 与训练产出的 ``metrics.json`` 导出为交接包
``handoff/forecast/``，并生成 #4 契约（``part2/scml/warehouse/sql/ads_schema.sql`` §7）
的**三张预测表**数据：

  * ``ads_forecast_batch``  —— 批次元数据；``is_baseline = 0`` 表示真实 Spark MLlib 批次，
    用于替换 #4 当前的 seasonal-naive 降级基线（Flask ``/api/forecast/*`` 靠它定位激活批次）
  * ``ads_forecast_24h``    —— 未来 24h 逐步长预测明细（启用站点 × 24 步长）
  * ``ads_forecast_metric`` —— 1–24 步长回测指标（``wape`` 按契约 ×100）

导出物：``forecast_result.json`` / ``.csv``、``forecast_metric.json``、
``forecast_batch.json``、``metrics.json``、``manifest.json``（行数 + sha256 + run_id + 主机名）。

可选 ``--sqlite`` 写入 SQLite 单文件，供 #4 合并进 ``handoff/ads/ads.db``。

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
BATCH_COLUMNS = ["run_id", "model_version", "activated_at", "source",
                 "horizon_h_max", "is_baseline", "note"]
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
    ap.add_argument("--sqlite", default="", help="可选：写入 SQLite 单文件（三张契约表）")
    ap.add_argument("--model-version", default="", help="批次模型版本号，默认 gbt-<spark 版本>")
    ap.add_argument("--source", default="spark-mllib", help="批次来源标识")
    ap.add_argument("--note", default="", help="批次备注")
    ap.add_argument("--is-baseline", type=int, default=0, choices=[0, 1],
                    help="1=seasonal-naive 降级基线；0=真实 MLlib 批次")
    args = ap.parse_args()

    spark = SparkSession.builder.appName("part2-forecast-publish").getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    spark.conf.set("spark.sql.shuffle.partitions", "8")
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")

    df = spark.read.parquet(args.src)
    missing = [c for c in FORECAST_COLUMNS if c not in df.columns]
    if missing:
        raise SystemExit(f"ads_forecast_24h 缺少契约列: {missing}（实际列={df.columns}）")

    rows = df.count()
    if rows == 0:
        raise SystemExit(f"ads_forecast_24h 为空：{args.src}")

    pdf = df.select(*FORECAST_COLUMNS).orderBy("station_id", "horizon_h").toPandas()
    run_ids = [str(x) for x in pdf["run_id"].dropna().unique().tolist()]
    run_id = run_ids[0] if run_ids else ""
    horizon_max = int(pdf["horizon_h"].max()) if len(pdf) else 0

    out_dir = os.path.abspath(args.handoff)
    os.makedirs(out_dir, exist_ok=True)
    produced: list[str] = []

    json_path = os.path.join(out_dir, "forecast_result.json")
    csv_path = os.path.join(out_dir, "forecast_result.csv")
    pdf.to_json(json_path, orient="records", force_ascii=False, indent=2)
    pdf.to_csv(csv_path, index=False, encoding="utf-8")
    produced += [json_path, csv_path]

    # ---- metrics.json → ads_forecast_metric ----
    metric_rows: list[dict] = []
    spark_version = spark.version
    if args.metrics and os.path.exists(args.metrics):
        with open(args.metrics, encoding="utf-8") as fh:
            report = json.load(fh)
        metric_rows = report.get("ads_forecast_metric", []) or []
        spark_version = report.get("spark_version", spark_version)
        dst = os.path.join(out_dir, "metrics.json")
        shutil.copyfile(args.metrics, dst)
        produced.append(dst)

    if metric_rows:
        metric_path = os.path.join(out_dir, "forecast_metric.json")
        with open(metric_path, "w", encoding="utf-8") as fh:
            json.dump(metric_rows, fh, ensure_ascii=False, indent=2)
        produced.append(metric_path)

    # ---- ads_forecast_batch（交接入口表）----
    batch = {
        "run_id": run_id,
        "model_version": args.model_version or f"gbt-{spark_version}",
        "activated_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "source": args.source,
        "horizon_h_max": horizon_max,
        "is_baseline": int(args.is_baseline),
        "note": args.note or (
            f"Spark MLlib GBT 直接式多步长（1-{horizon_max}h）；"
            "特征含滞后/滚动项，训练/验证/测试按时间序 70/15/15 切分，无未来数据泄漏"
        ),
    }
    batch_path = os.path.join(out_dir, "forecast_batch.json")
    with open(batch_path, "w", encoding="utf-8") as fh:
        json.dump(batch, fh, ensure_ascii=False, indent=2)
    produced.append(batch_path)

    # ---- 可选 SQLite ----
    if args.sqlite:
        import pandas as pd

        sqlite_path = os.path.abspath(args.sqlite)
        os.makedirs(os.path.dirname(sqlite_path) or ".", exist_ok=True)
        con = sqlite3.connect(sqlite_path)
        try:
            # 真实批次整表替换基线的语义：先清掉旧的同批/基线行再写
            con.execute("DROP TABLE IF EXISTS ads_forecast_batch")
            con.execute("DROP TABLE IF EXISTS ads_forecast_24h")
            con.execute("DROP TABLE IF EXISTS ads_forecast_metric")
            pd.DataFrame([batch], columns=BATCH_COLUMNS).to_sql(
                "ads_forecast_batch", con, if_exists="replace", index=False)
            pdf.to_sql("ads_forecast_24h", con, if_exists="replace", index=False)
            if metric_rows:
                pd.DataFrame(metric_rows, columns=METRIC_COLUMNS).to_sql(
                    "ads_forecast_metric", con, if_exists="replace", index=False)
            counts = {
                t: con.execute(f"SELECT COUNT(*) FROM {t}").fetchone()[0]
                for t in ("ads_forecast_batch", "ads_forecast_24h", "ads_forecast_metric")
            }
        finally:
            con.close()
        produced.append(sqlite_path)
        print(f"[publish] SQLite 契约表行数: {counts} -> {sqlite_path}")

    manifest = {
        "package": "forecast",
        "contract": "ads-flask-v1 / ads_forecast_batch + ads_forecast_24h + ads_forecast_metric",
        "run_id": run_id,
        "run_ids": run_ids,
        "model_version": batch["model_version"],
        "is_baseline": batch["is_baseline"],
        "horizon_h_max": horizon_max,
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

    print(f"[publish] run_id={run_id} rows={rows} horizon_h_max={horizon_max} "
          f"is_baseline={batch['is_baseline']} model_version={batch['model_version']}")
    for name, info in manifest["files"].items():
        print(f"[publish]   {name:24s} {info['bytes']:>8d} B  sha256={info['sha256'][:16]}…")
    print(f"[publish] manifest -> {manifest_path}")

    spark.stop()
    print("PUBLISH_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
