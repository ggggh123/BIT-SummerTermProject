#!/usr/bin/env python3
"""SCML 本地交付体检。

用于把 `handoff/ods`、`handoff/dws`、`handoff/ads/ads.db` 的状态汇总成一段
可贴给队友/评审的结论。DWD 属于 #3 交付，默认缺失时记为 skip；加
`--require-dwd` 后才视为失败。
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import subprocess
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[1]
JOBS = ROOT / "warehouse" / "jobs"

sys.path.insert(0, str(ROOT / "scripts"))
from validate_handoff import validate as validate_ods  # noqa: E402


DWS_TABLES = ("dws_station_day", "dws_charger_day", "dws_user_day", "dws_region_day")
ADS_TABLES = (
    "ads_meta", "ads_station", "ads_charger", "ads_daily", "ads_station_day",
    "ads_station_hourly", "ads_user_rfm", "ads_district", "ads_quality_table",
    "ads_quality_issue", "ads_quality_meta", "ads_forecast_batch",
    "ads_forecast_24h", "ads_forecast_metric", "ads_event",
)


def ok(message: str, **extra: object) -> dict[str, object]:
    return {"status": "ok", "message": message, **extra}


def fail(message: str, **extra: object) -> dict[str, object]:
    return {"status": "fail", "message": message, **extra}


def skip(message: str, **extra: object) -> dict[str, object]:
    return {"status": "skip", "message": message, **extra}


def load_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def check_ods(ods: Path) -> dict[str, object]:
    errors = validate_ods(ods)
    if errors:
        return fail("; ".join(errors[:3]), errorCount=len(errors))
    manifest = load_json(ods / "manifest.json")
    tables = manifest.get("tables") or {}
    rows = {name: data.get("rows", 0) for name, data in tables.items() if isinstance(data, dict)}
    return ok(
        "ODS manifest/hash/partition/injection checks passed",
        kind=manifest.get("kind"),
        runId=manifest.get("runId") or manifest.get("run_id"),
        tables=rows,
    )


def check_dws(dws: Path) -> dict[str, object]:
    manifest_path = dws / "manifest.json"
    if not manifest_path.exists():
        return fail(f"missing {manifest_path}")
    manifest = load_json(manifest_path)
    if manifest.get("kind") != "dws-handoff":
        return fail("DWS manifest kind must be dws-handoff")

    missing = [table for table in DWS_TABLES if not (dws / table / "part-00000.csv").exists()]
    if missing:
        return fail("missing DWS table files", missing=missing)

    tables = manifest.get("tables") or {}
    rows = {table: dict(tables.get(table, {})).get("rows", 0) for table in DWS_TABLES}
    return ok("DWS four-table handoff is present", tables=rows)


def check_ads(ads: Path) -> dict[str, object]:
    db_path = ads / "ads.db"
    if not db_path.exists():
        return fail(f"missing {db_path}")
    if not (ads / "ads_manifest.json").exists():
        return fail(f"missing {ads / 'ads_manifest.json'}")

    try:
        with sqlite3.connect(f"{db_path.resolve().as_uri()}?mode=ro", uri=True) as db:
            actual = {row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            missing = [table for table in ADS_TABLES if table not in actual]
            if missing:
                return fail("ADS database is missing contract tables", missing=missing)
            meta = dict(db.execute("SELECT key, value FROM ads_meta"))
    except sqlite3.Error as exc:
        return fail(f"cannot read ads.db: {exc}")

    return ok(
        "ADS SQLite database is readable",
        db=str(db_path),
        runId=meta.get("runId"),
        stationCount=int(meta.get("stationCount", 0)),
        chargerCount=int(meta.get("chargerCount", 0)),
        orderCount=int(meta.get("orderCount", 0)),
    )


def check_dwd(dwd: Path, require_dwd: bool) -> dict[str, object]:
    if dwd.exists():
        return ok("DWD handoff directory is present", path=str(dwd))
    if require_dwd:
        return fail(f"missing {dwd}")
    return skip(f"{dwd} not delivered yet; D group reconciliation is skipped")


def check_reconcile(ods: Path, dws: Path, ads: Path, dwd: Path, require_dwd: bool) -> dict[str, object]:
    args = [
        sys.executable, str(JOBS / "reconcile.py"),
        "--ods", str(ods),
        "--dws", str(dws),
        "--ads", str(ads / "ads.db"),
        "--dwd", str(dwd),
    ]
    if require_dwd:
        args.append("--require-dwd")
    result = subprocess.run(args, capture_output=True, text=True, encoding="utf-8")
    stdout = result.stdout
    if result.returncode != 0:
        return fail("reconciliation failed", detail=(stdout + result.stderr).strip()[-1200:])
    skipped_dwd = "DWD 订单表" in stdout and ("跳过" in stdout or "SKIP" in stdout)
    return ok(
        "reconciliation passed",
        dwdGroup="skip" if skipped_dwd else "ok",
        summary=next((line.strip() for line in stdout.splitlines() if line.strip().startswith("共 ")), ""),
    )


def build_report(args: argparse.Namespace) -> dict[str, object]:
    checks = {
        "ods": check_ods(args.ods),
        "dws": check_dws(args.dws),
        "ads": check_ads(args.ads),
        "dwd": check_dwd(args.dwd, args.require_dwd),
    }
    if all(item["status"] in {"ok", "skip"} for item in checks.values()):
        checks["reconcile"] = check_reconcile(args.ods, args.dws, args.ads, args.dwd, args.require_dwd)

    status = "fail" if any(item["status"] == "fail" for item in checks.values()) else "ready"
    return {"status": status, "checks": checks}


def print_human(report: dict[str, object]) -> None:
    print(f"SCML delivery: {report['status']}")
    for name, check in report["checks"].items():
        marker = {"ok": "OK", "skip": "SKIP", "fail": "FAIL"}[check["status"]]
        print(f"[{marker}] {name}: {check['message']}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check SCML handoff readiness.")
    parser.add_argument("--ods", type=Path, default=Path("handoff/ods"))
    parser.add_argument("--dws", type=Path, default=Path("handoff/dws"))
    parser.add_argument("--ads", type=Path, default=Path("handoff/ads"))
    parser.add_argument("--dwd", type=Path, default=Path("handoff/dwd"))
    parser.add_argument("--require-dwd", action="store_true")
    parser.add_argument("--json", action="store_true", help="print machine-readable report")
    args = parser.parse_args(argv)

    report = build_report(args)
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        print_human(report)
    return 0 if report["status"] == "ready" else 1


if __name__ == "__main__":
    raise SystemExit(main())
