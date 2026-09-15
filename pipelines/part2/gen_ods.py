#!/usr/bin/env python3
"""第二阶段 ODS 模拟数据生成器（1/10 规模参考骨架，确定性可复现）。

口径：以第一阶段 database/schema.sql 为契约；金额整数分、时间 +08:00 ISO 8601。
注入 10 类数据质量问题（Q1-Q10），写入 injection_log.json 供质量作业对账。
"""
import csv
import json
import os
import random
from datetime import datetime, timedelta, timezone

SEED = 20260914
SCALE = 0.1
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "handoff", "ods")
TZ = timezone(timedelta(hours=8))
DEMO_DAY = datetime(2026, 9, 14, 10, 0, 0, tzinfo=TZ)

N_STATIONS, N_USERS = 8, int(5000 * SCALE)
N_CHARGERS = int(300 * SCALE)
N_ORDERS = int(120000 * SCALE)
N_TELEMETRY = int(1000000 * SCALE)
N_EVENTS = int(20000 * SCALE)
DAYS = 90

STATION_SEED = [
    ("朝阳国贸中心充电站", "朝阳区", 39.9085, 116.4612),
    ("海淀中关村科技园充电站", "海淀区", 39.9836, 116.3164),
    ("丰台丽泽商务区充电站", "丰台区", 39.8586, 116.3245),
    ("通州运河商务区充电站", "通州区", 39.9026, 116.6584),
    ("大兴亦庄经开区充电站", "大兴区", 39.7956, 116.5064),
    ("朝阳望京SOHO充电站", "朝阳区", 40.0009, 116.4707),
    ("海淀西二旗软件园充电站", "海淀区", 40.0509, 116.3034),
    ("丰台科技园东区充电站", "丰台区", 39.8339, 116.2946),
]

rnd = random.Random(SEED)
injection = {f"Q{i}": {"injected": 0, "keys": []} for i in range(1, 11)}


def iso(dt):
    return dt.strftime("%Y-%m-%dT%H:%M:%S+08:00")


def dirty_time(dt):
    style = rnd.choice(["slash", "epoch", "compact"])
    if style == "slash":
        return dt.strftime("%Y/%m/%d %H:%M:%S")
    if style == "epoch":
        return str(int(dt.timestamp()))
    return dt.strftime("%Y%m%d%H%M%S")


def note(qid, key):
    injection[qid]["injected"] += 1
    if len(injection[qid]["keys"]) < 50:
        injection[qid]["keys"].append(key)


def write_csv(name, header, rows):
    with open(os.path.join(OUT, f"{name}.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)
    print(f"  {name}.csv  {len(rows)} 行")
    return {"table": name, "rows": len(rows), "path": f"handoff/ods/{name}.csv"}


def build_dimensions():
    rows = []
    for i, (name, district, lat, lng) in enumerate(STATION_SEED, start=1):
        nm, addr, la, lo = name, f"北京市{district}示例路{i}号", lat, lng
        if i == 3:
            la, lo = 91.2, 200.5
            note("Q9", f"station_id={i}")
        if i == 5:
            nm, addr = f"  {name}　", f"北京市{district}　示例路 "
            note("Q10", f"station_id={i}")
        rows.append([i, nm, addr, district, la, lo, rnd.randint(80, 160), 1, iso(DEMO_DAY)])
    return rows


def build_chargers():
    rows = []
    for cid in range(1, N_CHARGERS + 1):
        sid = (cid - 1) % N_STATIONS + 1
        fast = cid % 5 < 2
        status = rnd.choice(["idle"] * 6 + ["reserved", "charging", "fault", "restarting"])
        if cid % 37 == 0:
            status = "unknown"
            note("Q8", f"charger_id={cid}")
        rows.append([cid, sid, f"C-{sid:02d}-{cid:03d}", "fast" if fast else "slow",
                     rnd.choice([120, 60]) if fast else rnd.choice([7, 30]),
                     status, rnd.randint(0, 800), rnd.randint(0, 400000), iso(DEMO_DAY)])
    return rows


def build_users():
    rows = []
    for uid in range(1, N_USERS + 1):
        mobile = f"1{rnd.choice('3578')}{rnd.randint(10**8, 10**9 - 1)}"
        nickname = f"用户{uid:04d}"
        if uid % 41 == 0:
            mobile = f"1{rnd.randint(10**8, 10**9 - 2)}x"
            note("Q8", f"user_id={uid}")
        if uid % 53 == 0:
            nickname = ""
            note("Q1", f"user_id={uid}")
        if uid % 67 == 0:
            nickname = f"  {nickname} "
            note("Q10", f"user_id={uid}")
        reg = DEMO_DAY - timedelta(days=rnd.randint(1, DAYS), hours=rnd.randint(0, 23))
        rows.append([uid, mobile, nickname, "", rnd.randint(0, 50000),
                     "frozen" if uid % 97 == 0 else "active", iso(reg)])
    return rows


def build_orders(stations):
    rows, dup_ids = [], []
    for oid in range(1, N_ORDERS + 1):
        uid = rnd.randint(1, N_USERS)
        cid = rnd.randint(1, N_CHARGERS)
        started = DEMO_DAY - timedelta(days=rnd.randint(0, DAYS - 1), hours=rnd.randint(0, 23), minutes=rnd.randint(0, 59))
        ended = started + timedelta(minutes=rnd.randint(20, 120))
        status = rnd.choice(["completed"] * 9 + ["cancelled"])
        energy = round(rnd.uniform(20, 120) / 60 * rnd.uniform(6, 60), 2)
        price = stations[(cid - 1) % N_STATIONS][6]
        amount = int(energy * price)
        s_at, e_at = iso(started), iso(ended)

        if oid % 211 == 0:
            e_at = ""
            note("Q1", f"order_id={oid}")
        if oid % 97 == 0:
            energy = rnd.choice([-12.5, 9999])
            note("Q3", f"order_id={oid}")
        if oid % 149 == 0:
            s_at, e_at = dirty_time(started), dirty_time(ended)
            note("Q4", f"order_id={oid}")
        if oid % 173 == 0:
            s_at, e_at = e_at, s_at
            note("Q5", f"order_id={oid}")
        if oid % 127 == 0:
            amount = amount // 100
            note("Q6", f"order_id={oid}")
        if oid % 199 == 0:
            cid = N_CHARGERS + rnd.randint(1000, 9999)
            note("Q7", f"order_id={oid}")
        rows.append([oid, uid, cid, status, iso(started - timedelta(minutes=5)), s_at, e_at, energy, amount])
        if oid % 61 == 0:
            note("Q2", f"order_id={oid}")
            dup_ids.append(oid)
    rows.extend([row[:] for row in rows if row[0] in dup_ids])
    return rows, len(dup_ids)


def build_telemetry(chargers):
    # 功率必须受该桩额定功率约束（否则清洗阶段会因"功率超额定"被大量剔除）
    rated = {int(c[0]): float(c[4]) for c in chargers}
    rows = []
    for tid in range(1, N_TELEMETRY + 1):
        cid = rnd.randint(1, N_CHARGERS)
        ts = DEMO_DAY - timedelta(minutes=rnd.randint(0, DAYS * 24 * 60))
        power = round(rnd.uniform(0.3, 1.0) * rated[cid], 2)
        inc = round(rnd.uniform(0.05, 2.5), 3)
        p_at = iso(ts)
        if tid % 181 == 0:
            power = round(power * 3, 2)   # Q3：超过额定 1.2 倍，应被清洗剔除
            inc = -abs(inc)
            note("Q3", f"telemetry_id={tid}")
        if tid % 233 == 0:
            power = ""
            note("Q1", f"telemetry_id={tid}")
        if tid % 191 == 0:
            p_at = dirty_time(ts)
            note("Q4", f"telemetry_id={tid}")
        rows.append([tid, cid, p_at, power, inc, "sampling"])
        if tid % 151 == 0:
            note("Q2", f"telemetry_id={tid}")
            rows.append(list(rows[-1]))
    return rows


def build_hourly(chargers):
    rows = []
    for sid in range(1, N_STATIONS + 1):
        pile = sum(1 for c in chargers if c[1] == sid)
        for d in range(DAYS):
            day = DEMO_DAY - timedelta(days=d + 1)
            for h in range(24):
                peak = 1.0 if 8 <= h <= 21 else 0.45
                busy = max(0, min(pile, int(pile * peak * rnd.uniform(0.5, 0.95))))
                load = round(busy * rnd.uniform(20, 60), 1)
                if rnd.random() < 0.002:
                    busy = pile + rnd.randint(1, 5)
                    note("Q5", f"station_id={sid},d={d},h={h}")
                rows.append([sid, iso(day.replace(hour=h, minute=0, second=0)), pile, 120.0,
                             round(rnd.uniform(2, 32), 1), 0, busy, load])
    return rows


def build_events():
    kinds = ["order_completed", "charging_started", "fault", "recovered", "restarting"]
    rows = []
    for eid in range(1, N_EVENTS + 1):
        k = rnd.choice(kinds)
        ts = DEMO_DAY - timedelta(minutes=rnd.randint(0, DAYS * 24 * 60))
        rows.append([eid, k, "charger", rnd.randint(1, N_CHARGERS), f"{k} @ charger {rnd.randint(1, N_CHARGERS)}", iso(ts)])
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    manifest = []
    stations = build_dimensions()
    chargers = build_chargers()
    manifest.append(write_csv("stations",
        ["id", "name", "address", "district", "latitude", "longitude", "price_fen_per_kwh", "forecast_enabled", "created_at"], stations))
    manifest.append(write_csv("chargers",
        ["id", "station_id", "code", "type", "power_kw", "status", "charge_count", "total_duration_sec", "updated_at"], chargers))
    manifest.append(write_csv("users",
        ["id", "mobile", "nickname", "avatar_path", "balance_fen", "status", "registered_at"], build_users()))
    orders, dup = build_orders(stations)
    manifest.append(write_csv("orders",
        ["id", "user_id", "charger_id", "status", "reserved_at", "started_at", "ended_at", "energy_kwh", "amount_fen"], orders))
    print(f"    （Q2 重复订单行 {dup} 条）")
    manifest.append(write_csv("telemetry",
        ["id", "charger_id", "recorded_at", "power_kw", "energy_increment_kwh", "event_type"], build_telemetry(chargers)))
    manifest.append(write_csv("station_hourly_history",
        ["station_id", "observed_at", "pile_count", "rated_power_kw", "temperature_c", "is_holiday", "busy_count", "load_kw"], build_hourly(chargers)))
    manifest.append(write_csv("events",
        ["id", "event_type", "entity_type", "entity_id", "message", "created_at"], build_events()))

    with open(os.path.join(OUT, "injection_log.json"), "w", encoding="utf-8") as fh:
        json.dump({"seed": SEED, "scale": SCALE, "generated_at": iso(DEMO_DAY), "issues": injection}, fh, ensure_ascii=False, indent=2)
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump({"seed": SEED, "scale": SCALE, "tables": manifest}, fh, ensure_ascii=False, indent=2)
    print("\n注入汇总：")
    for qid, info in injection.items():
        print(f"  {qid}: {info['injected']}")
    print(f"\n输出目录：{OUT}")


if __name__ == "__main__":
    main()
