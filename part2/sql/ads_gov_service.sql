-- ============================================================================
-- ads_gov_service · 政府视角服务指标（区域覆盖 / 服务能力 / 等效碳减排）
-- 承担：#5 PE（分担表，输出给 #2 的 /api/gov/* 接口）
--
-- 字段：district, station_cnt, charger_cnt, charger_per_station, area_km2,
--       station_density_per_km2, charger_density_per_km2, order_cnt,
--       energy_kwh, revenue_fen, served_user_cnt,
--       avg_order_energy_kwh, avg_order_amount_yuan, co2_avoided_kg
--
-- 口径与来源假设：
--   * 面积取行政区公开面积近似值（km²），仅用于覆盖密度展示；
--   * 等效碳减排 = 充电电量 × 电网平均碳排放因子。因子默认 0.581 kgCO2/kWh，
--     来源假设：生态环境部公布的全国电网平均排放因子 0.5568 kgCO2/kWh
--     （2022 年值）并计入输配损耗后的常用近似值。**教学演示口径，非碳核算口径**，
--     大屏须与其它指标一样标注「项目生成的模拟数据」。
--   * 服务用户数需跨日去重，故回订单明细重算，而非对 dws_region_day 求和。
--
-- 占位符（由 run_share_tables.py 注入）：
--   {{DWS_REGION_DAY}}  {{DWD_ORDER}}  {{DIM_STATIONS}}  {{CO2_FACTOR}}
-- ============================================================================

WITH region AS (
    SELECT
        district,
        MAX(station_cnt)               AS station_cnt,
        MAX(charger_cnt)               AS charger_cnt,
        SUM(order_cnt)                 AS order_cnt,
        SUM(energy_kwh)                AS energy_kwh,
        SUM(revenue_fen)               AS revenue_fen
    FROM {{DWS_REGION_DAY}}
    GROUP BY district
),
served AS (
    SELECT
        s.district                     AS district,
        COUNT(DISTINCT o.user_id)      AS served_user_cnt
    FROM {{DWD_ORDER}} o
    JOIN {{DIM_STATIONS}} s
      ON o.station_id = s.station_id
    WHERE o.status = 'completed'
    GROUP BY s.district
),
area AS (
    SELECT * FROM VALUES
        ('朝阳', 470.8),
        ('海淀', 430.7),
        ('丰台', 306.0),
        ('通州', 906.0),
        ('大兴', 1036.0)
    AS t(district, area_km2)
)
SELECT
    r.district                                                   AS district,
    r.station_cnt                                                AS station_cnt,
    r.charger_cnt                                                AS charger_cnt,
    ROUND(r.charger_cnt / r.station_cnt, 2)                      AS charger_per_station,
    a.area_km2                                                   AS area_km2,
    ROUND(r.station_cnt / a.area_km2, 4)                         AS station_density_per_km2,
    ROUND(r.charger_cnt / a.area_km2, 4)                         AS charger_density_per_km2,
    r.order_cnt                                                  AS order_cnt,
    ROUND(r.energy_kwh, 3)                                       AS energy_kwh,
    r.revenue_fen                                                AS revenue_fen,
    MAX(sv.served_user_cnt)                                      AS served_user_cnt,
    ROUND(r.energy_kwh / r.order_cnt, 3)                         AS avg_order_energy_kwh,
    ROUND(r.revenue_fen / 100.0 / r.order_cnt, 2)                AS avg_order_amount_yuan,
    ROUND(r.energy_kwh * {{CO2_FACTOR}}, 2)                      AS co2_avoided_kg
FROM region r
LEFT JOIN served sv ON r.district = sv.district
LEFT JOIN area a    ON r.district = a.district
GROUP BY
    r.district, r.station_cnt, r.charger_cnt, a.area_km2,
    r.order_cnt, r.energy_kwh, r.revenue_fen
