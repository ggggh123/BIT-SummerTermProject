#!/usr/bin/env python3
"""本地物化作业：ODS 交接包 -> DWS 四表 + `handoff/ads/ads.db`（纯标准库）。

### 它和 Spark 作业是什么关系

正式链路（`scripts/run_dws_ads.sh`）在伪分布式 Hadoop 上用 SparkSQL 跑
`warehouse/sql/dws_etl.sql` + `ads_etl.sql`，这是要求（5）的验收点。
但 **Spark 只在虚拟机里有**：前端联调、答辩现场查数、CI 自测都需要一台
没装 Hadoop 的机器也能出数。所以本作业是同一套口径的**第二实现**：

    ODS ──[本作业：Python]──> handoff/dws + handoff/ads/ads.db
    ODS ──[Spark：SQL]─────> /ev-charging/dws + /ev-charging/ads + 导出 ads.db

两份实现的对应关系（字段级）写在 `warehouse/README.md`；
`reconcile.py` 在两者都在的情况下会做交叉比对，**不是**「随便挑一个信」。

### 用法

    python warehouse/jobs/build_local.py \\
        --ods   handoff/ods \\
        --dws   handoff/dws \\
        --ads   handoff/ads \\
        [--generated-at 2026-09-14T14:30:00+08:00]

`--generated-at` 用于可复现构建：固定该值后重跑，`ads.db` 内容逐字节一致。

### 产出

* `handoff/dws/dws_*.csv` + `dws_manifest.json`（行数 + SHA-256）
* `handoff/ads/ads.db`（15 张表，DDL 真源 = `warehouse/sql/ads_schema.sql`）
* `handoff/ads/ads_manifest.json`
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import _ads_extras as extras  # noqa: E402
from _lib import (  # noqa: E402
    CARBON_FACTOR_NOTE,
    CARBON_FACTOR_TON_PER_MWH,
    CHARGER_STATES,
    CN_TZ,
    DISTRICT_POPULATION,
    KG_CO2_PER_TREE_YEAR,
    NULL_TEXT,
    MAX_SESSION_KWH,
    impute_coordinate,
    is_valid_coordinate,
    is_valid_mobile,
    iter_rows,
    needs_text_repair,
    normalize_text,
    parse_timestamp,
    resolve_amount_fen,
    station_display_name,
    to_district_cn,
    to_float,
    to_int,
    write_ads_db,
)

DWS_TABLES = ["dws_station_day", "dws_charger_day", "dws_user_day", "dws_region_day"]


# --------------------------------------------------------------------------- #
# ODS 读取与清洗
# --------------------------------------------------------------------------- #
def load_stations(ods_dir: Path, detected: Counter) -> dict[int, dict]:
    stations: dict[int, dict] = {}
    for row in iter_rows(ods_dir / "ods_stations"):
        station_id = to_int(row.get("id"))
        if not station_id:
            continue
        district_raw = normalize_text(row.get("district"))
        district_cn = to_district_cn(district_raw)
        name_raw = normalize_text(row.get("name"))
        latitude = to_float(row.get("latitude"))
        longitude = to_float(row.get("longitude"))
        imputed = 0
        if not is_valid_coordinate(latitude, longitude):
            detected["R09"] += 1
            latitude, longitude = impute_coordinate(district_cn, station_id)
            imputed = 1
        if needs_text_repair(row.get("name"), name_raw):
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
            "price_fen_per_kwh": to_int(row.get("price_fen_per_kwh"), 100),
            "forecast_enabled": to_int(row.get("forecast_enabled")),
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
        charger_id = to_int(row.get("id"))
        if not charger_id:
            continue
        station_id = to_int(row.get("station_id"))
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
            "power_kw": to_int(row.get("power_kw")),
            "status": status,
            "charge_count": to_int(row.get("charge_count")),
            "total_duration_sec": to_int(row.get("total_duration_sec")),
            "fault_flag": 1 if status == "fault" else 0,
        }
    return cleaned, raw_reference


def load_users(ods_dir: Path, detected: Counter) -> dict[int, dict]:
    users: dict[int, dict] = {}
    for row in iter_rows(ods_dir / "ods_users"):
        user_id = to_int(row.get("id"))
        if not user_id:
            continue
        if not is_valid_mobile(row.get("mobile")):  # R08
            detected["R08_users"] += 1
            continue
        nickname = normalize_text(row.get("nickname"))
        if needs_text_repair(row.get("nickname"), nickname):
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
        order_id = to_int(row.get("id"))
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

        for field, parsed in (
            ("reserved_at", reserved_at),
            ("started_at", started_at),
            ("ended_at", ended_at),
        ):
            if normalize_text(row.get(field)) and parsed is None:
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
        energy_kwh = to_float(row.get("energy_kwh"), -1.0)
        if energy_kwh < 0 or energy_kwh > MAX_SESSION_KWH:
            detected["R03"] += 1
            continue
        # R07 孤儿引用 -> 剔除
        user_id = to_int(row.get("user_id"))
        charger_id = to_int(row.get("charger_id"))
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


def load_station_hourly(
    ods_dir: Path, stations: dict[int, dict], detected: Counter
) -> list[dict]:
    rows: list[dict] = []
    for row in iter_rows(ods_dir / "ods_station_hourly"):
        observed_at = parse_timestamp(row.get("observed_at"))
        if observed_at is None:
            detected["R04"] += 1
            continue
        station_id = to_int(row.get("station_id"))
        if station_id not in stations:
            detected["R07"] += 1
            continue
        pile_count = max(1, to_int(row.get("pile_count"), 1))
        busy_count = to_int(row.get("busy_count"))
        if busy_count > pile_count:  # R05 -> 裁剪回 pile_count
            detected["R05"] += 1
            busy_count = pile_count
        busy_count = max(0, busy_count)
        rows.append(
            {
                "dt": observed_at.date().isoformat(),
                "station_id": station_id,
                "hour": observed_at.hour,
                "observed_at": observed_at.isoformat(timespec="seconds"),
                "pile_count": pile_count,
                "busy_count": busy_count,
                "load_kw": round(max(0.0, to_float(row.get("load_kw"))), 3),
                "utilization": round(busy_count / pile_count * 100.0, 1),
            }
        )
    return rows


def scan_telemetry(ods_dir: Path, rated_power: dict[int, int], detected: Counter) -> tuple[int, int]:
    """流式扫描遥测表：只做质量对账计数，ADS 不直接消费桩级明细。

    返回 (总行数, 剔除行数)。用流式而不是整表读进内存：全量 100 万行。
    """
    total = 0
    removed = 0
    seen: set[int] = set()
    for row in iter_rows(ods_dir / "ods_telemetry"):
        total += 1
        row_id = to_int(row.get("id"))
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
            power = to_float(power_raw, -1.0)
            rated = rated_power.get(to_int(row.get("charger_id")), 120)
            if power < 0 or power > rated * 1.05:
                detected["R03"] += 1
                bad = True
        if parse_timestamp(row.get("recorded_at")) is None:
            detected["R04"] += 1
            bad = True
        if bad:
            removed += 1
    return total, removed


def load_events_and_faults(
    ods_dir: Path,
    orders_by_id: dict[int, dict],
    stations: dict[int, dict],
    chargers: dict[int, dict],
    charger_reference: dict[int, int],
    detected: Counter,
) -> tuple[list[dict], set[tuple[str, int, int]]]:
    """读取事件流：返回 (清洗后事件, 故障事件集合)。

    故障集合是 `(dt, station_id, charger_id)` 三元组的集合，供 DWS 的
    `fault_cnt` / `fault_flag` 使用 —— 生成器把故障写成**独立事件**
    （`event_type = 'charger_fault'`），遥测表的 `event_type` 恒为 `'telemetry'`，
    所以故障只能从事件流取。
    """
    events: list[dict] = []
    faults: set[tuple[str, int, int]] = set()
    seen_ids: set[int] = set()
    for row in iter_rows(ods_dir / "ods_events"):
        created_at = parse_timestamp(row.get("created_at"))
        if created_at is None:
            detected["R04"] += 1
            continue
        # R02：事件表主键去重（重复投递在真实事件流里很常见）
        event_id = to_int(row.get("id"))
        if event_id in seen_ids:
            detected["R02"] += 1
            continue
        seen_ids.add(event_id)
        raw_type = normalize_text(row.get("event_type")) or "unknown"
        message_raw = normalize_text(row.get("message"))
        entity_id = to_int(row.get("entity_id"))
        if raw_type == "charger_fault":
            event_type = "fault"
            station_id = charger_reference.get(entity_id)
            if station_id is not None:
                faults.add((created_at.date().isoformat(), station_id, entity_id))
            charger = chargers.get(entity_id)
            station = stations.get(charger["station_id"]) if charger else None
            if charger and station:
                message = f"设备告警：{station['name']} {charger['code']} 号桩上报故障"
            else:
                message = f"设备告警：充电桩 #{entity_id} 上报故障"
        elif raw_type == "order_completed":
            event_type = "order_completed"
            order = orders_by_id.get(entity_id)
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
                "event_id": event_id,
                "event_type": event_type,
                "message": message,
                "message_raw": message_raw,
                "created_at": created_at.isoformat(timespec="seconds"),
            }
        )
    events.sort(key=lambda item: (item["created_at"], item["event_id"]))
    return events, faults


# --------------------------------------------------------------------------- #
# DWS 四表
# --------------------------------------------------------------------------- #
def build_dws(
    orders: list[dict],
    hourly: list[dict],
    chargers: dict[int, dict],
    stations: dict[int, dict],
    faults: set[tuple[str, int, int]],
) -> dict[str, list[dict]]:
    """按 `warehouse/sql/dws_etl.sql` 的口径产出 DWS 四表（字段名与 SQL 完全一致）。"""
    # ---- 订单侧聚合 ----
    order_totals: dict[tuple[str, int], dict] = defaultdict(
        lambda: {"order_cnt": 0, "energy_kwh": 0.0, "revenue_fen": 0,
                 "fast_order_cnt": 0, "slow_order_cnt": 0}
    )
    charger_day: dict[tuple[str, int], dict] = defaultdict(
        lambda: {"order_cnt": 0, "energy_kwh": 0.0, "charge_duration_sec": 0}
    )
    user_day: dict[tuple[str, int], dict] = defaultdict(
        lambda: {"order_cnt": 0, "energy_kwh": 0.0, "amount_fen": 0}
    )
    region_day: dict[tuple[str, str], dict] = defaultdict(
        lambda: {"order_cnt": 0, "energy_kwh": 0.0, "revenue_fen": 0, "users": set()}
    )

    for order in orders:
        dt = order["started_at"].date().isoformat()
        station_id = order["station_id"]
        station = stations.get(station_id)
        district = station["district"] if station else "未知行政区"

        row = order_totals[(dt, station_id)]
        row["order_cnt"] += 1
        row["energy_kwh"] += order["energy_kwh"]
        row["revenue_fen"] += order["amount_fen"]
        if order["charger_type"] == "fast":
            row["fast_order_cnt"] += 1
        else:
            row["slow_order_cnt"] += 1

        crow = charger_day[(dt, order["charger_id"])]
        crow["order_cnt"] += 1
        crow["energy_kwh"] += order["energy_kwh"]
        crow["charge_duration_sec"] += int(
            (order["ended_at"] - order["started_at"]).total_seconds()
        )

        urow = user_day[(dt, order["user_id"])]
        urow["order_cnt"] += 1
        urow["energy_kwh"] += order["energy_kwh"]
        urow["amount_fen"] += order["amount_fen"]

        rrow = region_day[(dt, district)]
        rrow["order_cnt"] += 1
        rrow["energy_kwh"] += order["energy_kwh"]
        rrow["revenue_fen"] += order["amount_fen"]
        rrow["users"].add(order["user_id"])

    # ---- 小时侧：利用率均值与峰值时段 ----
    hourly_group: dict[tuple[str, int], list[dict]] = defaultdict(list)
    for row in hourly:
        hourly_group[(row["dt"], row["station_id"])].append(row)
    util_avg = {
        key: round(sum(h["utilization"] for h in rows) / len(rows), 1)
        for key, rows in hourly_group.items()
        if rows
    }
    peak_hour = {
        # 并列时取**更早**的整点（SQL 侧 ORDER BY busy_count DESC, hour ASC 同口径）
        key: max(rows, key=lambda h: (h["busy_count"], -h["hour"]))["hour"]
        for key, rows in hourly_group.items()
        if rows
    }

    # ---- 故障：按 (dt, station) 与 (dt, charger) 两个粒度 ----
    fault_by_station: dict[tuple[str, int], set[int]] = defaultdict(set)
    fault_by_charger: set[tuple[str, int]] = set()
    for dt, station_id, charger_id in faults:
        fault_by_station[(dt, station_id)].add(charger_id)
        fault_by_charger.add((dt, charger_id))

    # ---- DWS-1 站点 × 日（以小时表驱动，保住「当天无订单但有占用」的站点日） ----
    station_day_rows = []
    for key in sorted(hourly_group):
        dt, station_id = key
        totals = order_totals.get(key, {})
        station_day_rows.append(
            {
                "dt": dt,
                "station_id": station_id,
                "order_cnt": int(totals.get("order_cnt", 0)),
                "energy_kwh": round(totals.get("energy_kwh", 0.0), 3),
                "revenue_fen": int(totals.get("revenue_fen", 0)),
                "avg_utilization": util_avg.get(key, 0.0),
                "peak_hour": peak_hour.get(key, 0),
                "fast_order_cnt": int(totals.get("fast_order_cnt", 0)),
                "slow_order_cnt": int(totals.get("slow_order_cnt", 0)),
                "fault_cnt": len(fault_by_station.get(key, ())),
            }
        )

    # ---- DWS-2 充电桩 × 日（稀疏：有订单或有故障的桩） ----
    charger_day_keys = set(charger_day) | fault_by_charger
    charger_day_rows = []
    for dt, charger_id in sorted(charger_day_keys):
        row = charger_day.get((dt, charger_id), {})
        charger_day_rows.append(
            {
                "dt": dt,
                "charger_id": charger_id,
                "order_cnt": int(row.get("order_cnt", 0)),
                "energy_kwh": round(row.get("energy_kwh", 0.0), 3),
                "charge_duration_sec": int(row.get("charge_duration_sec", 0)),
                "fault_flag": 1 if (dt, charger_id) in fault_by_charger else 0,
            }
        )

    # ---- DWS-3 用户 × 日 ----
    user_day_rows = [
        {
            "dt": dt,
            "user_id": user_id,
            "order_cnt": int(row["order_cnt"]),
            "energy_kwh": round(row["energy_kwh"], 3),
            "amount_fen": int(row["amount_fen"]),
            "active_flag": 1,
        }
        for (dt, user_id), row in sorted(user_day.items())
    ]

    # ---- DWS-4 行政区 × 日 ----
    station_cnt: dict[str, int] = defaultdict(int)
    charger_cnt: dict[str, int] = defaultdict(int)
    for station in stations.values():
        station_cnt[station["district"]] += 1
    for charger in chargers.values():
        station = stations.get(charger["station_id"])
        if station:
            charger_cnt[station["district"]] += 1
    region_day_rows = [
        {
            "dt": dt,
            "district": district,
            "station_cnt": station_cnt.get(district, 0),
            "charger_cnt": charger_cnt.get(district, 0),
            "order_cnt": int(row["order_cnt"]),
            "energy_kwh": round(row["energy_kwh"], 3),
            "revenue_fen": int(row["revenue_fen"]),
            "served_user_cnt": len(row["users"]),
        }
        for (dt, district), row in sorted(region_day.items())
    ]

    return {
        "dws_station_day": station_day_rows,
        "dws_charger_day": charger_day_rows,
        "dws_user_day": user_day_rows,
        "dws_region_day": region_day_rows,
    }


# --------------------------------------------------------------------------- #
# 写 DWS 交接包（CSV + 行数 + SHA-256，与 ODS 交接包同风格）
# --------------------------------------------------------------------------- #
def write_dws(out_dir: Path, tables: dict[str, list[dict]], *, run_id: str, generated_at: datetime,
              source_run_id: str) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    table_meta: dict[str, dict] = {}
    for name, rows in tables.items():
        table_dir = out_dir / name
        table_dir.mkdir(parents=True, exist_ok=True)
        part = table_dir / "part-00000.csv"
        columns = list(rows[0].keys()) if rows else []
        with part.open("w", encoding="utf-8", newline="") as handle:
            if columns:
                writer = csv.DictWriter(handle, fieldnames=columns)
                writer.writeheader()
                writer.writerows(rows)
        digest = hashlib.sha256(part.read_bytes()).hexdigest()
        (table_dir / "_SUCCESS").write_text("", encoding="utf-8")
        table_meta[name] = {
            "rows": len(rows),
            "format": "csv",
            "partitioned": False,
            "files": [f"{name}/part-00000.csv"],
            "sha256": digest,
            "path": name,
        }

    manifest = {
        "contractVersion": "dws-v0.1",
        "kind": "dws-handoff",
        "runId": run_id,
        "generatedAt": generated_at.isoformat(timespec="seconds"),
        "sourceKind": "ods-handoff",
        "sourceRunId": source_run_id,
        "producer": "warehouse/jobs/build_local.py（Spark 不可用时的同构物化）",
        "tables": table_meta,
    }
    (out_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return manifest


# --------------------------------------------------------------------------- #
# ADS 业务表（与 ads_etl.sql 的 7 张表一一对应）
# --------------------------------------------------------------------------- #
def build_ads_business(
    dws: dict[str, list[dict]],
    hourly: list[dict],
    chargers: dict[int, dict],
    stations: dict[int, dict],
    orders: list[dict],
    district_util: dict[str, float],
) -> dict[str, list[tuple]]:
    station_day = dws["dws_station_day"]
    region_day = dws["dws_region_day"]

    # ---- ADS-1 站点维度快照 ----
    per_station_chargers: dict[int, list[dict]] = defaultdict(list)
    for charger in chargers.values():
        per_station_chargers[charger["station_id"]].append(charger)
    station_orders: dict[int, int] = defaultdict(int)
    for row in station_day:
        station_orders[row["station_id"]] += row["order_cnt"]
    max_orders = max(station_orders.values(), default=1)

    ads_station = []
    for station_id, station in sorted(stations.items()):
        charger_list = per_station_chargers.get(station_id, [])
        status_counts = Counter(item["status"] for item in charger_list)
        fast = [item for item in charger_list if item["type"] == "fast"]
        slow = [item for item in charger_list if item["type"] != "fast"]
        order_cnt = station_orders.get(station_id, 0)
        ads_station.append(
            {
                "station_id": station_id,
                "name": station["name"],
                "name_raw": station["name_raw"],
                "district": station["district"],
                "district_raw": station["district_raw"],
                "address": station["address"],
                "longitude": station["longitude"],
                "latitude": station["latitude"],
                "price_fen_per_kwh": station["price_fen_per_kwh"],
                "forecast_enabled": station["forecast_enabled"],
                "charger_cnt": len(charger_list),
                "fast_cnt": len(fast),
                "slow_cnt": len(slow),
                "fast_power_kw": max((item["power_kw"] for item in fast), default=0),
                "slow_power_kw": max((item["power_kw"] for item in slow), default=0),
                "idle_cnt": status_counts.get("idle", 0),
                "reserved_cnt": status_counts.get("reserved", 0),
                "charging_cnt": status_counts.get("charging", 0),
                "fault_cnt": status_counts.get("fault", 0),
                "restarting_cnt": status_counts.get("restarting", 0),
                # 服务半径：运营规划参数，按订单需求规模折算（非实测，见 ads_meta.serviceRadiusNote）
                "service_radius_km": round(
                    min(3.2, max(0.8, 0.8 + 2.4 * math.sqrt(order_cnt / max(1, max_orders)))), 1
                ),
                "coord_imputed": station["coord_imputed"],
            }
        )

    # ---- ADS-2 充电桩维度快照 ----
    ads_charger = [
        {
            "charger_id": item["charger_id"],
            "station_id": item["station_id"],
            "code": item["code"],
            "type": item["type"],
            "power_kw": item["power_kw"],
            "status": item["status"],
            "charge_count": item["charge_count"],
            "total_duration_sec": item["total_duration_sec"],
            "fault_flag": item["fault_flag"],
        }
        for item in sorted(chargers.values(), key=lambda x: x["charger_id"])
    ]

    # ---- ADS-3 全城 × 日（由 DWS 站点日表汇总） ----
    daily: dict[str, dict] = defaultdict(
        lambda: {"revenue_fen": 0, "energy_kwh": 0.0, "order_cnt": 0}
    )
    for row in station_day:
        entry = daily[row["dt"]]
        entry["revenue_fen"] += row["revenue_fen"]
        entry["energy_kwh"] += row["energy_kwh"]
        entry["order_cnt"] += row["order_cnt"]
    active_users: dict[str, set[int]] = defaultdict(set)
    first_order_day: dict[int, str] = {}
    for row in dws["dws_user_day"]:
        active_users[row["dt"]].add(row["user_id"])
        uid, dt = row["user_id"], row["dt"]
        if uid not in first_order_day or dt < first_order_day[uid]:
            first_order_day[uid] = dt
    new_user_by_day = Counter(first_order_day.values())

    all_days = sorted(daily)
    ads_daily = [
        {
            "dt": dt,
            "revenue_fen": int(daily[dt]["revenue_fen"]),
            "energy_kwh": round(daily[dt]["energy_kwh"], 3),
            "order_cnt": int(daily[dt]["order_cnt"]),
            "new_user_cnt": int(new_user_by_day.get(dt, 0)),
            "active_user_cnt": len(active_users.get(dt, ())),
        }
        for dt in all_days
    ]

    # ---- ADS-4 站点 × 日（契约化重命名，与 DWS 同构） ----
    ads_station_day = [
        {
            "dt": row["dt"],
            "station_id": row["station_id"],
            "revenue_fen": row["revenue_fen"],
            "energy_kwh": row["energy_kwh"],
            "order_cnt": row["order_cnt"],
            "fast_order_cnt": row["fast_order_cnt"],
            "slow_order_cnt": row["slow_order_cnt"],
            "avg_utilization": row["avg_utilization"],
            "peak_hour": row["peak_hour"],
        }
        for row in station_day
    ]

    # ---- ADS-5 站点 × 小时 ----
    ads_station_hourly = [
        {
            "dt": row["dt"],
            "station_id": row["station_id"],
            "hour": row["hour"],
            "observed_at": row["observed_at"],
            "pile_count": row["pile_count"],
            "busy_count": row["busy_count"],
            "load_kw": row["load_kw"],
            "utilization": row["utilization"],
        }
        for row in sorted(hourly, key=lambda r: (r["dt"], r["station_id"], r["hour"]))
    ]

    # ---- ADS-7 行政区 ----
    district_totals: dict[str, dict] = defaultdict(
        lambda: {"order_cnt": 0, "energy_kwh": 0.0, "revenue_fen": 0, "wait_min": 0.0, "users": set()}
    )
    for order in orders:
        station = stations.get(order["station_id"])
        district = station["district"] if station else "未知行政区"
        entry = district_totals[district]
        entry["order_cnt"] += 1
        entry["energy_kwh"] += order["energy_kwh"]
        entry["revenue_fen"] += order["amount_fen"]
        entry["wait_min"] += order["wait_min"]
        entry["users"].add(order["user_id"])

    ads_district = []
    for district, entry in sorted(district_totals.items()):
        station_ids = [sid for sid, st in stations.items() if st["district"] == district]
        ads_district.append(
            {
                "district": district,
                "station_cnt": len(station_ids),
                "charger_cnt": sum(len(per_station_chargers.get(sid, [])) for sid in station_ids),
                "population": DISTRICT_POPULATION.get(district, 1000000),
                "order_cnt": int(entry["order_cnt"]),
                "served_user_cnt": len(entry["users"]),
                "avg_wait_min": (
                    round(entry["wait_min"] / entry["order_cnt"], 1) if entry["order_cnt"] else 0.0
                ),
                "utilization_rate": round(district_util.get(district, 0.0), 1),
                "energy_kwh": round(entry["energy_kwh"], 3),
                "revenue_fen": int(entry["revenue_fen"]),
                "co2_saved_kg": round(
                    entry["energy_kwh"] / 1000.0 * (CARBON_FACTOR_TON_PER_MWH * 1000), 2
                ),
            }
        )

    return {
        "ads_station": ads_station,
        "ads_charger": ads_charger,
        "ads_daily": ads_daily,
        "ads_station_day": ads_station_day,
        "ads_station_hourly": ads_station_hourly,
        "ads_district": ads_district,
    }


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #
def build(ods_dir: Path, dws_dir: Path, ads_dir: Path,
          generated_at: datetime | None = None,
          forecast_handoff: Path | None = None) -> dict:
    generated_at = generated_at or datetime.now(CN_TZ).replace(microsecond=0)

    ods_manifest_path = ods_dir / "manifest.json"
    ods_manifest = (
        json.loads(ods_manifest_path.read_text(encoding="utf-8"))
        if ods_manifest_path.exists()
        else {}
    )
    # 交接包有两种 kind：`ods-handoff`（正式规模）/ `prl-test-fixture`（小样例）。
    # 从小样例建出来的 `ads.db` 只有几十行，拿它给前端联调会得到满屏空图，
    # 所以这里只提示、不阻断 —— 自测确实需要用样例跑通全链路。
    if ods_manifest.get("kind") not in (None, "ods-handoff"):
        print(
            f"[warn] ODS 交接包的 kind = {ods_manifest.get('kind')!r}，不是正式规模 "
            f"`ods-handoff`；产物仅供自测，勿用于联调。",
            file=sys.stderr,
        )
    rows_before = {
        name: int(meta.get("rows", 0))
        for name, meta in (ods_manifest.get("tables") or {}).items()
    }
    injection_path = ods_dir / "injection_log.json"
    injection = (
        json.loads(injection_path.read_text(encoding="utf-8"))
        if injection_path.exists()
        else {}
    )

    detected: Counter = Counter()

    # ---- 读 + 清洗 ----
    stations = load_stations(ods_dir, detected)
    chargers, charger_reference = load_chargers(ods_dir, detected)
    users = load_users(ods_dir, detected)
    orders = load_orders(ods_dir, users, stations, charger_reference, chargers, detected)
    hourly = load_station_hourly(ods_dir, stations, detected)
    rated_power = {cid: int(row["power_kw"]) for cid, row in chargers.items()}
    telemetry_total, telemetry_removed = scan_telemetry(ods_dir, rated_power, detected)
    orders_by_id = {order["order_id"]: order for order in orders}
    events, faults = load_events_and_faults(
        ods_dir, orders_by_id, stations, chargers, charger_reference, detected
    )

    # ---- DWS ----
    dws = build_dws(orders, hourly, chargers, stations, faults)
    run_id = f"ads-{generated_at.strftime('%Y%m%d%H%M%S')}"
    dws_manifest = write_dws(
        dws_dir, dws, run_id=f"dws-{generated_at.strftime('%Y%m%d%H%M%S')}",
        generated_at=generated_at,
        source_run_id=str(ods_manifest.get("runId", "")),
    )

    # ---- 窗口口径 ----
    # 以「有遥测或订单的日期」为准；不能把用户注册日期并进来，否则 ads_daily 会凭空
    # 多出只有新增用户、没有营收的空日（并让 windowDays / 月度出现窗口外的幽灵月份）。
    all_days = sorted({row["dt"] for row in hourly} | {row["dt"] for row in dws["dws_station_day"]})
    window_start = all_days[0] if all_days else str(ods_manifest.get("dataWindow", {}).get("start", ""))[:10]
    window_end = all_days[-1] if all_days else generated_at.date().isoformat()

    # 行政区利用率：该区各站小时表利用率均值（与 dws_station_day.avg_utilization 同口径）
    station_district = {sid: st["district"] for sid, st in stations.items()}
    district_util: dict[str, float] = {}
    bucket: dict[str, list[float]] = defaultdict(list)
    for row in hourly:
        district = station_district.get(row["station_id"])
        if district:
            bucket[district].append(row["utilization"])
    for district, values in bucket.items():
        district_util[district] = sum(values) / len(values)

    # ---- ADS 业务表 ----
    ads_business = build_ads_business(dws, hourly, chargers, stations, orders, district_util)
    ads_business["ads_user_rfm"] = extras.build_rfm(orders, window_end)

    # ---- ADS 旁路表（meta / quality / forecast / event） ----
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
    if forecast_handoff:
        batch, forecast_points, metrics = extras.load_forecast_handoff(forecast_handoff, generated_at)
    else:
        batch, forecast_points = extras.build_baseline_forecast(hourly, stations, generated_at)
        metrics = extras.build_baseline_metrics(hourly, stations) if batch else []

    total_revenue = sum(row["revenue_fen"] for row in ads_business["ads_daily"])
    total_energy = round(sum(row["energy_kwh"] for row in ads_business["ads_daily"]), 3)
    total_orders = sum(row["order_cnt"] for row in ads_business["ads_daily"])
    window_days = len(all_days) or 1

    meta = extras.build_meta(
        run_id=run_id,
        generated_at=generated_at,
        ods_manifest=ods_manifest,
        window_start=window_start,
        window_end=window_end,
        window_days=window_days,
        station_count=len(stations),
        charger_count=len(chargers),
        user_count=len(users),
        order_count=total_orders,
        total_revenue_fen=total_revenue,
        total_energy_kwh=total_energy,
        batch=batch,
        producer="warehouse/jobs/build_local.py",
        first_order_user_count=len({order["user_id"] for order in orders}),
    )

    # ---- 写库（幂等重建；列顺序取自 ads_schema.sql） ----
    ads_dir.mkdir(parents=True, exist_ok=True)
    db_path = ads_dir / "ads.db"

    payload: dict[str, list[dict]] = dict(ads_business)
    payload["ads_quality_table"] = quality_table
    payload["ads_quality_issue"] = quality_issue
    payload["ads_quality_meta"] = quality_meta
    payload["ads_meta"] = [{"key": k, "value": str(v)} for k, v in meta.items()]
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

    write_ads_db(db_path, payload)

    injected_total = sum(row["injected"] for row in quality_issue)
    detected_total = sum(row["detected"] for row in quality_issue)
    manifest = {
        "contractVersion": "ads-flask-v1",
        "runId": meta["runId"],
        "generatedAt": meta["generatedAt"],
        "source": "ods-handoff",
        "sourceRunId": meta["sourceRunId"],
        "dataWindow": {
            "start": meta["dataWindowStart"],
            "end": meta["dataWindowEnd"],
            "days": window_days,
        },
        "database": "ads.db",
        "tables": {
            "ads_station": len(ads_business["ads_station"]),
            "ads_charger": len(ads_business["ads_charger"]),
            "ads_daily": len(ads_business["ads_daily"]),
            "ads_station_day": len(ads_business["ads_station_day"]),
            "ads_station_hourly": len(ads_business["ads_station_hourly"]),
            "ads_user_rfm": len(ads_business["ads_user_rfm"]),
            "ads_district": len(ads_business["ads_district"]),
            "ads_quality_table": len(quality_table),
            "ads_quality_issue": len(quality_issue),
            "ads_forecast_24h": len(forecast_points),
            "ads_forecast_metric": len(metrics),
            "ads_event": len(events),
        },
        "dws": {name: len(rows) for name, rows in dws.items()},
        "dwsManifest": dws_manifest["runId"],
        "quality": {"injected": injected_total, "detected": detected_total},
        "forecast": {
            "source": batch["model_version"] if batch else "none",
            "isBaseline": bool(batch and int(batch.get("is_baseline", 0))),
        },
    }
    (ads_dir / "ads_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(
        description="ODS 交接包 -> DWS 四表 + handoff/ads/ads.db（纯标准库）"
    )
    parser.add_argument("--ods", type=Path, default=Path("handoff/ods"))
    parser.add_argument("--dws", type=Path, default=Path("handoff/dws"))
    parser.add_argument("--ads", type=Path, default=Path("handoff/ads"))
    parser.add_argument(
        "--generated-at", type=str, default=None,
        help="固定 generatedAt（ISO 8601），用于可复现构建",
    )
    parser.add_argument(
        "--forecast-handoff", type=Path, default=None,
        help="可选：读取 #5 的 handoff/forecast，整体替换 ADS 三张预测表",
    )
    args = parser.parse_args()
    fixed = (
        datetime.fromisoformat(args.generated_at).replace(tzinfo=CN_TZ)
        if args.generated_at
        else None
    )
    manifest = build(args.ods, args.dws, args.ads, fixed, args.forecast_handoff)
    print(json.dumps({"ok": True, "manifest": manifest}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
