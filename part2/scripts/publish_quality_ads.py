"""将 #3 的逐行对账结果事务化写入 #4 既有 ADS SQLite 质量三表。"""
import argparse
import json
import os
import sqlite3
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from part2.clean.normalization import SHANGHAI
from part2.common.handoff import sha256_file

TABLE_COLUMNS = {
    "ads_quality_table": ["name", "rows_before", "rows_after"],
    "ads_quality_issue": ["rule", "type", "injected", "detected", "handled", "recall"],
    "ads_quality_meta": ["key", "value"],
    "ads_meta": ["key", "value"],
}
SCML_DWD_TABLES = {"dwd_order_detail", "dwd_telemetry_detail", "dwd_station_hourly", "dwd_event", "dim_stations", "dim_chargers", "dim_users"}


def load_reports(quality_path, cleaning_path):
    quality = json.loads(Path(quality_path).read_text(encoding="utf-8"))
    cleaning = json.loads(Path(cleaning_path).read_text(encoding="utf-8"))
    keys = ("run_id", "source_run_id", "contract_version", "policy_version", "dwd_profile")
    if quality.get("report_type") != "quality_report" or cleaning.get("report_type") != "cleaning_report" or any(quality.get(key) != cleaning.get(key) for key in keys):
        raise ValueError("质量与清洗报告不是同一成功批次")
    assertions = cleaning.get("dwd_assertions", [])
    if quality.get("dwd_profile") != "scml-dwd-v0.1" or len(assertions) != 7 or {item.get("table") for item in assertions} != SCML_DWD_TABLES or not all(item.get("ok") for item in assertions):
        raise ValueError("仅允许发布通过七表读回断言的 SCML 兼容批次")
    return quality, cleaning


def schema_columns(connection, table):
    return [row[1] for row in connection.execute(f"PRAGMA table_info({table})")]


def quality_payload(quality, cleaning, quality_path):
    table_rows = [("ods_" + name, int(item["before"]), int(item["after"])) for name, item in cleaning["tables"].items()]
    issue_rows = []
    exact = {}
    for number in range(1, 11):
        source_rule, target_rule = f"Q{number}", f"R{number:02d}"
        item = quality["injection_comparison"][source_rule]
        injected = int(item["truePositive"] + item["falseNegative"])
        detected = int(item["truePositive"] + item["falsePositive"])
        recall = float(item["recall"]) if item["recall"] is not None else 0.0
        issue_rows.append((target_rule, quality["rules"][source_rule]["name"], injected, detected, detected, recall))
        exact[target_rule] = {"truePositive": int(item["truePositive"]), "falsePositive": int(item["falsePositive"]), "falseNegative": int(item["falseNegative"]), "recall": item["recall"], "precision": item["precision"]}
    meta = {
        "source": "prl-quality-report",
        "qualityRunId": quality["run_id"],
        "sourceRunId": quality["source_run_id"],
        "contractVersion": quality["contract_version"],
        "sourceContractVersion": quality.get("source_contract_version", ""),
        "policyVersion": quality["policy_version"],
        "dwdProfile": quality["dwd_profile"],
        "qualityReportSha256": sha256_file(quality_path),
        "readyForTeamDelivery": json.dumps(bool(quality.get("ready_for_team_delivery"))),
        "exactMetrics": json.dumps(exact, ensure_ascii=False, separators=(",", ":")),
        "tableDetails": json.dumps({
            "ods_" + name: {"cascadeAffectedRows": item.get("cascade_affected_rows")}
            for name, item in cleaning["tables"].items()
        }, ensure_ascii=False, separators=(",", ":")),
        "pendingPolicyNotes": json.dumps(quality.get("pending_policy_notes", []), ensure_ascii=False, separators=(",", ":")),
        "metricSemantics": "injected=TP+FN; detected=handled=TP+FP; recall=TP/(TP+FN); precision/FP/FN 见 exactMetrics",
        "publishedAt": datetime.now(SHANGHAI).isoformat(),
    }
    return table_rows, issue_rows, sorted(meta.items())


def prepare_manifest(manifest_path, database, quality, table_rows, issue_rows, accept_pending):
    if manifest_path is None:
        return None, None
    manifest_path = Path(manifest_path)
    if not manifest_path.is_file():
        raise ValueError(f"ADS 清单不存在：{manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("contractVersion") != "ads-flask-v1":
        raise ValueError("ADS 清单 contractVersion 不是 ads-flask-v1")
    if manifest.get("database") != database.name:
        raise ValueError("ADS 清单中的 database 与发布目标不一致")
    if str(manifest.get("sourceRunId", "")) != str(quality["source_run_id"]):
        raise ValueError("ADS 清单与质量报告来源批次不一致")

    tables = dict(manifest.get("tables") or {})
    tables["ads_quality_table"] = len(table_rows)
    tables["ads_quality_issue"] = len(issue_rows)
    manifest["tables"] = tables
    manifest["quality"] = {
        "source": "prl-quality-report",
        "runId": quality["run_id"],
        "injected": sum(row[2] for row in issue_rows),
        "detected": sum(row[3] for row in issue_rows),
        "readyForTeamDelivery": bool(quality.get("ready_for_team_delivery")),
        "acceptedPendingPolicy": bool(
            accept_pending and not quality.get("ready_for_team_delivery")
        ),
    }
    return manifest_path, manifest


def write_manifest_atomic(manifest_path, manifest):
    temporary = manifest_path.with_name(f".{manifest_path.name}.{os.getpid()}.tmp")
    temporary.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    os.replace(temporary, manifest_path)


def restore_database(database, backup):
    source = sqlite3.connect(f"file:{backup.resolve()}?mode=ro", uri=True)
    target = sqlite3.connect(database)
    try:
        source.backup(target)
    finally:
        target.close()
        source.close()


def publish(
    quality_path, cleaning_path, database, backup, accept_pending=False, manifest_path=None
):
    quality_path, cleaning_path, database, backup = map(Path, (quality_path, cleaning_path, database, backup))
    quality, cleaning = load_reports(quality_path, cleaning_path)
    if not quality.get("ready_for_team_delivery") and not accept_pending:
        raise ValueError("报告仍有未确认策略；试联调需显式 --accept-pending，不能冒充正式发布")
    if not database.is_file() or backup.exists() or database.resolve() == backup.resolve():
        raise ValueError("ADS 数据库不存在、备份已存在，或备份目标与原库相同")
    table_rows, issue_rows, meta_rows = quality_payload(quality, cleaning, quality_path)
    manifest_path, manifest = prepare_manifest(
        manifest_path, database, quality, table_rows, issue_rows, accept_pending
    )

    backup.parent.mkdir(parents=True, exist_ok=True)
    source = sqlite3.connect(f"file:{database.resolve()}?mode=ro", uri=True)
    try:
        for table, columns in TABLE_COLUMNS.items():
            if schema_columns(source, table) != columns:
                raise ValueError(f"ADS SQLite schema 不匹配：{table}")
        meta = dict(source.execute("SELECT key, value FROM ads_meta"))
        ads_source_run = meta.get("sourceRunId") or meta.get("runId")
        if ads_source_run != quality["source_run_id"]:
            raise ValueError(f"ADS 与质量报告来源批次不一致：{ads_source_run} != {quality['source_run_id']}")
        target = sqlite3.connect(backup)
        try:
            source.backup(target)
        finally:
            target.close()
    finally:
        source.close()
    connection = sqlite3.connect(database)
    try:
        connection.execute("PRAGMA foreign_keys=ON")
        connection.execute("BEGIN IMMEDIATE")
        for table in ("ads_quality_table", "ads_quality_issue", "ads_quality_meta"):
            connection.execute(f"DELETE FROM {table}")
        connection.executemany("INSERT INTO ads_quality_table VALUES (?,?,?)", table_rows)
        connection.executemany("INSERT INTO ads_quality_issue VALUES (?,?,?,?,?,?)", issue_rows)
        connection.executemany("INSERT INTO ads_quality_meta VALUES (?,?)", meta_rows)
        stored = dict(connection.execute("SELECT key, value FROM ads_quality_meta"))
        if connection.execute("SELECT COUNT(*) FROM ads_quality_table").fetchone()[0] != 7 or connection.execute("SELECT COUNT(*) FROM ads_quality_issue").fetchone()[0] != 10 or stored.get("qualityReportSha256") != sha256_file(quality_path):
            raise ValueError("ADS 质量表写后读回断言失败")
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()

    if manifest is not None:
        manifest["databaseSha256"] = sha256_file(database)
        try:
            write_manifest_atomic(manifest_path, manifest)
        except Exception:
            restore_database(database, backup)
            raise

    return {
        "ok": True,
        "database": str(database.resolve()),
        "backup": str(backup.resolve()),
        "manifest": str(manifest_path.resolve()) if manifest_path else None,
        "source_run_id": quality["source_run_id"],
        "quality_run_id": quality["run_id"],
        "quality_report_sha256": sha256_file(quality_path),
        "database_sha256": sha256_file(database),
        "tables": 7,
        "rules": 10,
        "ready_for_team_delivery": bool(quality.get("ready_for_team_delivery")),
        "accepted_pending_policy": bool(
            accept_pending and not quality.get("ready_for_team_delivery")
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quality", type=Path, required=True)
    parser.add_argument("--cleaning", type=Path, required=True)
    parser.add_argument("--ads-db", type=Path, required=True)
    parser.add_argument(
        "--ads-manifest", type=Path,
        help="可选 ads_manifest.json；提供时与数据库一并更新并校验批次",
    )
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--accept-pending", action="store_true")
    args = parser.parse_args()
    if args.receipt and args.receipt.exists():
        raise ValueError(f"回执已存在，拒绝在发布后才失败：{args.receipt}")
    result = publish(
        args.quality, args.cleaning, args.ads_db, args.backup,
        args.accept_pending, args.ads_manifest,
    )
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        with args.receipt.open("x", encoding="utf-8") as target:
            json.dump(result, target, ensure_ascii=False, indent=2)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
