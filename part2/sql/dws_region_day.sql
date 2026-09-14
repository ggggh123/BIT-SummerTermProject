-- ============================================================================
-- dws_region_day · 区域（行政区）× 日 汇总宽表
-- 承担：#5 PE（与 #4 SCML 对接，口径一致，遵守《04》§3.2 / §3.4）
--
-- 字段：dt, district, station_cnt, charger_cnt, order_cnt,
--       energy_kwh, revenue_fen, served_user_cnt
--
-- 口径：
--   * 仅统计 status = 'completed' 的有效订单（与 dws_station_day 一致）
--   * 金额一律整数分；电量保留 3 位小数
--   * 对账约束：SUM(revenue_fen) 必须等于 dwd_order_detail 中已完成订单金额合计
--
-- 占位符（由 run_share_tables.py 注入）：
--   {{DWD_ORDER}}  {{DIM_STATIONS}}  {{DIM_CHARGERS}}
-- ============================================================================

WITH completed AS (
    SELECT
        o.dt,
        o.station_id,
        o.order_id,
        o.user_id,
        o.energy_kwh,
        o.amount_fen,
        s.district
    FROM {{DWD_ORDER}} o
    JOIN {{DIM_STATIONS}} s
      ON o.station_id = s.station_id
    WHERE o.status = 'completed'
),
region_chargers AS (
    SELECT s.district, COUNT(c.charger_id) AS charger_cnt
    FROM {{DIM_CHARGERS}} c
    JOIN {{DIM_STATIONS}} s
      ON c.station_id = s.station_id
    GROUP BY s.district
)
SELECT
    c.dt                                   AS dt,
    c.district                             AS district,
    COUNT(DISTINCT c.station_id)           AS station_cnt,
    MAX(rc.charger_cnt)                    AS charger_cnt,
    COUNT(c.order_id)                      AS order_cnt,
    ROUND(SUM(c.energy_kwh), 3)            AS energy_kwh,
    SUM(c.amount_fen)                      AS revenue_fen,
    COUNT(DISTINCT c.user_id)              AS served_user_cnt
FROM completed c
LEFT JOIN region_chargers rc
       ON c.district = rc.district
GROUP BY c.dt, c.district
