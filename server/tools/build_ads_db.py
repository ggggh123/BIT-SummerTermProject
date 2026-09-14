"""ADS 物化作业：ODS 交接包 -> `handoff/ads/ads.db`（SQLite 单文件）。

对应《02-TL-Hadoop平台与Flask后端设计》§3.2：ADS 指标在 HDFS 以 Parquet 物化后
**导出为 SQLite 单文件**，Flask 用标准库 `sqlite3` 只读打开、进程内不起 Spark。

本作业是「导出」这一步的可离线复现实现（纯标准库、零依赖）：没有 Hadoop/Spark 的
机器也能产出与集成机同构的 `ads.db`，供前端联调与答辩查数。正式链路上 DWS/ADS 由
SparkSQL 产出 Parquet，导出同一张表结构即可，Flask 侧无需改动。

用法：
    python server/tools/build_ads_db.py --ods handoff/ods --out handoff/ads
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sqlite3
import sys
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from services.ads_cleaning import (  # noqa: E402
    CARBON_FACTOR_NOTE,
    CARBON_FACTOR_TON_PER_MWH,
    CHARGER_STATES,
    DISTRICT_POPULATION,
    KG_CO2_PER_TREE_YEAR,
    NULL_TEXT,
    MAX_SESSION_KWH,
    impute_coordinate,
    is_valid_mobile,
    is_valid_coordinate,
    normalize_text,
    parse_timestamp,
    resolve_amount_fen,
    station_display_name,
    to_district_cn,
)

CN_TZ = timezone(timedelta(hours=8))

# 规则 ID 与问题类型（对齐 03-PRL §2.3 与前端 quality_summary 契约）
RULE_TYPES = [
    ("R01", "缺失值"),
    ("R02", "重复记录"),
    ("R03", "异常值"),
    ("R04", "时间格式混杂"),
    ("R05", "逻辑矛盾"),
    ("R06", "金额口径错误"),
    ("R07", "孤儿引用"),
    ("R08", "非法字段值"),
    ("R09", "经纬度越界"),
    ("R10", "文本脏数据"),
]
# 生成器注入日志用的是 Q1..Q10，与 PRL 的 R01..R10 一一对应
INJECTION_TO_RULE = {f"Q{i}": f"R{i:02d}" for i in range(1, 11)}

SCHEMA = """
CREATE TABLE ads_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);

CREATE TABLE ads_station (
    station_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    name_raw TEXT NOT NULL,
    district TEXT NOT NULL,
    district_raw TEXT NOT NULL,
    address TEXT,
    longitude REAL NOT NULL,
    latitude REAL NOT NULL,
    price_fen_per_kwh INTEGER NOT NULL,
    forecast_enabled INTEGER NOT NULL,
    charger_cnt INTEGER NOT NULL,
    fast_cnt INTEGER NOT NULL,
    slow_cnt INTEGER NOT NULL,
    fast_power_kw INTEGER NOT NULL,
    slow_power_kw INTEGER NOT NULL,
    idle_cnt INTEGER NOT NULL,
    reserved_cnt INTEGER NOT NULL,
    charging_cnt INTEGER NOT NULL,
    fault_cnt INTEGER NOT NULL,
    restarting_cnt INTEGER NOT NULL,
    service_radius_km REAL NOT NULL,
    coord_imputed INTEGER NOT NULL
);

CREATE TABLE ads_charger (
    charger_id INTEGER PRIMARY KEY,
    station_id INTEGER NOT NULL,
    code TEXT NOT NULL,
    type TEXT NOT NULL,
    power_kw INTEGER NOT NULL,
    status TEXT NOT NULL,
    charge_count INTEGER NOT NULL,
    total_duration_sec INTEGER NOT NULL,
    fault_flag INTEGER NOT NULL
);
CREATE INDEX idx_ads_charger_station ON ads_charger(station_id);

CREATE TABLE ads_daily (
    dt TEXT PRIMARY KEY,
    revenue_fen INTEGER NOT NULL,
    energy_kwh REAL NOT NULL,
    order_cnt INTEGER NOT NULL,
    new_user_cnt INTEGER NOT NULL,
    active_user_cnt INTEGER NOT NULL
);

CREATE TABLE ads_station_day (
    dt TEXT NOT NULL,
    station_id INTEGER NOT NULL,
    revenue_fen INTEGER NOT NULL,
    energy_kwh REAL NOT NULL,
    order_cnt INTEGER NOT NULL,
    fast_order_cnt INTEGER NOT NULL,
    slow_order_cnt INTEGER NOT NULL,
    avg_utilization REAL NOT NULL,
    peak_hour INTEGER NOT NULL,
    PRIMARY KEY (dt, station_id)
);

CREATE TABLE ads_station_hourly (
    dt TEXT NOT NULL,
    station_id INTEGER NOT NULL,
    hour INTEGER NOT NULL,
    observed_at TEXT NOT NULL,
    pile_count INTEGER NOT NULL,
    busy_count INTEGER NOT NULL,
    load_kw REAL NOT NULL,
    utilization REAL NOT NULL,
    PRIMARY KEY (dt, station_id, hour)
);

CREATE TABLE ads_user_rfm (
    segment TEXT PRIMARY KEY,
    user_cnt INTEGER NOT NULL,
    avg_recency_days REAL NOT NULL,
    avg_frequency REAL NOT NULL,
    avg_monetary_fen REAL NOT NULL,
    revenue_fen INTEGER NOT NULL
);

CREATE TABLE ads_district (
    district TEXT PRIMARY KEY,
    station_cnt INTEGER NOT NULL,
    charger_cnt INTEGER NOT NULL,
    population INTEGER NOT NULL,
    order_cnt INTEGER NOT NULL,
    served_user_cnt INTEGER NOT NULL,
    avg_wait_min REAL NOT NULL,
    utilization_rate REAL NOT NULL,
    energy_kwh REAL NOT NULL,
    revenue_fen INTEGER NOT NULL,
    co2_saved_kg REAL NOT NULL
);

CREATE TABLE ads_quality_table (
    name TEXT PRIMARY KEY,
    rows_before INTEGER NOT NULL,
    rows_after INTEGER NOT NULL
);

CREATE TABLE ads_quality_issue (
    rule TEXT PRIMARY KEY,
    type TEXT NOT NULL,
    injected INTEGER NOT NULL,
    detected INTEGER NOT NULL,
    handled INTEGER NOT NULL,
    recall REAL NOT NULL
);

CREATE TABLE ads_quality_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);

CREATE TABLE ads_forecast_batch (
    run_id TEXT PRIMARY KEY,
    model_version TEXT NOT NULL,
    activated_at TEXT NOT NULL,
    source TEXT NOT NULL,
    horizon_h_max INTEGER NOT NULL,
    is_baseline INTEGER NOT NULL,
    note TEXT
);

CREATE TABLE ads_forecast_24h (
    run_id TEXT NOT NULL,
    station_id INTEGER NOT NULL,
    forecast_at TEXT NOT NULL,
    horizon_h INTEGER NOT NULL,
    predicted_load_kw REAL NOT NULL,
    predicted_busy_count INTEGER NOT NULL,
    predicted_idle_count INTEGER NOT NULL,
    congestion_level TEXT NOT NULL,
    is_peak INTEGER NOT NULL,
    PRIMARY KEY (run_id, station_id, horizon_h)
);

CREATE TABLE ads_forecast_metric (
    horizon_h INTEGER PRIMARY KEY,
    mae REAL NOT NULL,
    rmse REAL NOT NULL,
    wape REAL NOT NULL,
    baseline_wape REAL NOT NULL
);

CREATE TABLE ads_event (
    event_id INTEGER PRIMARY KEY,
    event_type TEXT NOT NULL,
    message TEXT NOT NULL,
    message_raw TEXT,
    created_at TEXT NOT NULL
);
CREATE INDEX idx_ads_event_created ON ads_event(created_at DESC);
"""

# 幂等重建用的表清单（顺序无所谓，SQLite 无外键约束）
TABLE_NAMES = [
    "ads_meta", "ads_station", "ads_charger", "ads_daily", "ads_station_day",
    "ads_station_hourly", "ads_user_rfm", "ads_district", "ads_quality_table",
    "ads_quality_issue", "ads_quality_meta", "ads_forecast_batch",
    "ads_forecast_24h", "ads_forecast_metric", "ads_event",
]
RESET_SQL = "\n".join(f"DROP TABLE IF EXISTS {name};" for name in TABLE_NAMES)


# --------------------------------------------------------------------------- #
# ODS 读取
# --------------------------------------------------------------------------- #
def iter_rows(table_dir: Path):
    """读取一张表：优先 Hive 风格 `dt=*` 分区目录，其次扁平 `part-*` 文件。"""
    if not table_dir.is_dir():
        return
    parts = sorted(p for p in table_dir.glob("dt=*/part-*") if p.is_file())
    if not parts:
        parts = sorted(p for p in table_dir.glob("part-*") if p.is_file())
    for part in parts:
        if part.suffix == ".jsonl":
            with part.open(encoding="utf-8") as handle:
                for line in handle:
                    line = line.strip()
                    if line:
                        yield json.loads(line)
        else:
            with part.open(encoding="utf-8", newline="") as handle:
                for row in csv.DictReader(handle):
                    yield row


def _int(value, default=0) -> int:
    try:
        return int(float(str(value).strip()))
    except (TypeError, ValueError):
        return default


def _float(value, default=0.0) -> float:
    try:
        return float(str(value).strip())
    except (TypeError, ValueError):
        return default


def _needs_text_repair(raw: object, normalized: str) -> bool:
    text = "" if raw is None else str(raw)
    return text != normalized and text != NULL_TEXT and normalized != ""


# --------------------------------------------------------------------------- #
# 各层构建
# --------------------------------------------------------------------------- #
def load_stations(ods_dir: Path, detected: Counter) -> dict[int, dict]:
    stations: dict[int, dict] = {}
    for row in iter_rows(ods_dir / "ods_stations"):
        station_id = _int(row.get("id"))
        if not station_id:
            continue
        district_raw = normalize_text(row.get("district"))
        district_cn = to_district_cn(district_raw)
        name_raw = normalize_text(row.get("name"))
        latitude = _float(row.get("latitude"))
        longitude = _float(row.get("longitude"))
        imputed = 0
        if not is_valid_coordinate(latitude, longitude):
            detected["R09"] += 1
            latitude, longitude = impute_coordinate(district_cn, station_id)
            imputed = 1
        if _needs_text_repair(row.get("name"), name_raw):
            detected["R10"] += 1
        stations[station_id] = {
            "station_id": station_id,
            "name": station_display_name(row.get("name"), district_cn, station_id),
            "name_raw": "" if row.get("name") in (None, NULL_TEXT) else str(row.get("name")),
            "district": district_cn,
            "district_raw": district_raw,
            "address": normalize_text(row.get("address")),
            "longitude": round(longitude, 6),
            "latitude": round(latitude, 6),
            "price_fen_per_kwh": _int(row.get("price_fen_per_kwh"), 100),
            "forecast_enabled": _int(row.get("forecast_enabled")),
            "coord_imputed": imputed,
        }
    return stations


def load_chargers(ods_dir: Path, detected: Counter) -> tuple[dict[int, dict], dict[int, int]]:
    """返回 (清洗后的桩维度, 原始桩 -> 所属站 的引用映射)。

    引用映射用**原始**全量桩，避免「某桩状态字段非法被剔除」把它的订单连带判成孤儿
    （桩物理存在，只是那一次状态读数不可信）。
    """
    raw_reference: dict[int, int] = {}
    cleaned: dict[int, dict] = {}
    for row in iter_rows(ods_dir / "ods_chargers"):
        charger_id = _int(row.get("id"))
        if not charger_id:
            continue
        station_id = _int(row.get("station_id"))
        raw_reference[charger_id] = station_id
        status = normalize_text(row.get("status")).lower()
        if status not in CHARGER_STATES:  # R08 非法枚举 -> 剔除
            detected["R08_chargers"] += 1
            continue
        cleaned[charger_id] = {
            "charger_id": charger_id,
            "station_id": station_id,
            "code": normalize_text(row.get("code")),
            "type": normalize_text(row.get("type")).lower() or "slow",
            "power_kw": _int(row.get("power_kw")),
            "status": status,
            "charge_count": _int(row.get("charge_count")),
            "total_duration_sec": _int(row.get("total_duration_sec")),
            "fault_flag": 1 if status == "fault" else 0,
        }
    return cleaned, raw_reference


def load_users(ods_dir: Path, detected: Counter) -> dict[int, dict]:
    users: dict[int, dict] = {}
    for row in iter_rows(ods_dir / "ods_users"):
        user_id = _int(row.get("id"))
        if not user_id:
            continue
        if not is_valid_mobile(row.get("mobile")):  # R08
            detected["R08_users"] += 1
            continue
        nickname = normalize_text(row.get("nickname"))
        if _needs_text_repair(row.get("nickname"), nickname):
            detected["R10_users"] += 1
        users[user_id] = {
            "user_id": user_id,
            "mobile": normalize_text(row.get("mobile")),
            "nickname": nickname,
            "registered_at": parse_timestamp(row.get("registered_at")),
        }
    return users


def load_orders(
    ods_dir: Path,
    users: dict[int, dict],
    stations: dict[int, dict],
    charger_reference: dict[int, int],
    chargers: dict[int, dict],
    detected: Counter,
) -> list[dict]:
    """清洗订单并返回完成订单明细（营收/电量/订单量的唯一事实源）。"""
    # R02 去重：按业务主键 id 保留 reserved_at 最早的一条
    best: dict[int, tuple[datetime, dict]] = {}
    for row in iter_rows(ods_dir / "ods_orders"):
        order_id = _int(row.get("id"))
        if not order_id:
            continue
        reserved = parse_timestamp(row.get("reserved_at"))
        key = reserved or datetime.max.replace(tzinfo=CN_TZ)
        current = best.get(order_id)
        if current is None:
            best[order_id] = (key, row)
        else:
            detected["R02"] += 1
            if key < current[0]:
                best[order_id] = (key, row)

    orders: list[dict] = []
    for order_id, (_, row) in best.items():
        reserved_at = parse_timestamp(row.get("reserved_at"))
        started_at = parse_timestamp(row.get("started_at"))
        ended_at = parse_timestamp(row.get("ended_at"))

        if normalize_text(row.get("reserved_at")) and reserved_at is None:
            detected["R04"] += 1
        if normalize_text(row.get("started_at")) and started_at is None:
            detected["R04"] += 1
        if normalize_text(row.get("ended_at")) and ended_at is None:
            detected["R04"] += 1

        # 业务口径：只有 completed 订单计入营收、电量、订单量
        if normalize_text(row.get("status")).lower() != "completed":
            continue

        # R01 关键字段缺失 / R04 时间不可解析 -> 剔除
        if started_at is None or ended_at is None:
            detected["R01"] += 1
            continue
        # R05 逻辑矛盾 -> 剔除
        if ended_at < started_at:
            detected["R05"] += 1
            continue
        # R03 异常电量 -> 剔除
        energy_kwh = _float(row.get("energy_kwh"), -1.0)
        if energy_kwh < 0 or energy_kwh > MAX_SESSION_KWH:
            detected["R03"] += 1
            continue
        # R07 孤儿引用 -> 剔除
        user_id = _int(row.get("user_id"))
        charger_id = _int(row.get("charger_id"))
        if user_id not in users or charger_id not in charger_reference:
            detected["R07"] += 1
            continue

        station_id = charger_reference[charger_id]
        station = stations.get(station_id)
        if station is None:
            detected["R07"] += 1
            continue

        # R06 金额口径 -> 修正或剔除
        amount_fen, repaired = resolve_amount_fen(
            row.get("amount_fen"), energy_kwh, int(station["price_fen_per_kwh"])
        )
        if amount_fen is None:
            detected["R06_bad"] += 1
            continue
        if repaired:
            detected["R06"] += 1

        wait_min = 0.0
        if reserved_at is not None and started_at > reserved_at:
            wait_min = (started_at - reserved_at).total_seconds() / 60.0

        orders.append(
            {
                "order_id": order_id,
                "user_id": user_id,
                "charger_id": charger_id,
                "station_id": station_id,
                "charger_type": chargers.get(charger_id, {}).get("type", "slow"),
                "started_at": started_at,
                "ended_at": ended_at,
                "energy_kwh": energy_kwh,
                "amount_fen": amount_fen,
                "wait_min": wait_min,
            }
        )
    return orders


def load_station_hourly(ods_dir: Path, stations: dict[int, dict], detected: Counter) -> list[dict]:
    rows: list[dict] = []
    for row in iter_rows(ods_dir / "ods_station_hourly"):
        observed_at = parse_timestamp(row.get("observed_at"))
        if observed_at is None:
            detected["R04"] += 1
            continue
        station_id = _int(row.get("station_id"))
        if station_id not in stations:
            detected["R07"] += 1
            continue
        pile_count = max(1, _int(row.get("pile_count"), 1))
        busy_count = _int(row.get("busy_count"))
        if busy_count > pile_count:  # R05 -> 裁剪回 pile_count
            detected["R05"] += 1
            busy_count = pile_count
        busy_count = max(0, busy_count)
        load_kw = max(0.0, _float(row.get("load_kw")))
        rows.append(
            {
                "dt": observed_at.date().isoformat(),
                "station_id": station_id,
                "hour": observed_at.hour,
                "observed_at": observed_at.isoformat(timespec="seconds"),
                "pile_count": pile_count,
                "busy_count": busy_count,
                "load_kw": round(load_kw, 3),
                "utilization": round(busy_count / pile_count * 100.0, 1),
            }
        )
    return rows


def scan_telemetry(ods_dir: Path, rated_power: dict[int, int], detected: Counter) -> tuple[int, int]:
    """流式扫描遥测表：只做质量对账计数，ADS 不直接消费桩级明细。

    返回 (总行数, 剔除行数)。
    """
    total = 0
    removed = 0
    seen: set[int] = set()
    for row in iter_rows(ods_dir / "ods_telemetry"):
        total += 1
        row_id = _int(row.get("id"))
        bad = False
        if row_id in seen:
            detected["R02"] += 1
            bad = True
        else:
            seen.add(row_id)
        power_raw = row.get("power_kw")
        if power_raw is None or str(power_raw).strip() in ("", NULL_TEXT):
            detected["R01"] += 1
            bad = True
        else:
            power = _float(power_raw, -1.0)
            rated = rated_power.get(_int(row.get("charger_id")), 120)
            if power < 0 or power > rated * 1.05:
                detected["R03"] += 1
                bad = True
        if parse_timestamp(row.get("recorded_at")) is None:
            detected["R04"] += 1
            bad = True
        if bad:
            removed += 1
    return total, removed


def load_events(ods_dir: Path, orders: dict[int, dict], stations: dict[int, dict],
                chargers: dict[int, dict], detected: Counter) -> list[dict]:
    events: list[dict] = []
    for row in iter_rows(ods_dir / "ods_events"):
        created_at = parse_timestamp(row.get("created_at"))
        if created_at is None:
            detected["R04"] += 1
            continue
        raw_type = normalize_text(row.get("event_type")) or "unknown"
        message_raw = normalize_text(row.get("message"))
        entity_id = _int(row.get("entity_id"))
        if raw_type == "charger_fault":
            event_type = "fault"
            charger = chargers.get(entity_id)
            station = stations.get(charger["station_id"]) if charger else None
            if charger and station:
                message = f"设备告警：{station['name']} {charger['code']} 号桩上报故障"
            else:
                message = f"设备告警：充电桩 #{entity_id} 上报故障"
        elif raw_type == "order_completed":
            event_type = "order_completed"
            order = orders.get(entity_id)
            if order:
                station = stations.get(order["station_id"])
                message = (
                    f"订单完成：{station['name'] if station else '未知站点'}结算 "
                    f"{order['amount_fen'] / 100:.2f} 元"
                )
            else:
                message = f"订单完成：# {entity_id}"
        else:
            event_type = raw_type
            message = message_raw
        events.append(
            {
                "event_id": _int(row.get("id")),
                "event_type": event_type,
                "message": message,
                "message_raw": message_raw,
                "created_at": created_at.isoformat(timespec="seconds"),
            }
        )
    events.sort(key=lambda item: (item["created_at"], item["event_id"]))
    return events


# --------------------------------------------------------------------------- #
# RFM 分层
# --------------------------------------------------------------------------- #
RFM_SEGMENTS = [
    "重要价值客户",
    "重要保持客户",
    "重要发展客户",
    "重要挽留客户",
    "一般价值客户",
    "一般保持客户",
    "一般发展客户",
    "一般挽留客户",
]


def _quintile_scores(values: list[float], higher_is_better: bool) -> list[int]:
    """把一列值映射成 1..5 分（按分位等分，同值同分）。"""
    if not values:
        return []
    order = sorted(range(len(values)), key=lambda i: values[i], reverse=higher_is_better)
    n = len(order)
    scores = [0] * n
    for rank, index in enumerate(order):
        # rank 0 是最优 -> 分数 5
        scores[index] = 5 - min(4, int(rank * 5 / max(1, n)))
    return scores


def build_rfm(orders: list[dict], window_end: datetime) -> tuple[list[dict], dict]:
    per_user: dict[int, dict] = defaultdict(lambda: {"orders": 0, "amount": 0, "last": None})
    for order in orders:
        rec = per_user[order["user_id"]]
        rec["orders"] += 1
        rec["amount"] += order["amount_fen"]
        if rec["last"] is None or order["ended_at"] > rec["last"]:
            rec["last"] = order["ended_at"]

    users = list(per_user.items())
    if not users:
        return [], {}
    recency = [max(0.0, (window_end - rec["last"]).total_seconds() / 86400.0) for _, rec in users]
    frequency = [float(rec["orders"]) for _, rec in users]
    monetary = [float(rec["amount"]) for _, rec in users]
    r_score = _quintile_scores(recency, higher_is_better=False)
    f_score = _quintile_scores(frequency, higher_is_better=True)
    m_score = _quintile_scores(monetary, higher_is_better=True)

    buckets: dict[str, dict] = defaultdict(
        lambda: {"user_cnt": 0, "recency": 0.0, "frequency": 0.0, "monetary": 0.0, "revenue": 0}
    )
    for index, (user_id, rec) in enumerate(users):
        high_r, high_f, high_m = r_score[index] >= 4, f_score[index] >= 4, m_score[index] >= 4
        if high_r and high_f and high_m:
            segment = "重要价值客户"
        elif (not high_r) and high_f and high_m:
            segment = "重要保持客户"
        elif high_r and (not high_f) and high_m:
            segment = "重要发展客户"
        elif (not high_r) and (not high_f) and high_m:
            segment = "重要挽留客户"
        elif high_r and high_f and (not high_m):
            segment = "一般价值客户"
        elif (not high_r) and high_f and (not high_m):
            segment = "一般保持客户"
        elif high_r and (not high_f) and (not high_m):
            segment = "一般发展客户"
        else:
            segment = "一般挽留客户"
        bucket = buckets[segment]
        bucket["user_cnt"] += 1
        bucket["recency"] += recency[index]
        bucket["frequency"] += frequency[index]
        bucket["monetary"] += monetary[index]
        bucket["revenue"] += rec["amount"]

    rows = []
    for segment in RFM_SEGMENTS:
        bucket = buckets.get(segment)
        count = bucket["user_cnt"] if bucket else 0
        rows.append(
            {
                "segment": segment,
                "user_cnt": count,
                "avg_recency_days": round(bucket["recency"] / count, 1) if count else 0.0,
                "avg_frequency": round(bucket["frequency"] / count, 1) if count else 0.0,
                "avg_monetary_fen": round(bucket["monetary"] / count, 1) if count else 0.0,
                "revenue_fen": int(bucket["revenue"]) if bucket else 0,
            }
        )
    per_user_rfm = {
        user_id: {
            "segment": None,
            "monetary_fen": per_user[user_id]["amount"],
        }
        for user_id, _ in users
    }
    return rows, per_user_rfm


# --------------------------------------------------------------------------- #
# 预测批次（无 MLlib 批次时的 seasonal-naive 基线）
# --------------------------------------------------------------------------- #
def build_baseline_forecast(hourly: list[dict], stations: dict[int, dict],
                            generated_at: datetime) -> tuple[dict, list[dict]]:
    """按《05-PE》§8 降级预案：seasonal-naive（昨日同一时刻值）顶替，并如实标注。

    `05-PE` 明确「若 D3 仍不达标，按降级预案以 seasonal-naive 基线顶上并如实标注，
    或大屏显示「暂无预测」，不伪造结果」。这里选择前者，并在批次与 meta 里标注
    基线来源；#5 的真实 MLlib 批次落地后直接替换 `ads_forecast_24h` 即可。
    """
    by_station_last_day: dict[int, dict[int, dict]] = defaultdict(dict)
    last_dt = max((row["dt"] for row in hourly), default=None)
    if last_dt is None:
        return {}, []
    for row in hourly:
        if row["dt"] == last_dt:
            by_station_last_day[row["station_id"]][row["hour"]] = row

    anchor = datetime.fromisoformat(f"{last_dt}T00:00:00+08:00")
    forecast_day = anchor + timedelta(days=1)
    run_id = f"baseline-{forecast_day.date().isoformat()}"
    points: list[dict] = []
    for station_id, station in sorted(stations.items()):
        if not station["forecast_enabled"]:
            continue
        hours = by_station_last_day.get(station_id)
        if not hours or len(hours) < 24:
            continue
        rows = []
        for hour in range(24):
            source = hours.get(hour)
            if source is None:
                continue
            pile = max(1, int(source["pile_count"]))
            busy = min(pile, int(source["busy_count"]))
            load_kw = round(float(source["load_kw"]), 1)
            occupancy = busy / pile
            level = "high" if occupancy >= 0.8 else "medium" if occupancy >= 0.55 else "low"
            rows.append(
                {
                    "station_id": station_id,
                    "forecast_at": (forecast_day + timedelta(hours=hour)).isoformat(timespec="seconds"),
                    "horizon_h": hour + 1,
                    "predicted_load_kw": load_kw,
                    "predicted_busy_count": busy,
                    "predicted_idle_count": max(0, pile - busy),
                    "congestion_level": level,
                    "is_peak": 0,
                }
            )
        if len(rows) < 24:
            continue
        # 高峰 = 预测负荷最大的连续 2 小时（对齐《05》§2.3）
        peak_index = max(range(len(rows)), key=lambda i: rows[i]["predicted_load_kw"])
        rows[peak_index]["is_peak"] = 1
        if peak_index + 1 < len(rows):
            rows[peak_index + 1]["is_peak"] = 1
        points.extend(rows)

    batch = {
        "run_id": run_id,
        "model_version": "seasonal-naive-baseline",
        "activated_at": generated_at.isoformat(timespec="seconds"),
        "source": "ads_station_hourly(seasonal-naive: 昨日同时刻值)",
        "horizon_h_max": 24,
        "is_baseline": 1,
        "note": (
            "Spark MLlib 批次尚未交接（#5），按《05-PE》§8 降级预案以 seasonal-naive 基线顶替并如实标注；"
            "#5 交付 handoff/forecast 后替换 ads_forecast_24h 表即可，前端无需改动。"
        ),
    }
    return batch, points


def build_baseline_metrics(hourly: list[dict], stations: dict[int, dict]) -> list[dict]:
    """seasonal-naive 基线的回测指标：用「前一日同时刻值」预测「当日同时刻值」。

    指标口径（对齐《05-PE》§4 评估表）：
      MAE / RMSE 单位为 kW；WAPE = Σ|误差| / Σ|实际| × 100（百分比）。
      baselineWape 是「前一日全天均值」这个常量预测的 WAPE，用来证明
      seasonal-naive 相对朴素常数基线确有增益；若更差则如实体现，不伪造。
    """
    days = sorted({row["dt"] for row in hourly})
    if len(days) < 2:
        return []
    actual_day, prev_day = days[-1], days[-2]
    enabled = {sid for sid, st in stations.items() if st["forecast_enabled"]}

    def index(day: str) -> dict[tuple[int, int], float]:
        return {
            (row["station_id"], row["hour"]): float(row["load_kw"])
            for row in hourly
            if row["dt"] == day and row["station_id"] in enabled
        }

    actual, previous = index(actual_day), index(prev_day)
    buckets: dict[int, list[float]] = defaultdict(list)
    for (sid, _hour), value in previous.items():
        buckets[sid].append(value)
    per_station_mean = {sid: sum(v) / len(v) for sid, v in buckets.items()}

    metrics: list[dict] = []
    for hour in range(24):
        pairs = [
            (previous[(sid, hour)], actual[(sid, hour)], per_station_mean.get(sid, 0.0))
            for sid in sorted(enabled)
            if (sid, hour) in previous and (sid, hour) in actual
        ]
        if not pairs:
            metrics.append({"horizon_h": hour + 1, "mae": 0.0, "rmse": 0.0,
                            "wape": 0.0, "baseline_wape": 0.0})
            continue
        errors = [pred - real for pred, real, _ in pairs]
        total = sum(abs(real) for _, real, _ in pairs) or 1.0
        base_errors = [mean - real for _, real, mean in pairs]
        metrics.append({
            "horizon_h": hour + 1,
            "mae": round(sum(abs(e) for e in errors) / len(errors), 2),
            "rmse": round(math.sqrt(sum(e * e for e in errors) / len(errors)), 2),
            "wape": round(sum(abs(e) for e in errors) / total * 100.0, 2),
            "baseline_wape": round(sum(abs(e) for e in base_errors) / total * 100.0, 2),
        })
    return metrics


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #
def build(ods_dir: Path, out_dir: Path, generated_at: datetime | None = None) -> dict:
    generated_at = generated_at or datetime.now(CN_TZ).replace(microsecond=0)
    manifest_path = ods_dir / "manifest.json"
    ods_manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else {}
    rows_before = {name: int(meta.get("rows", 0)) for name, meta in (ods_manifest.get("tables") or {}).items()}

    injection_path = ods_dir / "injection_log.json"
    injection = json.loads(injection_path.read_text(encoding="utf-8")) if injection_path.exists() else {}

    detected: Counter = Counter()

    stations = load_stations(ods_dir, detected)
    chargers, charger_reference = load_chargers(ods_dir, detected)
    users = load_users(ods_dir, detected)
    orders = load_orders(ods_dir, users, stations, charger_reference, chargers, detected)
    hourly = load_station_hourly(ods_dir, stations, detected)
    rated_power = {cid: int(row["power_kw"]) for cid, row in chargers.items()}
    telemetry_total, telemetry_removed = scan_telemetry(ods_dir, rated_power, detected)
    orders_by_id = {order["order_id"]: order for order in orders}
    events = load_events(ods_dir, orders_by_id, stations, chargers, detected)

    # ---- 站点五状态与快慢充结构（由清洗后的桩维度聚合） ----
    per_station_chargers: dict[int, list[dict]] = defaultdict(list)
    for charger in chargers.values():
        per_station_chargers[charger["station_id"]].append(charger)

    # ---- 订单聚合 ----
    daily: dict[str, dict] = defaultdict(
        lambda: {"revenue_fen": 0, "energy_kwh": 0.0, "order_cnt": 0, "users": set()}
    )
    station_day: dict[tuple[str, int], dict] = defaultdict(
        lambda: {"revenue_fen": 0, "energy_kwh": 0.0, "order_cnt": 0,
                 "fast_order_cnt": 0, "slow_order_cnt": 0, "users": set()}
    )
    station_totals: dict[int, dict] = defaultdict(
        lambda: {"revenue_fen": 0, "energy_kwh": 0.0, "order_cnt": 0, "wait_min": 0.0}
    )
    district_totals: dict[str, dict] = defaultdict(
        lambda: {"revenue_fen": 0, "energy_kwh": 0.0, "order_cnt": 0, "wait_min": 0.0, "users": set()}
    )
    for order in orders:
        dt = order["started_at"].date().isoformat()
        station_id = order["station_id"]
        station = stations.get(station_id)
        district = station["district"] if station else "未知行政区"
        row = daily[dt]
        row["revenue_fen"] += order["amount_fen"]
        row["energy_kwh"] += order["energy_kwh"]
        row["order_cnt"] += 1
        row["users"].add(order["user_id"])

        srow = station_day[(dt, station_id)]
        srow["revenue_fen"] += order["amount_fen"]
        srow["energy_kwh"] += order["energy_kwh"]
        srow["order_cnt"] += 1
        srow["users"].add(order["user_id"])
        if order["charger_type"] == "fast":
            srow["fast_order_cnt"] += 1
        else:
            srow["slow_order_cnt"] += 1

        total = station_totals[station_id]
        total["revenue_fen"] += order["amount_fen"]
        total["energy_kwh"] += order["energy_kwh"]
        total["order_cnt"] += 1
        total["wait_min"] += order["wait_min"]

        drow = district_totals[district]
        drow["revenue_fen"] += order["amount_fen"]
        drow["energy_kwh"] += order["energy_kwh"]
        drow["order_cnt"] += 1
        drow["wait_min"] += order["wait_min"]
        drow["users"].add(order["user_id"])

    # ---- 站点小时聚合（利用率 / 峰值时段） ----
    hourly_by_station_day: dict[tuple[str, int], list[dict]] = defaultdict(list)
    for row in hourly:
        hourly_by_station_day[(row["dt"], row["station_id"])].append(row)

    # ---- 新增用户分布（首单新客口径，见下） ----
    # 生成器把 5000 个用户的注册时间全部放在业务窗口之前（存量用户池，2026-03~06），
    # 窗口期内没有任何注册事件；若直接按 registered_at 聚合会得到一条零线。
    # 因此 new_user_cnt 采用「首单新客」口径：当天首次完成订单的用户数。
    # 该选择已在 ads_meta.newUserNote 如实标注，答辩口径可追溯。
    first_order_day: dict[int, str] = {}
    for order in orders:
        dt = order["started_at"].date().isoformat()
        uid = order["user_id"]
        if uid not in first_order_day or dt < first_order_day[uid]:
            first_order_day[uid] = dt
    new_users_by_day: Counter = Counter(first_order_day.values())

    # ---- 业务窗口：以「有遥测或订单的日期」为准 ----
    # 不能把用户注册日期并进来，否则 ads_daily 会凭空多出只有新增用户、没有营收的
    # 空日（并让 windowDays / monthly 出现窗口外的幽灵月份）。
    activity_days = {row["dt"] for row in hourly} | set(daily)
    all_days = sorted(activity_days)
    window_start = all_days[0] if all_days else (ods_manifest.get("dataWindow", {}).get("start", "")[:10])
    window_end = all_days[-1] if all_days else generated_at.date().isoformat()
    window_days = len(all_days) or 1
    window_end_dt = (
        datetime.fromisoformat(f"{window_end}T00:00:00+08:00") if all_days else generated_at
    )

    rfm_rows, _ = build_rfm(orders, window_end_dt + timedelta(hours=24))

    # ---- 写库（幂等重建） ----
    out_dir.mkdir(parents=True, exist_ok=True)
    db_path = out_dir / "ads.db"
    connection = sqlite3.connect(db_path)
    # 不删文件、只 DROP 重建：目标目录可能受回收站/沙箱删除策略保护，
    # 且幂等重建对「反复重跑物化」更友好（文件 inode 不变，只读连接不会失效）。
    connection.executescript(RESET_SQL)
    connection.executescript(SCHEMA)

    max_orders = max((row["order_cnt"] for row in station_totals.values()), default=1)
    station_rows = []
    for station_id, station in sorted(stations.items()):
        charger_list = per_station_chargers.get(station_id, [])
        total_cnt = len(charger_list)
        status_counts = Counter(item["status"] for item in charger_list)
        fast = [item for item in charger_list if item["type"] == "fast"]
        slow = [item for item in charger_list if item["type"] != "fast"]
        total = station_totals.get(station_id, {})
        order_cnt = int(total.get("order_cnt", 0))
        station_rows.append(
            (
                station_id,
                station["name"],
                station["name_raw"],
                station["district"],
                station["district_raw"],
                station["address"],
                station["longitude"],
                station["latitude"],
                station["price_fen_per_kwh"],
                station["forecast_enabled"],
                total_cnt,
                len(fast),
                len(slow),
                max((item["power_kw"] for item in fast), default=0),
                max((item["power_kw"] for item in slow), default=0),
                status_counts.get("idle", 0),
                status_counts.get("reserved", 0),
                status_counts.get("charging", 0),
                status_counts.get("fault", 0),
                status_counts.get("restarting", 0),
                # 服务半径：运营规划参数，按站点订单需求规模折算（非实测，见 README 口径说明）
                round(min(3.2, max(0.8, 0.8 + 2.4 * math.sqrt(order_cnt / max(1, max_orders)))), 1),
                station["coord_imputed"],
            )
        )
    connection.executemany(
        "INSERT INTO ads_station VALUES (" + ",".join("?" * 22) + ")", station_rows
    )

    connection.executemany(
        "INSERT INTO ads_charger VALUES (?,?,?,?,?,?,?,?,?)",
        [
            (
                item["charger_id"], item["station_id"], item["code"], item["type"], item["power_kw"],
                item["status"], item["charge_count"], item["total_duration_sec"], item["fault_flag"],
            )
            for item in sorted(chargers.values(), key=lambda x: x["charger_id"])
        ],
    )

    connection.executemany(
        "INSERT INTO ads_daily VALUES (?,?,?,?,?,?)",
        [
            (
                dt,
                int(daily[dt]["revenue_fen"]),
                round(daily[dt]["energy_kwh"], 3),
                int(daily[dt]["order_cnt"]),
                int(new_users_by_day.get(dt, 0)),
                len(daily[dt]["users"]),
            )
            for dt in all_days
        ],
    )

    station_day_rows = []
    for (dt, station_id), row in sorted(station_day.items()):
        hours = hourly_by_station_day.get((dt, station_id)) or []
        avg_util = round(sum(h["utilization"] for h in hours) / len(hours), 1) if hours else 0.0
        peak_hour = max(hours, key=lambda h: h["busy_count"])["hour"] if hours else 0
        station_day_rows.append(
            (
                dt, station_id, int(row["revenue_fen"]), round(row["energy_kwh"], 3), int(row["order_cnt"]),
                int(row["fast_order_cnt"]), int(row["slow_order_cnt"]), avg_util, peak_hour,
            )
        )
    connection.executemany("INSERT INTO ads_station_day VALUES (?,?,?,?,?,?,?,?,?)", station_day_rows)

    connection.executemany(
        "INSERT INTO ads_station_hourly VALUES (?,?,?,?,?,?,?,?)",
        [
            (
                row["dt"], row["station_id"], row["hour"], row["observed_at"],
                row["pile_count"], row["busy_count"], row["load_kw"], row["utilization"],
            )
            for row in sorted(hourly, key=lambda r: (r["dt"], r["station_id"], r["hour"]))
        ],
    )

    connection.executemany(
        "INSERT INTO ads_user_rfm VALUES (?,?,?,?,?,?)",
        [
            (row["segment"], row["user_cnt"], row["avg_recency_days"], row["avg_frequency"],
             row["avg_monetary_fen"], row["revenue_fen"])
            for row in rfm_rows
        ],
    )

    district_rows = []
    for district, row in sorted(district_totals.items()):
        station_ids = [sid for sid, st in stations.items() if st["district"] == district]
        charger_cnt = sum(len(per_station_chargers.get(sid, [])) for sid in station_ids)
        # 行政区利用率 = 该区各站小时表利用率均值（与 station_day.avg_utilization 同口径）
        util_values = [
            h["utilization"] for h in hourly if h["station_id"] in station_ids
        ]
        population = DISTRICT_POPULATION.get(district, 1000000)
        district_rows.append(
            (
                district,
                len(station_ids),
                charger_cnt,
                population,
                int(row["order_cnt"]),
                len(row["users"]),
                round(row["wait_min"] / row["order_cnt"], 1) if row["order_cnt"] else 0.0,
                round(sum(util_values) / len(util_values), 1) if util_values else 0.0,
                round(row["energy_kwh"], 3),
                int(row["revenue_fen"]),
                round(row["energy_kwh"] / 1000.0 * 581.0, 2),
            )
        )
    connection.executemany("INSERT INTO ads_district VALUES (?,?,?,?,?,?,?,?,?,?,?)", district_rows)

    # ---- 质量对账 ----
    quality_detected = {
        "R01": detected["R01"],
        "R02": detected["R02"],
        "R03": detected["R03"],
        "R04": detected["R04"],
        "R05": detected["R05"],
        "R06": detected["R06"] + detected["R06_bad"],
        "R07": detected["R07"],
        "R08": detected["R08_users"] + detected["R08_chargers"],
        "R09": detected["R09"],
        "R10": detected["R10"] + detected["R10_users"],
    }
    injected_by_rule: Counter = Counter()
    for rule, summary in (injection.get("summary") or {}).items():
        target = INJECTION_TO_RULE.get(rule)
        if target:
            injected_by_rule[target] += int(summary.get("total", 0))
    # 生成器把「站名/昵称」算在同一个 Q10 下；非法枚举在同一个 Q8 下，这里已归并
    issue_rows = []
    for rule, label in RULE_TYPES:
        injected = injected_by_rule.get(rule, 0)
        found = quality_detected.get(rule, 0)
        handled = found
        recall = round(min(1.0, found / injected), 4) if injected else 0.0
        issue_rows.append((rule, label, injected, found, handled, recall))
    connection.executemany("INSERT INTO ads_quality_issue VALUES (?,?,?,?,?,?)", issue_rows)

    kept_orders = len(best := orders_by_id)
    del best
    raw_orders = rows_before.get("ods_orders", 0)
    completed_raw = raw_orders  # 含 cancelled，清洗后仅保留 completed 且合规
    quality_tables = [
        ("ods_orders", raw_orders, len(orders)),
        ("ods_telemetry", telemetry_total, telemetry_total - telemetry_removed),
        ("ods_station_hourly", rows_before.get("ods_station_hourly", len(hourly)), len(hourly)),
        ("ods_users", rows_before.get("ods_users", len(users)), len(users)),
        ("ods_chargers", rows_before.get("ods_chargers", 0), len(chargers)),
        ("ods_stations", rows_before.get("ods_stations", len(stations)), len(stations)),
        ("ods_events", rows_before.get("ods_events", len(events)), len(events)),
    ]
    connection.executemany("INSERT INTO ads_quality_table VALUES (?,?,?)", quality_tables)
    connection.executemany(
        "INSERT INTO ads_quality_meta VALUES (?,?)",
        [
            ("run_id", f"ads-quality-{generated_at.strftime('%Y%m%d%H%M')}"),
            ("source", "ADS 侧按 PRL §3.2 规则对 ODS 独立复算"),
            ("injection_run_id", str(injection.get("runId", ""))),
            ("note", "#3 的 DWD 清洗报告（handoff/dwd/cleaning_report.json）交付后以其为准"),
        ],
    )

    # ---- 预测批次 ----
    batch, forecast_points = build_baseline_forecast(hourly, stations, generated_at)
    metrics = build_baseline_metrics(hourly, stations) if batch else []
    if batch:
        connection.execute(
            "INSERT INTO ads_forecast_batch VALUES (?,?,?,?,?,?,?)",
            (batch["run_id"], batch["model_version"], batch["activated_at"], batch["source"],
             batch["horizon_h_max"], batch["is_baseline"], batch["note"]),
        )
        connection.executemany(
            "INSERT INTO ads_forecast_24h VALUES (?,?,?,?,?,?,?,?,?)",
            [
                (batch["run_id"], p["station_id"], p["forecast_at"], p["horizon_h"],
                 p["predicted_load_kw"], p["predicted_busy_count"], p["predicted_idle_count"],
                 p["congestion_level"], p["is_peak"])
                for p in forecast_points
            ],
        )
        connection.executemany(
            "INSERT INTO ads_forecast_metric VALUES (?,?,?,?,?)",
            [(m["horizon_h"], m["mae"], m["rmse"], m["wape"], m["baseline_wape"]) for m in metrics],
        )

    # ---- 事件流 ----
    connection.executemany(
        "INSERT INTO ads_event VALUES (?,?,?,?,?)",
        [
            (row["event_id"], row["event_type"], row["message"], row["message_raw"], row["created_at"])
            for row in events
        ],
    )

    # ---- meta ----
    total_revenue = sum(int(row["revenue_fen"]) for row in daily.values())
    total_energy = round(sum(row["energy_kwh"] for row in daily.values()), 3)
    total_orders = sum(int(row["order_cnt"]) for row in daily.values())
    meta = {
        "adsContractVersion": "ads-flask-v1",
        "runId": f"ads-{generated_at.strftime('%Y%m%d%H%M%S')}",
        "generatedAt": generated_at.isoformat(timespec="seconds"),
        "sourceKind": "ods-handoff",
        "sourceRunId": str(ods_manifest.get("runId", "")),
        "sourceSeed": str(ods_manifest.get("seed", "")),
        "dataWindowStart": f"{window_start}T00:00:00+08:00",
        "dataWindowEnd": f"{window_end}T23:59:59+08:00",
        "windowDays": window_days,
        "stationCount": len(stations),
        "chargerCount": len(chargers),
        "userCount": len(users),
        "orderCount": total_orders,
        "totalRevenueFen": total_revenue,
        "totalEnergyKwh": total_energy,
        "forecastSource": batch["source"] if batch else "none",
        "forecastIsBaseline": "1" if batch else "0",
        "carbonFactorTonPerMwh": CARBON_FACTOR_TON_PER_MWH,
        "carbonFactorNote": CARBON_FACTOR_NOTE,
        "equivalentTrees": int(round(total_energy / 1000.0 * CARBON_FACTOR_TON_PER_MWH * 1000 / KG_CO2_PER_TREE_YEAR)),
        "cleaningSource": "PRL §3.2（ODS -> ADS 过渡口径，DWD 交付后切换）",
        "populationSource": "北京市第七次全国人口普查常住人口（外部参考数据，非生成器产出）",
        "serviceRadiusNote": "服务半径为运营规划参数，按站点订单需求规模折算，非实测",
        "avgWaitNote": "平均等待 = started_at − reserved_at；当前 ODS 未建模预约到开工的排队时长，故为 0",
        "newUserNote": (
            "new_user_cnt 为「窗口内首次完成订单的用户数」（首单新客口径）。"
            f"生成器 {len(users)} 个用户的注册时间全部早于业务窗口（2026-03~06 存量池），"
            "窗口期内没有注册事件，若按 registered_at 聚合会得到零线；"
            "如需改回注册口径，须由 #4 让生成器在窗口期内注入注册时间并重新交接 ODS。"
        ),
        "registeredUserCount": len(users),
        "firstOrderUserCount": len(first_order_day),
    }
    connection.executemany("INSERT INTO ads_meta VALUES (?,?)", [(k, str(v)) for k, v in meta.items()])
    connection.commit()
    connection.close()

    manifest = {
        "contractVersion": "ads-flask-v1",
        "runId": meta["runId"],
        "generatedAt": meta["generatedAt"],
        "source": "ods-handoff",
        "sourceRunId": meta["sourceRunId"],
        "dataWindow": {"start": meta["dataWindowStart"], "end": meta["dataWindowEnd"], "days": window_days},
        "database": "ads.db",
        "tables": {
            "ads_station": len(station_rows),
            "ads_charger": len(chargers),
            "ads_daily": len(all_days),
            "ads_station_day": len(station_day_rows),
            "ads_station_hourly": len(hourly),
            "ads_user_rfm": len(rfm_rows),
            "ads_district": len(district_rows),
            "ads_quality_issue": len(issue_rows),
            "ads_forecast_24h": len(forecast_points),
            "ads_forecast_metric": len(metrics),
            "ads_event": len(events),
        },
        "quality": {"injected": sum(injected_by_rule.values()), "detected": sum(quality_detected.values())},
        "forecast": {"source": batch["model_version"] if batch else "none", "isBaseline": bool(batch)},
    }
    (out_dir / "ads_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="ODS -> ads.db 物化作业")
    parser.add_argument("--ods", type=Path, default=Path("handoff/ods"))
    parser.add_argument("--out", type=Path, default=Path("handoff/ads"))
    parser.add_argument("--generated-at", type=str, default=None,
                        help="固定 generatedAt（ISO 8601），用于可复现构建")
    args = parser.parse_args()
    fixed = datetime.fromisoformat(args.generated_at).replace(tzinfo=CN_TZ) if args.generated_at else None
    manifest = build(args.ods, args.out, fixed)
    print(json.dumps({"ok": True, "manifest": manifest}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
