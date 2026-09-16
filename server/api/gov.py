"""政府视角端点（契约 §6）。

行政区口径说明：`district`（行政区）在第一阶段 `database/schema.sql` 的 `stations`
表中**并不存在**，属第二阶段扩展列（见 `docs/management/part2-design-review.md`
的字段漂移清单）；本后端由 `ads_station.district` 显式提供，取值来自生成器
`district` 列的标准化结果（英文枚举 → 中文区名）。

`/api/gov/carbon` 必须满足 `co2SavedTon = 电量/1000 × factorTonPerMwh`，
前端会自校验，因此这里用同一个算式生成，不做二次取整偏差。
"""

from __future__ import annotations

from flask import Blueprint

from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("gov", __name__)

# 行政区展示顺序（对齐 mock：朝阳 → 海淀 → 丰台 → 通州 → 大兴）
DISTRICT_ORDER = ["朝阳区", "海淀区", "丰台区", "通州区", "大兴区"]


def _districts() -> list[dict]:
    rows = ads.query(
        "SELECT district, station_cnt, charger_cnt, population, order_cnt, served_user_cnt, "
        "avg_wait_min, utilization_rate FROM ads_district"
    )
    order = {name: index for index, name in enumerate(DISTRICT_ORDER)}
    rows.sort(key=lambda row: order.get(row["district"], 99))
    return rows


@bp.get("/gov/coverage")
def coverage():
    return ok([
        {
            "district": row["district"],
            "stationCount": int(row["station_cnt"]),
            "chargerCount": int(row["charger_cnt"]),
            "population": int(row["population"]),
            "chargersPer10k": round(int(row["charger_cnt"]) / int(row["population"]) * 10000, 1)
            if int(row["population"]) else 0.0,
        }
        for row in _districts()
    ])


@bp.get("/gov/service-stats")
def service_stats():
    return ok([
        {
            "district": row["district"],
            "orderCount": int(row["order_cnt"]),
            "servedUserCnt": int(row["served_user_cnt"]),
            "avgWaitMin": float(row["avg_wait_min"]),
        }
        for row in _districts()
    ])


@bp.get("/gov/carbon")
def carbon():
    totals = ads.window_totals(ads.WINDOW_DAYS)
    bounds = ads.window_bounds(ads.WINDOW_DAYS)
    total_energy = totals["energyKwh"]
    factor = float(ads.meta("carbonFactorTonPerMwh", "") or 0.581)
    co2_ton = round(total_energy / 1000.0 * factor, 1)
    # 等效植树：1 棵树年均固碳 18 kg（口径见 ads_meta.carbonFactorNote）
    trees = int(round(total_energy / 1000.0 * factor * 1000 / 18.0))
    return ok({
        "totalEnergyKwh": total_energy,
        "co2SavedTon": co2_ton,
        "factorTonPerMwh": factor,
        "factorNote": ads.meta("carbonFactorNote", "按全国电网平均排放因子 0.581 tCO₂/MWh 折算"),
        "equivalentTrees": trees,
        # 累计口径窗口：电量/减排/植树三个指标必须能自证累计起点（2026-09-16 增补）
        "windowStart": bounds["start"],
        "windowEnd": bounds["end"],
        "windowDays": totals["days"],
    })


@bp.get("/gov/peak-load")
def peak_load():
    day = ads.latest_hourly_day()
    points = []
    if day is not None:
        # 全城负荷 = 各站该小时负荷之和（对齐 mock：全站合计而非单车）
        points = [
            {"hour": ads.hour_label(int(row["hour"])), "loadKw": round(float(row["load_kw"]), 1)}
            for row in ads.query(
                "SELECT hour, SUM(load_kw) AS load_kw FROM ads_station_hourly "
                "WHERE dt = ? GROUP BY hour ORDER BY hour ASC",
                (day,),
            )
        ]
    return ok({"points": points, "dt": day})


@bp.get("/gov/utilization")
def utilization():
    return ok([
        {
            "district": row["district"],
            "chargerCount": int(row["charger_cnt"]),
            "stationCount": int(row["station_cnt"]),
            "utilizationRate": float(row["utilization_rate"]),
        }
        for row in _districts()
    ])
