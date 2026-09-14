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
        writer.writerows(rows)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return {"rows": len(rows), "sha256": digest, "path": path.name}


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
    injection_log = _inject_quality_issues(config, orders, telemetry, users, stations, chargers, station_hourly)

    tables = {
        "ods_stations": (stations, list(stations[0].keys())),
        "ods_chargers": (chargers, list(chargers[0].keys())),
        "ods_users": (users, list(users[0].keys())),
        "ods_orders": (orders, list(orders[0].keys())),
        "ods_telemetry": (telemetry, list(telemetry[0].keys())),
        "ods_station_hourly": (station_hourly, list(station_hourly[0].keys())),
        "ods_events": (events, list(events[0].keys())),
    }

    table_manifest: dict[str, object] = {}
    for table, (rows, fields) in tables.items():
        table_dir = handoff_dir / table
        table_manifest[table] = write_csv(table_dir / "part-00000.csv", fields, rows)
        (table_dir / "_SUCCESS").write_text("", encoding="utf-8")

    generation_time = iso_at(base, hours=config.history_days * 24)
    manifest = {
        "runId": f"scml-{config.seed}",
        "seed": config.seed,
        "generatedAt": generation_time,
        "format": "csv",
        "tables": table_manifest,
    }
    (handoff_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    (handoff_dir / "injection_log.json").write_text(
        json.dumps(injection_log, ensure_ascii=False, indent=2) + "\n",
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
    return rows


def _chargers(config: GenerationConfig, rng: random.Random, base: datetime, stations: list[dict[str, object]]) -> list[dict[str, object]]:
    rows = []
    charger_id = 1
    for station in stations:
        for index in range(config.chargers_per_station):
            fast = index < round(config.chargers_per_station * 0.4)
            power = rng.choice([60, 120]) if fast else rng.choice([7, 30])
            rows.append(
                {
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
    return rows


def _users(config: GenerationConfig, rng: random.Random, base: datetime) -> list[dict[str, object]]:
    rows = []
    for user_id in range(1, config.user_count + 1):
        rows.append(
            {
                "id": user_id,
                "mobile": f"138{user_id:08d}",
                "nickname": f"user_{user_id}",
                "avatar_path": "",
                "balance_fen": rng.randint(0, 200000),
                "status": "frozen" if user_id % 83 == 0 else "active",
                "registered_at": iso_at(base, days=-rng.randint(1, 90), seconds=user_id),
            }
        )
    return rows


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
                "id": order_id,
                "user_id": rng.choice(users)["id"],
                "charger_id": charger_id,
                "station_id": station_id,
                "status": status,
                "reserved_at": started.isoformat(timespec="seconds"),
                "started_at": started.isoformat(timespec="seconds") if status == "completed" else "",
                "ended_at": (started + timedelta(minutes=duration)).isoformat(timespec="seconds") if status == "completed" else "",
                "energy_kwh": energy if status == "completed" else 0,
                "amount_fen": amount if status == "completed" else 0,
            }
        )
    return rows


def _telemetry(config: GenerationConfig, rng: random.Random, base: datetime, chargers: list[dict[str, object]]) -> list[dict[str, object]]:
    rows = []
    for row_id in range(1, config.telemetry_count + 1):
        charger = rng.choice(chargers)
        power = round(float(charger["power_kw"]) * rng.uniform(0.35, 0.95), 3)
        rows.append(
            {
                "id": row_id,
                "charger_id": charger["id"],
                "recorded_at": iso_at(base, minutes=row_id * 5),
                "power_kw": power,
                "energy_increment_kwh": round(power / 12, 4),
                "event_type": "telemetry",
            }
        )
    return rows


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
    return rows


def _events(config: GenerationConfig, rng: random.Random, base: datetime, orders: list[dict[str, object]], chargers: list[dict[str, object]]) -> list[dict[str, object]]:
    rows = []
    for event_id in range(1, config.event_count + 1):
        if event_id % 7 == 0:
            charger = rng.choice(chargers)
            rows.append(
                {
                    "id": event_id,
                    "event_type": "charger_fault",
                    "entity_type": "charger",
                    "entity_id": charger["id"],
                    "message": f"charger {charger['code']} fault reported",
                    "created_at": iso_at(base, minutes=event_id * 3),
                }
            )
        else:
            order = rng.choice(orders)
            rows.append(
                {
                    "id": event_id,
                    "event_type": "order_completed",
                    "entity_type": "order",
                    "entity_id": order["id"],
                    "message": f"order {order['id']} completed",
                    "created_at": iso_at(base, minutes=event_id * 3),
                }
            )
    return rows


def _inject_quality_issues(
    config: GenerationConfig,
    orders: list[dict[str, object]],
    telemetry: list[dict[str, object]],
    users: list[dict[str, object]],
    stations: list[dict[str, object]],
    chargers: list[dict[str, object]],
    station_hourly: list[dict[str, object]],
) -> dict[str, object]:
    issues = []

    def add(issue_id: str, table: str, key: object, description: str) -> None:
        issues.append({"id": issue_id, "table": table, "keys": [key], "count": 1, "description": description})

    if orders:
        orders[0]["ended_at"] = ""
        add("Q1", "ods_orders", orders[0]["id"], "missing completed ended_at")
    if len(orders) > 1:
        orders[1] = dict(orders[0], id=orders[1]["id"])
        add("Q2", "ods_orders", orders[1]["id"], "duplicated order payload with different id")
    if len(orders) > 2:
        orders[2]["energy_kwh"] = -1
        add("Q3", "ods_orders", orders[2]["id"], "negative energy_kwh")
    if len(orders) > 3:
        orders[3]["reserved_at"] = "2026/09/01 08:00:00"
        add("Q4", "ods_orders", orders[3]["id"], "mixed timestamp format")
    if len(orders) > 4:
        orders[4]["started_at"], orders[4]["ended_at"] = orders[4]["ended_at"], orders[4]["started_at"]
        add("Q5", "ods_orders", orders[4]["id"], "ended_at earlier than started_at")
    if len(orders) > 5:
        orders[5]["amount_fen"] = int(orders[5]["amount_fen"]) // 100
        add("Q6", "ods_orders", orders[5]["id"], "amount stored as yuan instead of fen")
    if len(orders) > 6:
        orders[6]["user_id"] = 99999999
        add("Q7", "ods_orders", orders[6]["id"], "orphan user reference")
    if users:
        users[0]["mobile"] = "13800BAD"
        add("Q8", "ods_users", users[0]["id"], "invalid mobile")
    if stations:
        stations[0]["latitude"] = 10.0
        add("Q9", "ods_stations", stations[0]["id"], "latitude outside Beijing")
    if len(stations) > 1:
        stations[1]["name"] = f"  {stations[1]['name']}  "
        add("Q10", "ods_stations", stations[1]["id"], "dirty surrounding whitespace")

    if telemetry:
        telemetry[0]["power_kw"] = ""
    if chargers:
        chargers[0]["status"] = "unknown"
    if station_hourly:
        station_hourly[0]["busy_count"] = int(station_hourly[0]["pile_count"]) + 1

    return {"runId": f"scml-{config.seed}", "seed": config.seed, "issues": issues}


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
