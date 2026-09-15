"""生成确定性的 PRL 测试夹具；不是 #4 的正式 ODS 数据生成器。"""
import argparse
import csv
import json
from pathlib import Path
from part2.common.contracts import load_contract, raw_columns, validate_record
from part2.common.handoff import sha256_file, count_rows, verify_handoff


def fixture_records():
    tables = {table: [] for table in load_contract()["tables"]}
    def add(table, row_id, **values):
        row = {name: None for name in raw_columns(table)}
        row.update({key: str(value) if value is not None else None for key, value in values.items()})
        row["_row_id"] = row_id
        validate_record(table, row)
        tables[table].append(row)
        return row
    findings = []
    def mark(rule, table, row):
        findings.append({"rule": rule, "table": table, "row_id": row["_row_id"]})
    stamp = "2026-09-01T08:00:00+08:00"
    for index in range(1, 4):
        user = add("users", f"u{index}", id=index, mobile=f"1380000000{index}", nickname=f"测试用户{index}", avatar_path="", balance_fen=10000, status="active", registered_at="2026-06-01T00:00:00+08:00")
        if index == 3:
            user["mobile"] = "138000000X"
            mark("Q8", "users", user)
        station = add("stations", f"s{index}", id=index, name=f"测试站{index}", address="北京市朝阳区测试路", latitude="39.92", longitude="116.46", price_fen_per_kwh=100, forecast_enabled=0, created_at="2026-06-01T00:00:00+08:00", district="朝阳区")
        if index == 2:
            station["latitude"] = "31.2"
            mark("Q9", "stations", station)
        if index == 3:
            station["name"] = "  Ａ测试站  "
            mark("Q10", "stations", station)
        add("chargers", f"c{index}", id=index, station_id=1 if index < 3 else 3, code=f"TEST-{index}", type="fast", power_kw=60, status="reserved" if index == 1 else "charging" if index == 2 else "idle", charge_count=0, total_duration_sec=0, updated_at=stamp)
    for index in range(1, 10):
        day = f"2026-08-{index:02d}"
        order = add("orders", f"o{index}", id=index, user_id=1, charger_id=1, status="completed", reserved_at=f"{day}T08:00:00+08:00", started_at=f"{day}T08:05:00+08:00", ended_at=f"{day}T08:35:00+08:00", energy_kwh="10", amount_fen="1000")
        if index == 2:
            order.update(status="reserved", reserved_at=stamp, started_at=None, ended_at=None, energy_kwh="0", amount_fen="0")
        elif index == 3:
            order.update(status="charging", user_id="2", charger_id="2", reserved_at=stamp, started_at=stamp, ended_at=None, energy_kwh="0", amount_fen="0")
        elif index == 4:
            order.update(status="cancelled", started_at=None, energy_kwh="0", amount_fen="0")
        elif index == 5:
            order["ended_at"] = None
            mark("Q1", "orders", order)
        elif index == 6:
            order["reserved_at"] = f"{day.replace('-', '/')} 08:00:00"
            mark("Q4", "orders", order)
        elif index == 7:
            order["ended_at"] = f"{day}T08:01:00+08:00"
            mark("Q5", "orders", order)
        elif index == 8:
            order["amount_fen"] = "10.00"
            mark("Q6", "orders", order)
        elif index == 9:
            order["charger_id"] = "999"
            mark("Q7", "orders", order)
    duplicate = dict(tables["orders"][0], _row_id="o1-copy")
    tables["orders"].append(duplicate)
    mark("Q2", "orders", duplicate)
    for index in (1, 2):
        telemetry = add("telemetry", f"t{index}", id=index, charger_id=2, recorded_at=f"2026-09-01T08:{index*5:02d}:00+08:00", power_kw=60 if index == 1 else 600, energy_increment_kwh=5, event_type="charging")
        if index == 2:
            mark("Q3", "telemetry", telemetry)
        add("station_hourly", f"h{index}", station_id=1, observed_at=f"2026-09-01T0{index+6}:00:00+08:00", pile_count=2, rated_power_kw=120, temperature_c=25, is_holiday=0, busy_count=1, load_kw=60)
    add("events", "e1", id=1, event_type="order_completed", entity_type="order", entity_id=1, message="PRL 测试夹具事件，不是真实运营数据", created_at=stamp)
    return tables, findings


def write_fixture(directory):
    root = Path(directory)
    if root.exists() and any(root.iterdir()):
        raise ValueError(f"输出目录非空，拒绝覆盖：{root}")
    root.mkdir(parents=True, exist_ok=True)
    contract = load_contract()
    tables, findings = fixture_records()
    files = []
    for table, records in tables.items():
        fmt = contract["tables"][table]["format"]
        path = root / f"{table}.{fmt}"
        with path.open("w", encoding="utf-8", newline="") as target:
            if fmt == "csv":
                writer = csv.DictWriter(target, fieldnames=raw_columns(table), lineterminator="\n")
                writer.writeheader()
                writer.writerows({key: contract["csv_null"] if value is None else value for key, value in row.items()} for row in records)
            else:
                for row in records:
                    target.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
        files.append({"path": path.name, "table": table, "format": fmt, "rows": len(records), "sha256": sha256_file(path)})
    log = root / "injection_log.json"
    log.write_text(json.dumps({"kind": "prl-test-fixture", "expected_findings": findings}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    files.append({"path": log.name, "format": "json", "rows": count_rows(log, "json"), "sha256": sha256_file(log)})
    manifest = {"contract_version": contract["contract_version"], "kind": "prl-test-fixture", "run_id": "prl-fixture-v1", "seed": 20260914, "generated_at": "2026-09-14T00:00:00+08:00", "generator": "part2.scripts.make_fixture", "files": files}
    (root / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    verify_handoff(root)
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = write_fixture(args.output)
    print(json.dumps({"ok": True, "kind": result["kind"], "tables": len(result["files"])-1, "output": str(args.output)}, ensure_ascii=False))
