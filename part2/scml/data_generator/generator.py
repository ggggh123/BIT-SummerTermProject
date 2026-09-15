from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import math
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

# =============================================================================
# 需求分布模型（《04-SCML》§2.1「真实分布：订单在日内呈早晚双峰、周末/节假日差异；
# 新站利用率爬坡；温度按季节模拟」）
# =============================================================================
# 早期版本的 `_orders` 在整窗口内均匀抽开工时间、`_station_hourly` 用
# `busy = randint(0, pile_count)` 均匀抽占用，结果是：24 小时负荷曲线是平的、
# 站点之间没有差异、`peak_hour` 在 0–23 之间均匀散落。这有两个后果：
#   1. 大屏的「24h 负荷曲线 / 繁忙热力图」看起来就是噪声；
#   2. `load_kw` 是 #5 的**预测目标**，没有日内规律就没有可学习的模式，
#      Spark MLlib 无论怎么调都是拟合噪声。
# 所以这里显式建模三层因子：日内形状 × 星期修正 × 站点规模与爬坡。

# 日内需求形状（24 点相对权重）：08 点早高峰与 18 点晚高峰双峰，04 点最深谷。
HOURLY_DEMAND_SHAPE: tuple[float, ...] = (
    0.26, 0.18, 0.14, 0.12, 0.13, 0.20,   # 00-05 夜间谷
    0.42, 0.72, 0.95, 0.86, 0.66, 0.58,   # 06-11 早高峰（08 点峰）
    0.62, 0.56, 0.54, 0.58, 0.70, 0.84,   # 12-17 午后逐步回升
    0.98, 0.90, 0.72, 0.56, 0.42, 0.32,   # 18-23 晚高峰（18 点峰）
)

# 星期修正（索引 = `datetime.weekday()`，0=周一 … 6=周日）：周末整体需求更低，
# 且**形状**要再压平一次（通勤早高峰消失），见 WEEKEND_HOUR_FACTOR。
WEEKDAY_FACTOR: tuple[float, ...] = (1.00, 0.97, 0.98, 1.01, 1.08, 0.88, 0.84)

# 周末的逐小时附加修正：抹掉早高峰、抬高白天与深夜。
WEEKEND_HOUR_FACTOR: tuple[float, ...] = (
    1.15, 1.20, 1.20, 1.20, 1.12, 1.00,
    0.88, 0.62, 0.58, 0.80, 0.98, 1.08,
    1.15, 1.15, 1.12, 1.08, 1.02, 0.98,
    1.00, 1.02, 1.08, 1.12, 1.15, 1.15,
)

# 站点规模差异：把站点按「投运越晚越小」排布，同时用于爬坡。
STATION_SCALE_MIN = 0.62
STATION_SCALE_MAX = 1.08

# 充电效率（负荷 / 额定）：快慢桩混合后的等效系数区间。
EFFICIENCY_MIN = 0.78
EFFICIENCY_MAX = 0.94

# 温度模型（北京 6 月中旬 → 9 月中旬）：季节项 + 日内项，日内 05 点最低、17 点最高。
TEMPERATURE_SEASONAL_BASE = 26.0
TEMPERATURE_SEASONAL_AMPLITUDE = 4.0
TEMPERATURE_DIURNAL_AMPLITUDE = 5.0


def _hour_weight(day_index: int, hour: int, base_weekday: int) -> float:
    """第 `day_index` 天 `hour` 点的相对需求权重（三层因子相乘）。"""
    weekday = (base_weekday + day_index) % 7
    weight = HOURLY_DEMAND_SHAPE[hour] * WEEKDAY_FACTOR[weekday]
    if weekday >= 5:
        weight *= WEEKEND_HOUR_FACTOR[hour]
    return weight


def _ramp_factor(day_index: int, station_age_days: float, history_days: int) -> float:
    """新站利用率爬坡：投运越晚的站点，在窗口早期利用率越低，60 天后趋于饱和。"""
    age = station_age_days + day_index
    maturity = min(1.0, max(0.0, age / 60.0))
    return 0.55 + 0.45 * maturity


def _temperature(day_index: int, hour: int, history_days: int, jitter: float) -> float:
    """季节曲线（夏初升到盛夏再回落）+ 日内曲线（05 点最低、17 点最高）+ 噪声。"""
    span = max(1, history_days - 1)
    seasonal = TEMPERATURE_SEASONAL_BASE + TEMPERATURE_SEASONAL_AMPLITUDE * math.sin(
        math.pi * day_index / span
    )
    diurnal = -TEMPERATURE_DIURNAL_AMPLITUDE * math.cos(2 * math.pi * (hour - 5) / 24.0)
    return round(seasonal + diurnal + jitter, 1)


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


def _station_age_days(station: dict[str, object], base: datetime) -> float:
    """站点在窗口起点（`base`）已投运的天数，用于爬坡因子。"""
    try:
        created = datetime.fromisoformat(str(station["created_at"])).astimezone(CN_TZ)
    except (ValueError, KeyError):
        return 90.0
    return max(0.0, (base - created).total_seconds() / 86400.0)


def _stations(config: GenerationConfig, rng: random.Random, base: datetime) -> list[dict[str, object]]:
    districts = ["Chaoyang", "Haidian", "Fengtai", "Tongzhou", "Daxing"]
    rows = []
    for station_id in range(1, config.station_count + 1):
        district = districts[(station_id - 1) % len(districts)]
        # 投运时间刻意拉开：1 号站约 108 天前投运（窗口起点已成熟），
        # 末号站只有 24 天（窗口内还在爬坡），支撑《04-SCML》§2.1 的「新站利用率爬坡」。
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
                "created_at": iso_at(base, days=-(120 - station_id * 12)),
            }
        )
    return _with_row_ids("stations", rows)


def _charger_snapshot_status(
    station_id: int,
    charger_index: int,
    chargers_per_station: int,
    station_count: int,
    charger_id: int,
) -> str:
    if charger_id % 97 == 0:
        return "fault"

    rank = (station_id - 1) / max(1, station_count - 1)
    active_ratio = 0.46 - rank * 0.30
    active_slots = max(1, round(chargers_per_station * active_ratio))
    reserved_slots = max(1, round(active_slots * 0.32)) if active_slots >= 3 else 0
    charging_slots = max(0, active_slots - reserved_slots)

    if charger_index < charging_slots:
        return "charging"
    if charger_index < active_slots:
        return "reserved"
    return "idle"


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
                    "status": _charger_snapshot_status(
                        int(station["id"]),
                        index,
                        config.chargers_per_station,
                        config.station_count,
                        charger_id,
                    ),
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

    # 开工时刻按「日内双峰 × 星期修正」加权抽样，而不是在整窗口上均匀撒点。
    # 累积权重表只建一次（history_days × 24 个桶），之后每个订单二分查找一次。
    cumulative: list[float] = []
    running = 0.0
    for day in range(config.history_days):
        for hour in range(24):
            running += _hour_weight(day, hour, base.weekday())
            cumulative.append(running)
    total_weight = cumulative[-1]

    rows = []
    for order_id in range(1, config.order_count + 1):
        charger = rng.choice(chargers)
        charger_id = int(charger["id"])
        station_id = charger_station[charger_id]
        slot = bisect.bisect_right(cumulative, rng.random() * total_weight)
        day, hour = divmod(min(slot, len(cumulative) - 1), 24)
        started = base + timedelta(days=day, hours=hour, minutes=rng.randint(0, 59))
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
    """站点 × 小时占用与负荷（ML 训练输入，也是大屏热力图的数据源）。

    占用率 = 日内形状 × 星期修正 × 站点规模 × 新站爬坡 × 噪声，再夹到 [0, 0.97]；
    `load_kw` = 额定总功率 × 占用率 × 充电效率。这样曲线有真实的早晚双峰，
    站点之间有规模差异，晚投运的站点在窗口内呈现爬坡 —— #5 才有可学的模式。
    """
    rows = []
    weekday0 = base.weekday()
    station_count = max(1, len(stations))

    for day in range(config.history_days):
        for hour in range(24):
            shape = _hour_weight(day, hour, weekday0)
            for index, station in enumerate(stations):
                pile_count = config.chargers_per_station
                rated = pile_count * 42
                # 站点规模与投运时间**反序**：1 号站最早投运、规模最大；末号站最晚投运、
                # 规模最小且仍在爬坡。两条因子的方向一致，曲线才讲得通 —— 否则
                # 「规模随投运变晚而变小」会被爬坡因子抵消掉，站间差异看不出趋势。
                rank = index / max(1, station_count - 1) if station_count > 1 else 1.0
                scale = STATION_SCALE_MIN + (STATION_SCALE_MAX - STATION_SCALE_MIN) * (1.0 - rank)
                ramp = _ramp_factor(day, _station_age_days(station, base), config.history_days)
                target = shape * scale * ramp * rng.uniform(0.88, 1.12)
                utilization = max(0.0, min(0.97, target))
                busy = max(0, min(pile_count, int(round(pile_count * utilization))))
                efficiency = rng.uniform(EFFICIENCY_MIN, EFFICIENCY_MAX)
                rows.append(
                    {
                        "_row_id": "",
                        "station_id": station["id"],
                        "observed_at": iso_at(base, days=day, hours=hour),
                        "pile_count": pile_count,
                        "rated_power_kw": rated,
                        "temperature_c": _temperature(day, hour, config.history_days, rng.uniform(-1.2, 1.2)),
                        "is_holiday": 1 if (base + timedelta(days=day)).weekday() >= 5 else 0,
                        "busy_count": busy,
                        "load_kw": round(rated * busy / pile_count * efficiency, 3),
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
    """把随机一行的**业务负载连同业务主键**复制到 target，形成一条“重复记录”。

    口径（2026-09-15 由 #2 拍板，见 part2/contracts/Q2-duplicate-policy-decision.md）：
    **Q2「重复记录」= 业务主键重复**，而不是“业务内容相同但主键不同”。

    因此这里必须复制契约 primary_key 覆盖的字段（orders 为 ["id"]；telemetry 为
    ["charger_id", "recorded_at"]）——旧实现跳过 `id` 不复制，导致注入行与源行业务主键不同，
    检测端按 primary_key 分组时永远命中不到（即对账里的 FN 漏检）。
    `_row_id` 仍保持唯一：它是行级追踪键，用于 injection_log 与检测结果逐条对账，不属于业务主键。
    """
    source = rows[rng.randrange(len(rows))]
    for key, value in source.items():
        if key != "_row_id":
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
