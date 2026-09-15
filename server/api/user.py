"""用户视角端点（契约 §4）。

消费者看大屏的口径：价格对比 / 距离-价格 / 空闲排行 / 时段热力。
距离以天安门（39.9087, 116.3975）为参考点，**保留 2 位小数**（前端测试按 2 位校验排序）。
"""

from __future__ import annotations

from flask import Blueprint

from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("user", __name__)


@bp.get("/user/price-compare")
def price_compare():
    rows = ads.stations()
    prices = [int(row["price_fen_per_kwh"]) for row in rows]
    city_avg = int(round(sum(prices) / len(prices))) if prices else 0
    return ok({
        "cityAvgFenPerKwh": city_avg,
        "stations": [
            {
                "stationId": row["station_id"],
                "name": row["name"],
                "priceFenPerKwh": int(row["price_fen_per_kwh"]),
            }
            for row in rows
        ],
    })


@bp.get("/user/price-distance")
def price_distance():
    rows = ads.stations()
    items = [
        {
            "stationId": row["station_id"],
            "name": row["name"],
            "distanceKm": ads.haversine_km(
                ads.TIANANMEN["lat"], ads.TIANANMEN["lng"],
                float(row["latitude"]), float(row["longitude"]),
            ),
            "priceFenPerKwh": int(row["price_fen_per_kwh"]),
            "idleCount": int(row["idle_cnt"]),
        }
        for row in rows
    ]
    items.sort(key=lambda item: item["distanceKm"])
    return ok(items)


@bp.get("/user/idle-ranking")
def idle_ranking():
    rows = ads.stations()
    items = [
        {
            "stationId": row["station_id"],
            "name": row["name"],
            "idleCount": int(row["idle_cnt"]),
            "chargerCount": int(row["charger_cnt"]),
            "idleRate": ads.pct(row["idle_cnt"], row["charger_cnt"]),
        }
        for row in rows
    ]
    items.sort(key=lambda item: item["idleCount"], reverse=True)
    return ok(items)


@bp.get("/user/peak-heatmap")
def peak_heatmap():
    day = ads.latest_hourly_day()
    rows = ads.stations()
    names = [row["name"] for row in rows]
    index_of = {row["station_id"]: index for index, row in enumerate(rows)}
    if day is None:
        return ok({"hours": [ads.hour_label(h) for h in range(24)], "names": names, "values": []})
    values = [
        [int(row["hour"]), index_of[row["station_id"]], int(row["busy_count"])]
        for row in ads.query(
            "SELECT station_id, hour, busy_count FROM ads_station_hourly WHERE dt = ? "
            "ORDER BY station_id ASC, hour ASC",
            (day,),
        )
        if row["station_id"] in index_of
    ]
    return ok({
        "hours": [ads.hour_label(h) for h in range(24)],
        "names": names,
        "values": values,
        "dt": day,
    })
