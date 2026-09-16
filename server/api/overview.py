"""主页端点（契约 §2）。

KPI 与 90 天趋势共用同一窗口（`ads_reader.daily_window`），保证契约要求的
「KPI 总营收/总订单/总电量 = 90 天趋势逐日汇总」永远成立 —— 前端已按此自校验。
"""

from __future__ import annotations

from flask import Blueprint, request

from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("overview", __name__)


@bp.get("/overview/kpis")
def kpis():
    totals = ads.window_totals(ads.WINDOW_DAYS)
    bounds = ads.window_bounds(ads.WINDOW_DAYS)
    counts = ads.charger_counts()
    total = counts["total"]
    return ok({
        "totalRevenueFen": totals["revenueFen"],
        "totalEnergyKwh": totals["energyKwh"],
        "totalOrders": totals["orderCnt"],
        "chargerCount": total,
        "idleCount": counts["idle"],
        "onlineRate": ads.pct(total - counts["fault"], total),
        "windowDays": totals["days"],
        # 起止日期：前端要在「累计营收/充电量/订单」旁标注这些数从哪天算起（2026-09-16 增补）
        "windowStart": bounds["start"],
        "windowEnd": bounds["end"],
        "generatedFrom": "ADS（ads_station / ads_daily / ads_charger）",
    })


@bp.get("/overview/stations")
def stations():
    utilization = ads.station_utilization()
    orders = ads.station_orders()
    revenue = ads.station_revenue()
    return ok([
        {
            "stationId": row["station_id"],
            "name": row["name"],
            "district": row["district"],
            "longitude": row["longitude"],
            "latitude": row["latitude"],
            "chargerCount": row["charger_cnt"],
            "idleCount": row["idle_cnt"],
            "utilizationRate": utilization.get(row["station_id"], 0.0),
            "revenueFen": revenue.get(row["station_id"], 0),
            "priceFenPerKwh": row["price_fen_per_kwh"],
            "orderCount": orders.get(row["station_id"], 0),
            "forecastEnabled": bool(row["forecast_enabled"]),
        }
        for row in ads.stations()
    ])


@bp.get("/overview/charger-status")
def charger_status():
    counts = ads.charger_counts()
    # 五者之和 = 总桩数：五个计数与 charger_cnt 同源于清洗后的桩维度，天然自洽
    return ok({
        "idle": counts["idle"],
        "reserved": counts["reserved"],
        "charging": counts["charging"],
        "fault": counts["fault"],
        "restarting": counts["restarting"],
    })


@bp.get("/overview/load-24h")
def load_24h():
    day = ads.latest_hourly_day()
    if day is None:
        return ok({"points": [], "dt": None})
    station_id = request.args.get("stationId")
    params: list[object] = [day]
    sql = (
        "SELECT station_id, observed_at, load_kw FROM ads_station_hourly WHERE dt = ?"
    )
    if station_id not in (None, ""):
        params.append(ads.parse_station_id(station_id))
        sql += " AND station_id = ?"
    sql += " ORDER BY station_id ASC, hour ASC"
    points = [
        {
            "stationId": row["station_id"],
            "observedAt": ads.ensure_iso(row["observed_at"]),
            "loadKw": round(float(row["load_kw"]), 1),
        }
        for row in ads.query(sql, params)
    ]
    return ok({"points": points, "dt": day})


@bp.get("/overview/events")
def events():
    limit = ads.parse_limit(request.args.get("limit"), default=10, maximum=100)
    rows = ads.query(
        "SELECT event_type, message, created_at FROM ads_event "
        "ORDER BY created_at DESC, event_id DESC LIMIT ?",
        (limit,),
    )
    return ok([
        {
            "eventType": row["event_type"],
            "message": row["message"],
            "createdAt": ads.ensure_iso(row["created_at"]),
        }
        for row in rows
    ])
