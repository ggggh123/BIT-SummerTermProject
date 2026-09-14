from __future__ import annotations

import argparse
import csv
import hashlib
import json
import random
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path


CN_TZ = timezone(timedelta(hours=8))
NULL_TEXT = r"\N"

# Telemetry is sampled on a 5-minute grid; every frame lands on a grid slot.
TELEMETRY_SLOT_MINUTES = 5

# ODS tables that are written as Hive-style `dt=YYYY-MM-DD` partition directories.
# Dimension-style snapshot tables (stations / chargers / users) stay flat on purpose.
PARTITION_COLUMNS: dict[str, str] = {
    "ods_orders": "reserved_at",
    "ods_telemetry": "recorded_at",
    "ods_station_hourly": "observed_at",
    "ods_events": "created_at",
}

# 10 injected quality-issue classes defined by the #4 design document.
# A rule can hit more than one table; the injection log must record every hit so
# that #3 can reconcile by (rule, table, row_id).
QUALITY_RULES: dict[str, dict[str, object]] = {
    "Q1": {
        "rate": 0.005,
        "tables": ("ods_orders", "ods_telemetry"),
        "description": "缺失值：completed 订单 ended_at 置空 / 遥测 power_kw 置空",
    },
    "Q2": {
        "rate": 0.010,
        "tables": ("ods_orders", "ods_telemetry"),
        "description": "重复记录：整行复制，模拟报文重传",
    },
    "Q3": {
        "rate": 0.003,
        "tables": ("ods_orders", "ods_telemetry"),
        "description": "异常值：energy_kwh 置负或 9999 / power_kw 放大 10 倍",
    },
    "Q4": {
        "rate": 0.010,
        "tables": ("ods_orders", "ods_telemetry"),
        "description": "时间格式混杂：yyyy/MM/dd HH:mm:ss 或 Unix 时间戳",
    },
    "Q5": {
        "rate": 0.003,
        "tables": ("ods_orders", "ods_station_hourly"),
        "description": "逻辑矛盾：started_at/ended_at 倒置 / busy_count > pile_count",
    },
    "Q6": {
        "rate": 0.005,
        "tables": ("ods_orders",),
        "description": "金额口径错误：amount_fen 按元写入（÷100）",
    },
    "Q7": {
        "rate": 0.003,
        "tables": ("ods_orders",),
        "description": "孤儿引用：user_id 指向不存在的用户",
    },
    "Q8": {
        "rate": 0.003,
        "tables": ("ods_users", "ods_chargers"),
        "description": "非法字段值：手机号含字母 / 充电桩状态写 unknown",
    },
    "Q9": {
        "rate": 0.010,
        "tables": ("ods_stations",),
        "description": "经纬度越界：站点纬度移出北京市范围",
    },
    "Q10": {
        "rate": 0.005,
        "tables": ("ods_stations", "ods_users"),
        "description": "文本脏数据：站名/昵称前后空格与全角字符",
    },
}

TABLE_FORMATS: dict[str, str] = {"ods_events": "jsonl"}


@dataclass(frozen=True)
class GenerationConfig:
    seed: int = 20260914
    station_count: int = 8
    chargers_per_station: int = 36
    user_count: int = 5000
    order_count: int = 120000
    telemetry_count: int = 1000000
    history_days: int = 90
    event_count: int = 20000
    start_date: str = "2026-09-01"


def iso_at(base: datetime, **delta: int) -> str:
    return (base + timedelta(**delta)).isoformat(timespec="seconds")


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, object]]) -> dict[str, object]:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows([{key: _external_value(row.get(key)) for key in fieldnames} for row in rows])
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return {"rows": len(rows), "sha256": digest, "path": path.as_posix()}


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> dict[str, object]:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps({key: _external_value(value) for key, value in row.items()}, ensure_ascii=False) + "\n")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return {"rows": len(rows), "sha256": digest, "path": path.as_posix()}


def _external_value(value: object) -> object:
    return NULL_TEXT if value is None else value


def _with_row_ids(table: str, rows: list[dict[str, object]]) -> list[dict[str, object]]:
    for index, row in enumerate(rows, 1):
        row["_row_id"] = f"{table}-{index:09d}"
    return rows


def _date_of(value: object) -> str | None:
    """Return the +08:00 calendar date of an ISO timestamp, or None if unparsable."""
    if value is None:
        return None
    try:
        return datetime.fromisoformat(str(value)).astimezone(CN_TZ).date().isoformat()
    except ValueError:
        return None


def _partition_map(table: str, rows: list[dict[str, object]]) -> dict[str, str]:
    """Map _row_id -> dt. Computed BEFORE injection so dirty timestamps do not move a row."""
    column = PARTITION_COLUMNS.get(table)
    if column is None:
        return {}
    return {str(row["_row_id"]): (_date_of(row.get(column)) or "unknown") for row in rows}


def _write_table_file(fmt: str, path: Path, fields: list[str], rows: list[dict[str, object]]) -> dict[str, object]:
    if fmt == "jsonl":
        return write_jsonl(path, rows)
    return write_csv(path, fields, rows)


def generate_handoff(config: GenerationConfig, handoff_dir: Path) -> dict[str, object]:
    rng = random.Random(config.seed)
    handoff_dir.mkdir(parents=True, exist_ok=True)
    base = datetime.fromisoformat(config.start_date).replace(tzinfo=CN_TZ)

    stations = _stations(config, rng, base)
    chargers = _chargers(config, rng, base, stations)
    users = _users(config, rng, base)
    orders = _orders(config, rng, base, users, chargers, stations)
    telemetry = _telemetry(config, rng, base, chargers)
    station_hourly = _station_hourly(config, rng, base, stations)
    events = _events(config, rng, base, orders, chargers)

    rows_by_table = {
        "ods_stations": stations,
        "ods_chargers": chargers,
        "ods_users": users,
        "ods_orders": orders,
        "ods_telemetry": telemetry,
        "ods_station_hourly": station_hourly,
        "ods_events": events,
    }

    # Partition keys come from the clean timestamps; injection happens afterwards.
    partition_keys = {name: _partition_map(name, rows) for name, rows in rows_by_table.items()}
    injection_log = _inject_quality_issues(config, rows_by_table)

    table_manifest: dict[str, object] = {}
    files: list[dict[str, object]] = []
    for table, rows in rows_by_table.items():
        fmt = TABLE_FORMATS.get(table, "csv")
        fields = list(rows[0].keys()) if rows else []
        extension = "jsonl" if fmt == "jsonl" else "csv"
        table_dir = handoff_dir / table
        table_dir.mkdir(parents=True, exist_ok=True)
        written: list[dict[str, object]] = []

        if table in PARTITION_COLUMNS:
            grouped: dict[str, list[dict[str, object]]] = {}
            for row in rows:
                grouped.setdefault(partition_keys[table][str(row["_row_id"])], []).append(row)
            for dt in sorted(grouped):
                part_dir = table_dir / f"dt={dt}"
                meta = _write_table_file(fmt, part_dir / f"part-00000.{extension}", fields, grouped[dt])
                meta.update({"path": f"{table}/dt={dt}/part-00000.{extension}", "table": table, "format": fmt, "dt": dt})
                files.append(meta)
                written.append(meta)
                (part_dir / "_SUCCESS").write_text("", encoding="utf-8")
        else:
            meta = _write_table_file(fmt, table_dir / f"part-00000.{extension}", fields, rows)
            meta.update({"path": f"{table}/part-00000.{extension}", "table": table, "format": fmt})
            files.append(meta)
            written.append(meta)

        (table_dir / "_SUCCESS").write_text("", encoding="utf-8")
        table_manifest[table] = {
            "rows": sum(int(item["rows"]) for item in written),
            "format": fmt,
            "partitioned": table in PARTITION_COLUMNS,
            "partitionColumn": PARTITION_COLUMNS.get(table),
            "partitions": [
                {"dt": item.get("dt"), "rows": item["rows"], "path": item["path"], "sha256": item["sha256"]}
                for item in written
                if item.get("dt")
            ],
            "files": [item["path"] for item in written],
            "sha256": hashlib.sha256(
                "|".join(f"{item['path']}:{item['sha256']}" for item in written).encode("utf-8")
            ).hexdigest(),
            "path": table,
        }

    generation_time = iso_at(base, hours=config.history_days * 24)
    injection_meta = {
        "path": "injection_log.json",
        "table": "injection_log",
        "format": "json",
        "rows": len(injection_log["issues"]),
        "sha256": "",
    }
    manifest = {
        "contractVersion": "ods-dwd-v0.1",
        "kind": "ods-handoff" if config.order_count >= 100000 else "prl-test-fixture",
        "runId": f"scml-{config.seed}",
        "run_id": f"scml-{config.seed}",
        "seed": config.seed,
        "generatedAt": generation_time,
        "generated_at": generation_time,
        "sourceKind": "simulated",
        "dataWindow": {
            "start": base.isoformat(timespec="seconds"),
            "end": generation_time,
            "days": config.history_days,
        },
        "tables": table_manifest,
        "files": files,
    }
    injection_path = handoff_dir / "injection_log.json"
    injection_path.write_text(
        json.dumps(injection_log, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    injection_meta["sha256"] = hashlib.sha256(injection_path.read_bytes()).hexdigest()
    files.append(injection_meta)
    (handoff_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest


def _stations(config: GenerationConfig, rng: random.Random, base: datetime) -> list[dict[str, object]]:
    districts = ["Chaoyang", "Haidian", "Fengtai", "Tongzhou", "Daxing"]
    rows = []
    for station_id in range(1, config.station_count + 1):
        district = districts[(station_id - 1) % len(districts)]
        rows.append(
            {
                "_row_id": "",
                "id": station_id,
                "name": f"{district} Station {station_id}",
                "address": f"Beijing {district} Road {station_id}",
                "district": district,
                "latitude": round(39.75 + rng.random() * 0.35, 6),
                "longitude": round(116.10 + rng.random() * 0.55, 6),
                "price_fen_per_kwh": rng.choice([80, 100, 120, 140, 160]),
                "forecast_enabled": 1 if station_id <= min(6, config.station_count) else 0,
                "created_at": iso_at(base, days=-90 + station_id),
            }
        )
    return _with_row_ids("stations", rows)


def _chargers(config: GenerationConfig, rng: random.Random, base: datetime, stations: list[dict[str, object]]) -> list[dict[str, object]]:
    rows = []
    charger_id = 1
    for station in stations:
        for index in range(config.chargers_per_station):
            fast = index < round(config.chargers_per_station * 0.4)
            power = rng.choice([60, 120]) if fast else rng.choice([7, 30])
            rows.append(
                {
                    "_row_id": "",
                    "id": charger_id,
                    "station_id": station["id"],
                    "code": f"C{charger_id:05d}",
                    "type": "fast" if fast else "slow",
                    "power_kw": power,
                    "status": "fault" if charger_id % 97 == 0 else "idle",
                    "charge_count": rng.randint(0, 250),
                    "total_duration_sec": rng.randint(0, 3600 * 500),
                    "updated_at": iso_at(base, hours=rng.randint(0, config.history_days * 24)),
                }
            )
            charger_id += 1
    return _with_row_ids("chargers", rows)


def _users(config: GenerationConfig, rng: random.Random, base: datetime) -> list[dict[str, object]]:
    rows = []
    for user_id in range(1, config.user_count + 1):
        rows.append(
            {
                "_row_id": "",
                "id": user_id,
                "mobile": f"138{user_id:08d}",
                "nickname": f"user_{user_id}",
                "avatar_path": None,
                "balance_fen": rng.randint(0, 200000),
                "status": "frozen" if user_id % 83 == 0 else "active",
                "registered_at": iso_at(base, days=-rng.randint(1, 90), seconds=user_id),
            }
        )
    return _with_row_ids("users", rows)


def _orders(
    config: GenerationConfig,
    rng: random.Random,
    base: datetime,
    users: list[dict[str, object]],
    chargers: list[dict[str, object]],
    stations: list[dict[str, object]],
) -> list[dict[str, object]]:
    station_price = {int(row["id"]): int(row["price_fen_per_kwh"]) for row in stations}
    charger_station = {int(row["id"]): int(row["station_id"]) for row in chargers}
    rows = []
    for order_id in range(1, config.order_count + 1):
        charger = rng.choice(chargers)
        charger_id = int(charger["id"])
        station_id = charger_station[charger_id]
        started = base + timedelta(minutes=rng.randint(0, config.history_days * 24 * 60 - 90))
        duration = rng.randint(15, 180)
        energy = round(float(charger["power_kw"]) * duration / 60 * rng.uniform(0.22, 0.72), 3)
        status = "completed" if order_id % 23 else "cancelled"
        amount = int(round(energy * station_price[station_id]))
        rows.append(
            {
                "_row_id": "",
                "id": order_id,
                "user_id": rng.choice(users)["id"],
                "charger_id": charger_id,
                "status": status,
                "reserved_at": started.isoformat(timespec="seconds"),
                "started_at": started.isoformat(timespec="seconds") if status == "completed" else None,
                "ended_at": (started + timedelta(minutes=duration)).isoformat(timespec="seconds") if status == "completed" else None,
                "energy_kwh": energy if status == "completed" else 0,
                "amount_fen": amount if status == "completed" else 0,
            }
        )
    return _with_row_ids("orders", rows)


def _telemetry(config: GenerationConfig, rng: random.Random, base: datetime, chargers: list[dict[str, object]]) -> list[dict[str, object]]:
    """Emit `telemetry_count` frames spread evenly over the whole history window.

    The old version used `recorded_at = base + row_id * 5min`, which pushed the last
    frame `telemetry_count * 5` minutes into the future (about 9.5 years at the full
    scale) and broke `dt` partitioning and every time-windowed metric. Frames are now
    placed on the 5-minute grid inside `[base, base + history_days)`, so at full scale
    roughly 39 of the 288 chargers report on each tick.
    """
    total = max(1, config.telemetry_count)
    window_slots = max(1, config.history_days * 24 * 60 // TELEMETRY_SLOT_MINUTES)
    rows = []
    for row_id in range(1, total + 1):
        charger = rng.choice(chargers)
        power = round(float(charger["power_kw"]) * rng.uniform(0.35, 0.95), 3)
        slot = ((row_id - 1) * window_slots) // total
        rows.append(
            {
                "_row_id": "",
                "id": row_id,
                "charger_id": charger["id"],
                "recorded_at": iso_at(base, minutes=slot * TELEMETRY_SLOT_MINUTES),
                "power_kw": power,
                "energy_increment_kwh": round(power / 12, 4),
                "event_type": "telemetry",
            }
        )
    return _with_row_ids("telemetry", rows)


def _station_hourly(config: GenerationConfig, rng: random.Random, base: datetime, stations: list[dict[str, object]]) -> list[dict[str, object]]:
    rows = []
    for day in range(config.history_days):
        for hour in range(24):
            for station in stations:
                pile_count = config.chargers_per_station
                busy = rng.randint(0, pile_count)
                rated = pile_count * 42
                rows.append(
                    {
                        "_row_id": "",
                        "station_id": station["id"],
                        "observed_at": iso_at(base, days=day, hours=hour),
                        "pile_count": pile_count,
                        "rated_power_kw": rated,
                        "temperature_c": round(18 + rng.uniform(-8, 12), 1),
                        "is_holiday": 1 if (base + timedelta(days=day)).weekday() >= 5 else 0,
                        "busy_count": busy,
                        "load_kw": round(rated * busy / pile_count * rng.uniform(0.45, 0.85), 3),
                    }
                )
    return _with_row_ids("station_hourly", rows)


def _events(config: GenerationConfig, rng: random.Random, base: datetime, orders: list[dict[str, object]], chargers: list[dict[str, object]]) -> list[dict[str, object]]:
    total = max(1, config.event_count)
    window_minutes = max(1, config.history_days * 24 * 60)
    rows = []
    for event_id in range(1, config.event_count + 1):
        created = iso_at(base, minutes=((event_id - 1) * window_minutes) // total)
        if event_id % 7 == 0:
            charger = rng.choice(chargers)
            rows.append(
                {
                    "_row_id": "",
                    "id": event_id,
                    "event_type": "charger_fault",
                    "entity_type": "charger",
                    "entity_id": charger["id"],
                    "message": f"charger {charger['code']} fault reported",
                    "created_at": created,
                }
            )
        else:
            order = rng.choice(orders)
            rows.append(
                {
                    "_row_id": "",
                    "id": event_id,
                    "event_type": "order_completed",
                    "entity_type": "order",
                    "entity_id": order["id"],
                    "message": f"order {order['id']} completed",
                    "created_at": created,
                }
            )
    return _with_row_ids("events", rows)


def _duplicate_payload(rows: list[dict[str, object]], target: dict[str, object], rng: random.Random) -> None:
    source = rows[rng.randrange(len(rows))]
    for key, value in source.items():
        if key not in ("_row_id", "id"):
            target[key] = value


def _messy_timestamp(value: object, index: int) -> object:
    try:
        moment = datetime.fromisoformat(str(value))
    except ValueError:
        return value
    if index % 2 == 0:
        return moment.strftime("%Y/%m/%d %H:%M:%S")
    return str(int(moment.timestamp()))


def _inject_quality_issues(config: GenerationConfig, rows_by_table: dict[str, list[dict[str, object]]]) -> dict[str, object]:
    """Inject the 10 documented issue classes at their documented rates.

    Every mutation is recorded in `issues` with (rule, table, row_id) so #3 can
    reconcile detections one by one. The old version mutated telemetry / chargers /
    station_hourly without logging, which made those three classes impossible to
    reconcile.
    """
    rng = random.Random(config.seed + 991)
    issues: list[dict[str, object]] = []
    used: dict[str, set[int]] = {}

    def pick(table: str, rate: float, candidates: list[int] | None = None, minimum: int = 1, maximum: int | None = None) -> list[dict[str, object]]:
        rows = rows_by_table.get(table, [])
        taken = used.setdefault(table, set())
        total = len(rows)
        if total == 0:
            return []
        pool = [index for index in (range(total) if candidates is None else candidates) if index not in taken]
        count = max(minimum, round(total * rate))
        if maximum is not None:
            count = min(count, maximum)
        count = min(count, len(pool))
        if count <= 0:
            return []
        chosen = rng.sample(pool, count)
        taken.update(chosen)
        return [rows[index] for index in chosen]

    def record(rule: str, table: str, row: dict[str, object]) -> None:
        issues.append(
            {
                "rule": rule,
                "table": table,
                "row_id": row["_row_id"],
                "business_key": row.get("id", row.get("station_id")),
                "description": str(QUALITY_RULES[rule]["description"]),
            }
        )

    def order_indices_with_times() -> list[int]:
        orders = rows_by_table["ods_orders"]
        return [index for index, row in enumerate(orders) if row.get("started_at") and row.get("ended_at")]

    # Q1 missing values
    for row in pick("ods_orders", 0.005, candidates=order_indices_with_times()):
        row["ended_at"] = None
        record("Q1", "ods_orders", row)
    for row in pick("ods_telemetry", 0.005):
        row["power_kw"] = None
        record("Q1", "ods_telemetry", row)

    # Q2 duplicated records
    for row in pick("ods_orders", 0.010):
        _duplicate_payload(rows_by_table["ods_orders"], row, rng)
        record("Q2", "ods_orders", row)
    for row in pick("ods_telemetry", 0.010):
        _duplicate_payload(rows_by_table["ods_telemetry"], row, rng)
        record("Q2", "ods_telemetry", row)

    # Q3 outliers
    for index, row in enumerate(pick("ods_orders", 0.003)):
        row["energy_kwh"] = -1 if index % 2 == 0 else 9999
        record("Q3", "ods_orders", row)
    for row in pick("ods_telemetry", 0.003):
        if row["power_kw"] is not None:
            row["power_kw"] = round(float(row["power_kw"]) * 10, 3)
        record("Q3", "ods_telemetry", row)

    # Q4 mixed timestamp formats
    for index, row in enumerate(pick("ods_orders", 0.010)):
        row["reserved_at"] = _messy_timestamp(row["reserved_at"], index)
        record("Q4", "ods_orders", row)
    for index, row in enumerate(pick("ods_telemetry", 0.010)):
        row["recorded_at"] = _messy_timestamp(row["recorded_at"], index)
        record("Q4", "ods_telemetry", row)

    # Q5 logical contradictions
    for row in pick("ods_orders", 0.003, candidates=order_indices_with_times()):
        row["started_at"], row["ended_at"] = row["ended_at"], row["started_at"]
        record("Q5", "ods_orders", row)
    for row in pick("ods_station_hourly", 0.003):
        row["busy_count"] = int(row["pile_count"]) + 1
        record("Q5", "ods_station_hourly", row)

    # Q6 wrong money unit
    for row in pick("ods_orders", 0.005):
        row["amount_fen"] = int(row["amount_fen"] or 0) // 100
        record("Q6", "ods_orders", row)

    # Q7 orphan reference
    for row in pick("ods_orders", 0.003):
        row["user_id"] = 99999999
        record("Q7", "ods_orders", row)

    # Q8 illegal field values
    for row in pick("ods_users", 0.003):
        row["mobile"] = "13800BAD"
        record("Q8", "ods_users", row)
    for row in pick("ods_chargers", 0.003):
        row["status"] = "unknown"
        record("Q8", "ods_chargers", row)

    # Q9 out-of-range coordinates (1-2 stations)
    for row in pick("ods_stations", 0.010, maximum=2):
        row["latitude"] = 10.0
        record("Q9", "ods_stations", row)

    # Q10 dirty text
    for row in pick("ods_stations", 0.005):
        row["name"] = f"  {row['name']}  "
        record("Q10", "ods_stations", row)
    for row in pick("ods_users", 0.005):
        row["nickname"] = f"ｕｓｅｒ{row['id']}　"
        record("Q10", "ods_users", row)

    issues.sort(key=lambda item: (str(item["rule"]), str(item["table"]), str(item["row_id"])))
    summary: dict[str, object] = {}
    for rule in QUALITY_RULES:
        per_table: dict[str, int] = {}
        for item in issues:
            if item["rule"] == rule:
                key = str(item["table"])
                per_table[key] = per_table.get(key, 0) + 1
        summary[rule] = {
            "description": QUALITY_RULES[rule]["description"],
            "rate": QUALITY_RULES[rule]["rate"],
            "tables": per_table,
            "total": sum(per_table.values()),
        }

    generated_at = iso_at(datetime.fromisoformat(config.start_date).replace(tzinfo=CN_TZ))
    return {
        "contractVersion": "ods-dwd-v0.1",
        "runId": f"scml-{config.seed}",
        "run_id": f"scml-{config.seed}",
        "seed": config.seed,
        "generatedAt": generated_at,
        "generated_at": generated_at,
        "totalInjected": len(issues),
        "summary": summary,
        "issues": issues,
    }


def _load_simple_yaml(path: Path) -> dict[str, object]:
    values: dict[str, object] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line or ":" not in line:
            continue
        key, value = [part.strip() for part in line.split(":", 1)]
        if value.startswith('"') and value.endswith('"'):
            values[key] = value[1:-1]
        else:
            try:
                values[key] = int(value)
            except ValueError:
                values[key] = value
    return values


def load_config(path: Path) -> GenerationConfig:
    values = _load_simple_yaml(path)
    return GenerationConfig(**values)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate SCML ODS handoff data.")
    parser.add_argument("--config", type=Path, default=Path("config/part2_scml_sample.yaml"))
    parser.add_argument("--out", type=Path, default=Path("handoff/ods"))
    args = parser.parse_args()
    manifest = generate_handoff(load_config(args.config), args.out)
    print(json.dumps({"ok": True, "manifest": manifest}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
