"""企业视角端点（契约 §3）。

营收趋势 / 站点排行 / 用户增长 / RFM 分层 / 月度汇总，全部由 `ads_daily`、
`ads_station_day`、`ads_user_rfm` 聚合而来；趋势的 `orderCount`、`energyKwh`
**必填**（契约明确：缺失会让前端趋势图退化成零线）。
"""

from __future__ import annotations

from flask import Blueprint, request

from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("enterprise", __name__)


@bp.get("/enterprise/revenue-trend")
def revenue_trend():
    days = ads.parse_days(request.args.get("days"), default=30)
    rows = ads.daily_window(days)
    return ok({
        "days": days,
        "points": [
            {
                "date": row["dt"],
                "revenueFen": int(row["revenue_fen"]),
                "orderCount": int(row["order_cnt"]),
                # 与同文件 station-ranking 一致：对可空数值做 0 兜底，
                # 避免单点为 NULL 时整条趋势接口 500。
                "energyKwh": round(float(row["energy_kwh"] or 0.0), 1),
            }
            for row in rows
        ],
    })


@bp.get("/enterprise/station-ranking")
def station_ranking():
    limit = ads.parse_limit(request.args.get("limit"), default=8, maximum=50)
    column = ads.sort_key("station-ranking", request.args.get("sort"), "revenue_fen")
    direction = ads.sort_dir(request.args.get("order"), "desc")
    utilization = ads.station_utilization()
    # column / direction 均取自 ads_reader.SORTABLE 白名单映射，不拼接前端原文
    sql = (
        "SELECT s.station_id, t.name, t.charger_cnt, t.idle_cnt, "
        "SUM(s.revenue_fen) AS revenue_fen, SUM(s.order_cnt) AS order_count, "
        "AVG(s.avg_utilization) AS avg_util "
        "FROM ads_station_day s JOIN ads_station t ON t.station_id = s.station_id "
        "GROUP BY s.station_id, t.name, t.charger_cnt, t.idle_cnt "
        f"ORDER BY {column} {direction} LIMIT ?"
    )
    return ok([
        {
            "stationId": row["station_id"],
            "name": row["name"],
            "utilizationRate": utilization.get(row["station_id"], 0.0),
            "revenueFen": int(row["revenue_fen"] or 0),
            "idleCount": int(row["idle_cnt"]),
            "chargerCount": int(row["charger_cnt"]),
            "orderCount": int(row["order_count"] or 0),
        }
        for row in ads.query(sql, (limit,))
    ])


@bp.get("/enterprise/user-growth")
def user_growth():
    days = ads.parse_days(request.args.get("days"), default=30)
    rows = ads.daily_window(days)
    return ok({
        "days": days,
        "points": [
            {
                "date": row["dt"],
                "newUsers": int(row["new_user_cnt"]),
                "activeUsers": int(row["active_user_cnt"]),
            }
            for row in rows
        ],
    })


@bp.get("/enterprise/user-rfm")
def user_rfm():
    rows = ads.query(
        "SELECT segment, user_cnt, revenue_fen FROM ads_user_rfm ORDER BY user_cnt DESC"
    )
    # 恢复固定的八分层展示顺序（重要价值 → 一般挽留）
    order = {name: index for index, name in enumerate(ads.RFM_SEGMENTS)}
    rows.sort(key=lambda row: order.get(row["segment"], 99))
    return ok([
        {
            "segment": row["segment"],
            "userCount": int(row["user_cnt"]),
            "revenueFen": int(row["revenue_fen"]),
        }
        for row in rows
    ])


@bp.get("/enterprise/monthly")
def monthly():
    charger_count = int(ads.scalar("SELECT SUM(charger_cnt) FROM ads_station", (), 0)) or 1
    rows = ads.query(
        "SELECT substr(dt, 1, 7) AS month, SUM(revenue_fen) AS revenue_fen, "
        "SUM(energy_kwh) AS energy_kwh, SUM(order_cnt) AS order_cnt "
        "FROM ads_daily GROUP BY month ORDER BY month"
    )
    return ok([
        {
            "month": row["month"],
            "revenueFen": int(row["revenue_fen"] or 0),
            "energyKwh": round(float(row["energy_kwh"] or 0.0), 1),
            "orderCount": int(row["order_cnt"] or 0),
            # 单桩月度产值按当前桩数摊算（口径见 ads_meta，可在 README 追溯）
            "revenuePerChargerFen": int(round(int(row["revenue_fen"] or 0) / charger_count)),
        }
        for row in rows
    ])
