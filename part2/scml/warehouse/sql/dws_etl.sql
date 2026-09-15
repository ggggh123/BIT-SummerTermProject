-- =============================================================================
-- DWS 层 ETL（SparkSQL / Spark 3.5.7）
-- =============================================================================
-- 输入：DWD 层（`/ev-charging/dwd`，字段契约见 `contracts/handoff_contract.md`）
-- 输出：`/ev-charging/dws/*`（Parquet），再由 `scripts/run_dws_ads.sh` 导出到
--       `handoff/dws/` 交接给 #2/#5。
--
-- 执行方式（不要逐条手工跑，用作业脚本）：
--     spark-submit warehouse/jobs/build_dws.py
-- 或：
--     spark-sql -f warehouse/sql/dws_schema.sql
--     spark-sql -f warehouse/sql/dws_etl.sql
--
-- 幂等性：全部使用 `INSERT OVERWRITE`，重跑结果一致。
--
-- 口径说明（每条都对应《04-SCML》§3.2 的字段定义）：
--   * 订单口径统一 `dwd_order_detail.status = 'completed'`；DWD 已保证金额为整数分、
--     时间可解析、无孤儿引用（这些是 #3 的清洗职责，DWS 不重复清洗）。
--   * 电量保留 3 位小数，利用率保留 1 位（与 `ads_schema.sql` 的注释口径一致）。
--   * `dt` 一律取**订单开工日** `dwd_order_detail.dt`，不是结算日。
-- =============================================================================

USE ev_charging;


-- -----------------------------------------------------------------------------
-- DWS-1 站点 × 日
-- -----------------------------------------------------------------------------
-- 驱动表是**小时表**而不是订单表：`dwd_station_hourly` 对 8 站 × 24h × 90 天是全的，
-- 用它驱动可以保住「当天无订单但有占用」的站点日；若用订单表驱动会丢行，
-- 下游利用率趋势就会出现断点。
INSERT OVERWRITE TABLE ev_charging.dws_station_day
WITH ord AS (
    SELECT d.dt                     AS dt,
           d.station_id             AS station_id,
           COUNT(1)                 AS order_cnt,
           SUM(d.energy_kwh)        AS energy_kwh,
           SUM(d.amount_fen)        AS revenue_fen,
           SUM(CASE WHEN c.type = 'fast' THEN 1 ELSE 0 END) AS fast_order_cnt,
           SUM(CASE WHEN c.type = 'slow' THEN 1 ELSE 0 END) AS slow_order_cnt
      FROM ev_charging.dwd_order_detail d
      LEFT JOIN ev_charging.dim_chargers c
             ON c.charger_id = d.charger_id
     WHERE d.status = 'completed'
     GROUP BY d.dt, d.station_id
),
hr AS (
    -- 峰值时段：busy_count 最大；**并列时取更早的整点**（同 Python 侧 build_local.py，
    -- 保证两种实现逐行一致，reconcile.py 才能做零误差比对）。
    SELECT dt, station_id, hour, busy_count, utilization
      FROM (
            SELECT dt, station_id, hour, busy_count, utilization,
                   ROW_NUMBER() OVER (
                       PARTITION BY dt, station_id
                       ORDER BY busy_count DESC, hour ASC
                   ) AS rn
              FROM ev_charging.dwd_station_hourly
           ) ranked
     WHERE rn = 1
),
hr_avg AS (
    SELECT dt, station_id, ROUND(AVG(utilization), 1) AS avg_utilization
      FROM ev_charging.dwd_station_hourly
     GROUP BY dt, station_id
),
flt AS (
    -- 故障口径：当日**发生故障上报**的去重桩数（不是「当前处于 fault」的快照口径，
    -- 否则历史日会被当期状态污染）。
    -- 来源是事件流 `dwd_event`（`event_type = 'fault'`），**不是**遥测表：
    -- 生成器的 `ods_telemetry.event_type` 恒为 `'telemetry'`，故障是独立的一条事件。
    SELECT dt, entity_id AS charger_id
      FROM ev_charging.dwd_event
     WHERE event_type = 'fault'
     GROUP BY dt, entity_id
),
flt_station AS (
    -- 注意别名：`flt` 里已经把 `entity_id` 改名为 `charger_id`，
    -- 这里只能引用 `e.charger_id`（踩过一次 UNRESOLVED_COLUMN 的坑）。
    SELECT e.dt, c.station_id, COUNT(DISTINCT e.charger_id) AS fault_cnt
      FROM flt e
      JOIN ev_charging.dim_chargers c ON c.charger_id = e.charger_id
     GROUP BY e.dt, c.station_id
)
SELECT hr.dt,
       hr.station_id,
       COALESCE(o.order_cnt, 0)                        AS order_cnt,
       ROUND(COALESCE(o.energy_kwh, 0), 3)             AS energy_kwh,
       COALESCE(o.revenue_fen, 0)                      AS revenue_fen,
       COALESCE(a.avg_utilization, 0.0)                AS avg_utilization,
       COALESCE(hr.hour, 0)                            AS peak_hour,
       COALESCE(o.fast_order_cnt, 0)                   AS fast_order_cnt,
       COALESCE(o.slow_order_cnt, 0)                   AS slow_order_cnt,
       COALESCE(f.fault_cnt, 0)                        AS fault_cnt
  FROM hr
  LEFT JOIN hr_avg      a ON a.dt = hr.dt AND a.station_id = hr.station_id
  LEFT JOIN ord         o ON o.dt = hr.dt AND o.station_id = hr.station_id
  LEFT JOIN flt_station f ON f.dt = hr.dt AND f.station_id = hr.station_id;


-- -----------------------------------------------------------------------------
-- DWS-2 充电桩 × 日
-- -----------------------------------------------------------------------------
-- 稀疏表：只对「当日有完成订单」或「当日有故障上报」的桩出行。
-- DWD 契约中的时间是 ISO 8601 +08:00 STRING。Spark 3.5 不能由
-- `UNIX_TIMESTAMP(string)` 稳定解析该格式，必须先显式转换为 TIMESTAMP。
INSERT OVERWRITE TABLE ev_charging.dws_charger_day
WITH ord AS (
    SELECT dt,
           charger_id,
           COUNT(1)                                                  AS order_cnt,
           SUM(energy_kwh)                                           AS energy_kwh,
           SUM(UNIX_TIMESTAMP(CAST(ended_at AS TIMESTAMP))
               - UNIX_TIMESTAMP(CAST(started_at AS TIMESTAMP)))       AS charge_duration_sec
      FROM ev_charging.dwd_order_detail
     WHERE status = 'completed'
     GROUP BY dt, charger_id
),
flt AS (
    -- 同 DWS-1：故障来自事件流 `dwd_event`，不是遥测表。
    SELECT dt, entity_id AS charger_id, 1 AS fault_flag
      FROM ev_charging.dwd_event
     WHERE event_type = 'fault'
     GROUP BY dt, entity_id
)
SELECT COALESCE(o.dt, f.dt)                                  AS dt,
       COALESCE(o.charger_id, f.charger_id)                  AS charger_id,
       COALESCE(o.order_cnt, 0)                              AS order_cnt,
       ROUND(COALESCE(o.energy_kwh, 0), 3)                   AS energy_kwh,
       COALESCE(o.charge_duration_sec, 0)                    AS charge_duration_sec,
       COALESCE(f.fault_flag, 0)                             AS fault_flag
  FROM ord o
  FULL OUTER JOIN flt f
    ON f.dt = o.dt AND f.charger_id = o.charger_id;


-- -----------------------------------------------------------------------------
-- DWS-3 用户 × 日
-- -----------------------------------------------------------------------------
INSERT OVERWRITE TABLE ev_charging.dws_user_day
SELECT dt,
       user_id,
       COUNT(1)         AS order_cnt,
       ROUND(SUM(energy_kwh), 3) AS energy_kwh,
       SUM(amount_fen)  AS amount_fen,
       1                AS active_flag
  FROM ev_charging.dwd_order_detail
 WHERE status = 'completed'
 GROUP BY dt, user_id;


-- -----------------------------------------------------------------------------
-- DWS-4 行政区 × 日   （《04-SCML》§3.1 标注由 #5 分担，此处给出等价实现）
-- -----------------------------------------------------------------------------
-- `station_cnt` / `charger_cnt` 取**当期维度快照**，不分日：行政区内的站点与桩数量
-- 在这 90 天里没有增减（生成器不产出站点变更事件）。这是如实的口径，
-- 已在 `handoff_contract.md` 与 `ads_meta` 中标注。
INSERT OVERWRITE TABLE ev_charging.dws_region_day
WITH st AS (
    SELECT district,
           COUNT(DISTINCT station_id) AS station_cnt
      FROM ev_charging.dim_stations
     GROUP BY district
),
ch AS (
    SELECT s.district,
           COUNT(DISTINCT c.charger_id) AS charger_cnt
      FROM ev_charging.dim_chargers c
      JOIN ev_charging.dim_stations s ON s.station_id = c.station_id
     GROUP BY s.district
),
day AS (
    SELECT s.district                  AS district,
           d.dt                        AS dt,
           COUNT(1)                    AS order_cnt,
           SUM(d.energy_kwh)           AS energy_kwh,
           SUM(d.amount_fen)           AS revenue_fen,
           COUNT(DISTINCT d.user_id)   AS served_user_cnt
      FROM ev_charging.dwd_order_detail d
      JOIN ev_charging.dim_stations s ON s.station_id = d.station_id
     WHERE d.status = 'completed'
     GROUP BY s.district, d.dt
)
SELECT day.dt,
       day.district,
       COALESCE(st.station_cnt, 0)  AS station_cnt,
       COALESCE(ch.charger_cnt, 0)  AS charger_cnt,
       day.order_cnt                AS order_cnt,
       ROUND(day.energy_kwh, 3)     AS energy_kwh,
       day.revenue_fen              AS revenue_fen,
       day.served_user_cnt          AS served_user_cnt
  FROM day
  LEFT JOIN st ON st.district = day.district
  LEFT JOIN ch ON ch.district = day.district;
