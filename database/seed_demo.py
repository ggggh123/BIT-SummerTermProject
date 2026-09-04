"""Deterministic demo and 90-day hourly history generator.

Standard-library only. ``seed_database`` fills an already schema-applied
SQLite connection with the fixed demo dataset. The same ``(seed, cutoff)``
always yields byte-identical content.
"""
from __future__ import annotations

import hashlib
import random
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone

FIXED_SEED = 20260901
DEFAULT_CUTOFF = "2026-09-01T09:00:00+08:00"
TZ = timezone(timedelta(hours=8))

# Six fixed stations around one demo city, all forecast-enabled.
# (name, address, latitude, longitude, price_fen_per_kwh)
STATIONS = [
    ("朝阳公园充电站", "北京市朝阳区朝阳公园南路1号", 39.9337, 116.4710, 150),
    ("望京充电站", "北京市朝阳区望京街10号", 39.9960, 116.4700, 120),
    ("中关村充电站", "北京市海淀区中关村大街27号", 39.9830, 116.3150, 150),
    ("五道口充电站", "北京市海淀区成府路28号", 39.9920, 116.3370, 120),
    ("奥林匹克充电站", "北京市朝阳区北辰东路15号", 40.0000, 116.3900, 120),
    ("亦庄充电站", "北京市大兴区荣华中路8号", 39.7960, 116.5060, 150),
]

# Deterministic holidays within the 90-day history window.
HOLIDAYS = {"2026-07-01", "2026-08-01", "2026-08-15"}

CHARGERS_PER_STATION = 8
FAST_PER_STATION = 4


@dataclass
class SeedSummary:
    station_count: int = 0
    charger_count: int = 0
    user_count: int = 0
    history_count: int = 0
    completed_order_count: int = 0


def _fmt(dt: datetime) -> str:
    return dt.astimezone(TZ).isoformat()


def _admin_password_hash() -> str:
    return hashlib.sha256("123456".encode("utf-8")).hexdigest()


def seed_database(conn, seed: int = FIXED_SEED,
                  cutoff: str = DEFAULT_CUTOFF) -> SeedSummary:
    rng = random.Random(seed)
    cutoff_dt = datetime.fromisoformat(cutoff)
    summary = SeedSummary()

    conn.execute(
        "INSERT INTO admins (username, password_hash, created_at) VALUES (?, ?, ?)",
        ("admin", _admin_password_hash(), _fmt(cutoff_dt)),
    )

    # Stations.
    for name, address, lat, lon, price in STATIONS:
        conn.execute(
            "INSERT INTO stations (name, address, latitude, longitude, "
            "price_fen_per_kwh, forecast_enabled, created_at) "
            "VALUES (?, ?, ?, ?, ?, 1, ?)",
            (name, address, lat, lon, price, _fmt(cutoff_dt)),
        )
    summary.station_count = len(STATIONS)

    # Chargers: 6 stations x 8, codes 1001..1048, 4 fast + 4 slow per station.
    code = 1001
    for station_id in range(1, len(STATIONS) + 1):
        for slot in range(CHARGERS_PER_STATION):
            ctype = "fast" if slot < FAST_PER_STATION else "slow"
            power_kw = 60.0 if slot < FAST_PER_STATION else 30.0
            conn.execute(
                "INSERT INTO chargers (station_id, code, type, power_kw, "
                "status, charge_count, total_duration_sec, updated_at) "
                "VALUES (?, ?, ?, ?, 'idle', 0, 0, ?)",
                (station_id, str(code), ctype, power_kw, _fmt(cutoff_dt)),
            )
            code += 1
    summary.charger_count = 48
    # One deterministic fault charger (station 2, code 1010).
    conn.execute(
        "UPDATE chargers SET status='fault', updated_at=? WHERE code='1010'",
        (_fmt(cutoff_dt),),
    )

    # Users: 30, mobile 13800138000..13800138029.
    for i in range(30):
        mobile = f"13800138{i:03d}"
        balance = 50000 if i == 0 else rng.randint(0, 100000)
        nickname = "用户" + mobile[-4:]
        conn.execute(
            "INSERT INTO users (mobile, nickname, avatar_path, balance_fen, "
            "status, registered_at) VALUES (?, ?, '', ?, 'active', ?)",
            (mobile, nickname, balance, _fmt(cutoff_dt - timedelta(days=60))),
        )
    summary.user_count = 30

    # Charger code -> station price lookup.
    code_to_price = {}
    code = 1001
    for station_id in range(1, len(STATIONS) + 1):
        price = STATIONS[station_id - 1][4]
        for _ in range(CHARGERS_PER_STATION):
            code_to_price[str(code)] = price
            code += 1

    # 30 days of completed orders.
    completed = 0
    for day_offset in range(30):
        day = (cutoff_dt - timedelta(days=30 - day_offset)).replace(
            hour=0, minute=0, second=0, microsecond=0)
        for _ in range(rng.randint(12, 16)):
            user_id = rng.randint(1, 30)
            charger_id = rng.randint(1, 48)
            start = day + timedelta(hours=rng.randint(0, 22),
                                    minutes=rng.randint(0, 59))
            end = start + timedelta(minutes=rng.randint(30, 120))
            reserved = start - timedelta(minutes=10)
            energy = round(rng.uniform(1.0, 40.0), 3)
            amount = round(energy * code_to_price[str(1000 + charger_id)])
            conn.execute(
                "INSERT INTO orders (user_id, charger_id, status, reserved_at, "
                "started_at, ended_at, energy_kwh, amount_fen) "
                "VALUES (?, ?, 'completed', ?, ?, ?, ?, ?)",
                (user_id, charger_id, _fmt(reserved), _fmt(start),
                 _fmt(end), energy, amount),
            )
            completed += 1
    summary.completed_order_count = completed

    # 90 days x 24 hours history.
    history = 0
    rated_power_kw = 360.0
    start_hist = cutoff_dt - timedelta(days=90)
    for station_id in range(1, len(STATIONS) + 1):
        for hour_idx in range(90 * 24):
            t = start_hist + timedelta(hours=hour_idx)
            hour = t.hour
            is_weekend = t.weekday() >= 5
            is_holiday = 1 if t.strftime("%Y-%m-%d") in HOLIDAYS else 0

            base = 3.0
            if 7 <= hour < 9:
                base += 2.0
            elif 17 <= hour < 20:
                base += 3.0
            elif 0 <= hour < 6:
                base -= 1.5
            if is_weekend:
                base *= 0.9
            if is_holiday:
                base *= 0.7
            base += (station_id % 3) * 0.3

            busy = int(round(base + rng.uniform(-0.5, 0.5)))
            busy = max(0, min(8, busy))
            load = busy * (40.0 + rng.uniform(-5.0, 5.0))
            load = max(0.0, min(rated_power_kw, load))
            temperature = 20.0 + 8.0 * ((hour_idx % 24) / 24.0) + rng.uniform(-3.0, 3.0)

            conn.execute(
                "INSERT INTO station_hourly_history "
                "(station_id, observed_at, pile_count, rated_power_kw, "
                "temperature_c, is_holiday, busy_count, load_kw) "
                "VALUES (?, ?, 8, ?, ?, ?, ?, ?)",
                (station_id, _fmt(t), rated_power_kw, round(temperature, 2),
                 is_holiday, busy, round(load, 3)),
            )
            history += 1
    summary.history_count = history

    return summary
