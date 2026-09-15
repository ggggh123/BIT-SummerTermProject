#!/usr/bin/env python3
"""把 HDFS 上的 ADS Parquet 合并 Python 旁路表，导出为 `handoff/ads/ads.db`。

这是「ADS → SQLite 单文件」那一步的实现（《04-SCML》§3.3、《02-TL》§3.2），
交付给 #2 的 Flask 用标准库 `sqlite3` **只读**打开。

    spark-submit warehouse/jobs/export_ads_db.py \\
        --warehouse-root /ev-charging \\
        --ods handoff/ods \\
        --out handoff/ads

### 它为什么要读 ODS

7 张业务指标表来自 Spark（`ads_etl.sql`）；另外 8 张（`ads_meta`、
`ads_quality_*`、`ads_forecast_*`、`ads_event`）由 Python 产出，理由见
`ads_etl.sql` 文件头的职责边界说明。其中的质量对账必须**独立复算 ODS**，
所以这一步要读一次 ODS 交接包（不是重跑 Spark）。

`--local-parquet` 可在没有 HDFS 时指向本地 Parquet 目录，便于自测与演示。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _ads_extras as extras  # noqa: E402
from _lib import CN_TZ, load_schema, write_ads_db  # noqa: E402
from build_local import (  # noqa: E402  复用 ODS 读取与清洗，不重写第二套
    load_chargers,
    load_events_and_faults,
    load_orders,
    load_station_hourly,
    load_stations,
    load_users,
    scan_telemetry,
)

# Spark 侧产出的 7 张业务指标表
SPARK_TABLES = [
    "ads_station",
    "ads_charger",
    "ads_daily",
    "ads_station_day",
    "ads_station_hourly",
    "ads_user_rfm",
    "ads_district",
]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_ads_manifest(
    *,
    db_path: Path,
    payload: dict[str, list[dict]],
    meta: dict,
    quality_issue: list[dict],
    batch: dict | None,
    data_window: dict,
) -> dict:
    """为 Spark 导出路径生成与本地同构路径一致的 ADS 交接清单。"""
    return {
        "contractVersion": "ads-flask-v1",
        "runId": meta["runId"],
        "generatedAt": meta["generatedAt"],
        "source": "ods-handoff",
        "sourceRunId": meta["sourceRunId"],
        "dataWindow": data_window,
        "database": db_path.name,
        "databaseSha256": sha256_file(db_path),
        "tables": {name: len(rows) for name, rows in sorted(payload.items())},
        "quality": {
            "injected": sum(int(row["injected"]) for row in quality_issue),
            "detected": sum(int(row["detected"]) for row in quality_issue),
        },
        "forecast": {
            "source": batch["model_version"] if batch else "none",
            "isBaseline": bool(batch),
        },
    }


def write_json_atomic(path: Path, payload: dict) -> None:
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def read_parquet_tables(spark, root: str) -> dict[str, list[dict]]:
    """把 7 张 Parquet 表读成行字典。规模都是万级以下，`collect()` 安全。"""
    tables: dict[str, list[dict]] = {}
    for name in SPARK_TABLES:
        path = f"{root}/ads/{name}"
        rows = [row.asDict(recursive=True) for row in spark.read.parquet(path).collect()]
        tables[name] = rows
        print(f"[read] {path}: {len(rows)} rows", flush=True)
    return tables


def main() -> int:
    parser = argparse.ArgumentParser(description="ADS Parquet + Python 旁路表 -> handoff/ads/ads.db")
    parser.add_argument("--warehouse-root", default="/ev-charging",
                        help="HDFS 根（Spark 侧），或配合 --local-parquet 指向本地目录")
    parser.add_argument("--local-parquet", action="store_true",
                        help="按本地路径读 Parquet，不启动 Spark 连接 HDFS")
    parser.add_argument("--ods", type=Path, default=Path("handoff/ods"))
    parser.add_argument("--out", type=Path, default=Path("handoff/ads"))
    parser.add_argument("--generated-at", type=str, default=None)
    args = parser.parse_args()

    generated_at = (
        datetime.fromisoformat(args.generated_at).replace(tzinfo=CN_TZ)
        if args.generated_at
        else datetime.now(CN_TZ).replace(microsecond=0)
    )

    from pyspark.sql import SparkSession

    builder = SparkSession.builder.appName("ev-scml-ads-export").enableHiveSupport()
    if args.local_parquet:
        builder = builder.master("local[1]")
    spark = builder.getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    try:
        tables = read_parquet_tables(spark, args.warehouse_root)
    finally:
        spark.stop()

    # ---- Python 旁路表：独立复算 ODS ----
    ods_dir: Path = args.ods
    manifest_path = ods_dir / "manifest.json"
    ods_manifest = (
        json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else {}
    )
    rows_before = {
        name: int(meta.get("rows", 0)) for name, meta in (ods_manifest.get("tables") or {}).items()
    }
    injection_path = ods_dir / "injection_log.json"
    injection = (
        json.loads(injection_path.read_text(encoding="utf-8"))
        if injection_path.exists()
        else {}
    )

    detected: Counter = Counter()
    stations = load_stations(ods_dir, detected)
    chargers, charger_reference = load_chargers(ods_dir, detected)
    users = load_users(ods_dir, detected)
    orders = load_orders(ods_dir, users, stations, charger_reference, chargers, detected)
    hourly = load_station_hourly(ods_dir, stations, detected)
    rated_power = {cid: int(row["power_kw"]) for cid, row in chargers.items()}
    telemetry_total, telemetry_removed = scan_telemetry(ods_dir, rated_power, detected)
    orders_by_id = {order["order_id"]: order for order in orders}
    events, _faults = load_events_and_faults(
        ods_dir, orders_by_id, stations, chargers, charger_reference, detected
    )

    quality_table, quality_issue, quality_meta = extras.quality_rows(
        injection,
        detected,
        rows_before,
        {
            "ods_orders": len(orders),
            "ods_station_hourly": len(hourly),
            "ods_users": len(users),
            "ods_chargers": len(chargers),
            "ods_stations": len(stations),
            "ods_events": len(events),
        },
        telemetry_total,
        telemetry_removed,
        generated_at,
    )
    # 业务 ADS 站点来自正式 DWD，可能已有维度行因质量规则被剔除。预测必须只引用
    # 仍存在于 ads_station 的站点，否则会生成无法下钻的孤儿预测点。
    ads_station_ids = {int(row["station_id"]) for row in tables["ads_station"]}
    forecast_stations = {
        station_id: row for station_id, row in stations.items()
        if station_id in ads_station_ids
    }
    forecast_hourly = [
        row for row in hourly if int(row["station_id"]) in ads_station_ids
    ]
    batch, forecast_points = extras.build_baseline_forecast(
        forecast_hourly, forecast_stations, generated_at
    )
    metrics = (
        extras.build_baseline_metrics(forecast_hourly, forecast_stations) if batch else []
    )

    # ---- 窗口与总量：取自 Spark 侧产出的 ads_daily，保证 meta 与业务表自洽 ----
    daily = sorted(tables["ads_daily"], key=lambda row: row["dt"])
    window_start = daily[0]["dt"] if daily else ""
    window_end = daily[-1]["dt"] if daily else ""
    total_revenue = sum(int(row["revenue_fen"]) for row in daily)
    total_energy = round(sum(float(row["energy_kwh"]) for row in daily), 3)
    total_orders = sum(int(row["order_cnt"]) for row in daily)

    run_id = f"ads-{generated_at.strftime('%Y%m%d%H%M%S')}"
    meta = extras.build_meta(
        run_id=run_id,
        generated_at=generated_at,
        ods_manifest=ods_manifest,
        window_start=window_start,
        window_end=window_end,
        window_days=len(daily) or 1,
        station_count=len(tables["ads_station"]),
        charger_count=len(tables["ads_charger"]),
        user_count=len(users),
        order_count=total_orders,
        total_revenue_fen=total_revenue,
        total_energy_kwh=total_energy,
        batch=batch,
        producer="warehouse/jobs/export_ads_db.py（Spark 侧指标表 + Python 侧旁路表）",
        first_order_user_count=len({order["user_id"] for order in orders}),
    )

    payload = dict(tables)
    payload["ads_meta"] = [{"key": k, "value": str(v)} for k, v in meta.items()]
    payload["ads_quality_table"] = quality_table
    payload["ads_quality_issue"] = quality_issue
    payload["ads_quality_meta"] = quality_meta
    payload["ads_event"] = events
    if batch:
        payload["ads_forecast_batch"] = [{
            "run_id": batch["run_id"],
            "model_version": batch["model_version"],
            "activated_at": batch["activated_at"],
            "source": batch["source"],
            "horizon_h_max": batch["horizon_h_max"],
            "is_baseline": batch["is_baseline"],
            "note": batch["note"],
        }]
        payload["ads_forecast_24h"] = [{**p, "run_id": batch["run_id"]} for p in forecast_points]
        payload["ads_forecast_metric"] = metrics

    args.out.mkdir(parents=True, exist_ok=True)
    db_path = args.out / "ads.db"
    write_ads_db(db_path, payload, load_schema())

    data_window = {"start": window_start, "end": window_end, "days": len(daily)}
    manifest = build_ads_manifest(
        db_path=db_path,
        payload=payload,
        meta=meta,
        quality_issue=quality_issue,
        batch=batch,
        data_window=data_window,
    )
    manifest_path = args.out / "ads_manifest.json"
    write_json_atomic(manifest_path, manifest)

    result = {
        "ok": True,
        "database": str(db_path),
        "manifest": str(manifest_path),
        "databaseSha256": manifest["databaseSha256"],
        "runId": run_id,
        "tables": {name: len(rows) for name, rows in sorted(payload.items())},
        "dataWindow": data_window,
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
