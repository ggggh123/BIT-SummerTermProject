"""把 #5 预测交接库事务化合并到最终 ADS SQLite。

发布前校验 24 个 horizon、站点集合、物理守恒、峰值连续性、指标列集和批次；
发布时保留原库备份，并同步更新 ads_manifest.json。
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from part2.clean.normalization import SHANGHAI
from part2.common.handoff import sha256_file
from part2.scripts.publish_quality_ads import restore_database, write_manifest_atomic

FORECAST_COLUMNS = [
    "run_id", "station_id", "forecast_at", "horizon_h", "predicted_load_kw",
    "predicted_busy_count", "predicted_idle_count", "congestion_level", "is_peak",
]
METRIC_COLUMNS = ["horizon_h", "mae", "rmse", "wape", "baseline_wape"]
BATCH_COLUMNS = [
    "run_id", "model_version", "activated_at", "source", "horizon_h_max",
    "is_baseline", "note",
]


def columns(connection: sqlite3.Connection, table: str) -> list[str]:
    return [row[1] for row in connection.execute(f"PRAGMA table_info({table})")]


def load_forecast(database: Path) -> tuple[list[tuple], list[tuple], str]:
    if not database.is_file():
        raise ValueError(f"预测交接库不存在：{database}")
    with sqlite3.connect(f"file:{database.resolve()}?mode=ro", uri=True) as source:
        if columns(source, "ads_forecast_24h") != FORECAST_COLUMNS:
            raise ValueError("预测明细列集与 ads_forecast_24h 契约不一致")
        if columns(source, "ads_forecast_metric") != METRIC_COLUMNS:
            raise ValueError("预测指标列集与 ads_forecast_metric 契约不一致")
        points = list(source.execute(
            f"SELECT {','.join(FORECAST_COLUMNS)} FROM ads_forecast_24h"
        ))
        metrics = list(source.execute(
            f"SELECT {','.join(METRIC_COLUMNS)} FROM ads_forecast_metric"
        ))
    run_ids = {str(row[0]) for row in points}
    if len(run_ids) != 1 or not next(iter(run_ids), ""):
        raise ValueError("预测明细必须且只能包含一个非空 run_id")
    return points, metrics, next(iter(run_ids))


def validate_forecast_manifest(path: Path | None, forecast_db: Path, run_id: str) -> None:
    if path is None:
        return
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if str(manifest.get("run_id", "")) != run_id:
        raise ValueError("预测清单 run_id 与预测数据库不一致")
    entry = dict((manifest.get("files") or {}).get(forecast_db.name, {}))
    if entry.get("sha256") != sha256_file(forecast_db):
        raise ValueError("预测清单中的 SQLite SHA-256 不匹配")


def validate_against_ads(
    connection: sqlite3.Connection, points: list[tuple], metrics: list[tuple]
) -> None:
    for table, expected in (
        ("ads_forecast_batch", BATCH_COLUMNS),
        ("ads_forecast_24h", FORECAST_COLUMNS),
        ("ads_forecast_metric", METRIC_COLUMNS),
        ("ads_meta", ["key", "value"]),
    ):
        if columns(connection, table) != expected:
            raise ValueError(f"ADS 目标库 schema 不匹配：{table}")

    station_capacity = {
        int(row[0]): int(row[1])
        for row in connection.execute(
            "SELECT station_id, charger_cnt FROM ads_station WHERE forecast_enabled=1"
        )
    }
    if not station_capacity:
        raise ValueError("ADS 中没有 forecast_enabled=1 的存活站点")
    by_station: dict[int, list[tuple]] = {}
    for row in points:
        by_station.setdefault(int(row[1]), []).append(row)
    if set(by_station) != set(station_capacity):
        raise ValueError(
            f"预测站点集合与 ADS 不一致：forecast={sorted(by_station)} "
            f"ads={sorted(station_capacity)}"
        )
    for station_id, rows in by_station.items():
        ordered = sorted(rows, key=lambda row: int(row[3]))
        if [int(row[3]) for row in ordered] != list(range(1, 25)):
            raise ValueError(f"站点 {station_id} 未连续覆盖 horizon 1..24")
        capacity = station_capacity[station_id]
        peaks = []
        for row in ordered:
            load, busy, idle = float(row[4]), int(row[5]), int(row[6])
            if load < 0 or busy < 0 or idle < 0 or busy + idle != capacity:
                raise ValueError(f"站点 {station_id} 的负荷/桩数物理约束失败")
            if row[7] not in {"low", "medium", "high"} or int(row[8]) not in {0, 1}:
                raise ValueError(f"站点 {station_id} 的拥堵/峰值枚举非法")
            if int(row[8]) == 1:
                peaks.append(int(row[3]))
        if len(peaks) != 2 or peaks[1] != peaks[0] + 1:
            raise ValueError(f"站点 {station_id} 必须恰好标记连续两个峰值小时")

    if len(metrics) != 24 or sorted(int(row[0]) for row in metrics) != list(range(1, 25)):
        raise ValueError("预测指标必须连续覆盖 horizon 1..24")
    if any(value is None for row in metrics for value in row):
        raise ValueError("预测指标不允许空值")


def prepare_ads_manifest(path: Path, database: Path, source_run_id: str) -> dict:
    if not path.is_file():
        raise ValueError(f"ADS 清单不存在：{path}")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("contractVersion") != "ads-flask-v1":
        raise ValueError("ADS 清单 contractVersion 不匹配")
    if manifest.get("database") != database.name:
        raise ValueError("ADS 清单 database 与目标库不一致")
    if str(manifest.get("sourceRunId", "")) != source_run_id:
        raise ValueError("ADS 清单 sourceRunId 与目标库不一致")
    return manifest


def merge(
    *,
    forecast_db: Path,
    ads_db: Path,
    ads_manifest: Path,
    backup: Path,
    model_version: str,
    forecast_manifest: Path | None = None,
) -> dict:
    forecast_db, ads_db, ads_manifest, backup = map(
        Path, (forecast_db, ads_db, ads_manifest, backup)
    )
    if not ads_db.is_file() or backup.exists() or ads_db.resolve() == backup.resolve():
        raise ValueError("ADS 数据库不存在、备份已存在，或备份与原库相同")
    points, metrics, run_id = load_forecast(forecast_db)
    validate_forecast_manifest(forecast_manifest, forecast_db, run_id)

    with sqlite3.connect(f"file:{ads_db.resolve()}?mode=ro", uri=True) as source:
        target_meta = dict(source.execute("SELECT key,value FROM ads_meta"))
        validate_against_ads(source, points, metrics)
        manifest = prepare_ads_manifest(
            ads_manifest, ads_db, str(target_meta.get("sourceRunId", ""))
        )
        backup.parent.mkdir(parents=True, exist_ok=True)
        with sqlite3.connect(backup) as backup_db:
            source.backup(backup_db)

    activated_at = datetime.now(SHANGHAI).replace(microsecond=0).isoformat()
    note = "Spark MLlib 直接式 1–24h 预测；按验证集 MAE 逐 horizon 选择模型或 seasonal-naive 降级"
    with sqlite3.connect(ads_db) as target:
        try:
            target.execute("BEGIN IMMEDIATE")
            for table in ("ads_forecast_batch", "ads_forecast_24h", "ads_forecast_metric"):
                target.execute(f"DELETE FROM {table}")
            target.execute(
                "INSERT INTO ads_forecast_batch VALUES (?,?,?,?,?,?,?)",
                (run_id, model_version, activated_at, "spark-mllib", 24, 0, note),
            )
            target.executemany(
                f"INSERT INTO ads_forecast_24h ({','.join(FORECAST_COLUMNS)}) "
                f"VALUES ({','.join('?' for _ in FORECAST_COLUMNS)})",
                points,
            )
            target.executemany(
                f"INSERT INTO ads_forecast_metric ({','.join(METRIC_COLUMNS)}) VALUES (?,?,?,?,?)",
                metrics,
            )
            target.executemany(
                "INSERT OR REPLACE INTO ads_meta(key,value) VALUES (?,?)",
                (
                    ("forecastSource", "Spark MLlib direct multi-horizon"),
                    ("forecastIsBaseline", "0"),
                    ("forecastRunId", run_id),
                    ("forecastModelVersion", model_version),
                    ("forecastPublishedAt", activated_at),
                ),
            )
            validate_against_ads(target, points, metrics)
            target.commit()
        except Exception:
            target.rollback()
            raise

    manifest["tables"]["ads_forecast_batch"] = 1
    manifest["tables"]["ads_forecast_24h"] = len(points)
    manifest["tables"]["ads_forecast_metric"] = len(metrics)
    manifest["forecast"] = {
        "source": model_version,
        "runId": run_id,
        "isBaseline": False,
        "points": len(points),
        "metrics": len(metrics),
    }
    manifest["databaseSha256"] = sha256_file(ads_db)
    try:
        write_manifest_atomic(ads_manifest, manifest)
    except Exception:
        restore_database(ads_db, backup)
        raise

    return {
        "ok": True,
        "run_id": run_id,
        "model_version": model_version,
        "forecast_points": len(points),
        "metric_rows": len(metrics),
        "database": str(ads_db.resolve()),
        "database_sha256": sha256_file(ads_db),
        "backup": str(backup.resolve()),
        "manifest": str(ads_manifest.resolve()),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--forecast-db", type=Path, required=True)
    parser.add_argument("--forecast-manifest", type=Path)
    parser.add_argument("--ads-db", type=Path, required=True)
    parser.add_argument("--ads-manifest", type=Path, required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--model-version", default="spark-mllib-direct-v1")
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    if args.receipt and args.receipt.exists():
        raise ValueError(f"回执已存在：{args.receipt}")
    result = merge(
        forecast_db=args.forecast_db,
        forecast_manifest=args.forecast_manifest,
        ads_db=args.ads_db,
        ads_manifest=args.ads_manifest,
        backup=args.backup,
        model_version=args.model_version,
    )
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        with args.receipt.open("x", encoding="utf-8") as target:
            json.dump(result, target, ensure_ascii=False, indent=2)
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
