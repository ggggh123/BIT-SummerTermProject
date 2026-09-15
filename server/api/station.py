"""充电站视角端点（契约 §5）。

`/api/station/coverage` 是列表；`/api/station/{id}/{utilization|mix|health}`
按站点 id（第一阶段 `stations.id`，1–8）动态拼接，前端只在切换站点时请求。
"""

from __future__ import annotations

from flask import Blueprint

from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("station", __name__)


@bp.get("/station/coverage")
def coverage():
    return ok([
        {
            "stationId": row["station_id"],
            "name": row["name"],
            "district": row["district"],
            "longitude": row["longitude"],
            "latitude": row["latitude"],
            "chargerCount": row["charger_cnt"],
            "serviceRadiusKm": float(row["service_radius_km"]),
        }
        for row in ads.stations()
    ])


@bp.get("/station/<int:station_id>/utilization")
def utilization(station_id: int):
    station = ads.station_or_404(station_id)
    day = ads.latest_hourly_day()
    points: list[dict] = []
    if day is not None:
        points = [
            {
                "observedAt": ads.ensure_iso(row["observed_at"]),
                "utilizationRate": float(row["utilization"]),
            }
            for row in ads.query(
                "SELECT observed_at, utilization FROM ads_station_hourly "
                "WHERE station_id = ? AND dt = ? ORDER BY hour ASC",
                (station_id, day),
            )
        ]
    return ok({"stationId": station_id, "name": station["name"], "points": points, "dt": day})


@bp.get("/station/<int:station_id>/mix")
def mix(station_id: int):
    station = ads.station_or_404(station_id)
    row = ads.query_one(
        "SELECT SUM(fast_order_cnt) AS fast_cnt, SUM(slow_order_cnt) AS slow_cnt "
        "FROM ads_station_day WHERE station_id = ?",
        (station_id,),
    ) or {}
    fast_orders = int(row.get("fast_cnt") or 0)
    slow_orders = int(row.get("slow_cnt") or 0)
    total_orders = fast_orders + slow_orders
    return ok({
        "stationId": station_id,
        "name": station["name"],
        "fastCount": int(station["fast_cnt"]),
        "slowCount": int(station["slow_cnt"]),
        # 快慢桩数之和 = 该站总桩数（契约硬约束；两者同源于清洗后的桩维度）
        "fastPowerKw": int(station["fast_power_kw"]),
        "slowPowerKw": int(station["slow_power_kw"]),
        "fastOrderShare": round(fast_orders / total_orders, 2) if total_orders else 0.0,
    })


@bp.get("/station/<int:station_id>/health")
def health(station_id: int):
    station = ads.station_or_404(station_id)
    top = [
        {
            "code": row["code"],
            "chargeCount": int(row["charge_count"]),
            "totalDurationSec": int(row["total_duration_sec"]),
            "faultFlag": int(row["fault_flag"]),
        }
        for row in ads.query(
            "SELECT code, charge_count, total_duration_sec, fault_flag FROM ads_charger "
            "WHERE station_id = ? ORDER BY charge_count DESC, charger_id ASC LIMIT 5",
            (station_id,),
        )
    ]
    return ok({
        "stationId": station_id,
        "name": station["name"],
        "faultRate": ads.pct(station["fault_cnt"], station["charger_cnt"]),
        "faultCount": int(station["fault_cnt"]),
        "topChargers": top,
    })
