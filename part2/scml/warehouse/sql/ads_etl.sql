-- =============================================================================
-- ADS 层 ETL（SparkSQL / Spark 3.5.7）
-- =============================================================================
-- 输入：DWS 层（`/ev-charging/dws`）+ DWD 维表（`/ev-charging/dwd/dim_*`）。
--       个别指标 DWS 未承载（区域平均等待、小时表），直接读 DWD —— 见各段注释。
-- 输出：`/ev-charging/ads/*`（Parquet）。
--
-- 执行方式：
--     spark-submit warehouse/jobs/build_ads.py
-- 或：
--     spark-sql -f warehouse/sql/ads_etl.sql
--
-- ★ 职责边界（重要，先说清楚避免误解）★
-- 本文件只产出 **ADS 的 7 张业务指标表**：
--     ads_station / ads_charger / ads_daily / ads_station_day /
--     ads_station_hourly / ads_user_rfm / ads_district
--
-- 另外 **8 张表不由 Spark 产出**，由 Python 侧物化后合并进 `ads.db`：
--     ads_meta            —— 需要 ODS manifest 的 runId/seed、注入总量与一串口径说明文本
--     ads_quality_table   —— 「清洗前后行数」要从 ODS 原文复算
--     ads_quality_issue   —— R01..R10 的检出逻辑（金额单位识别、坐标范围、手机号正则、
--                            重复行判定）与 #3 的 DWD 清洗共用一套规则，写进 SQL 会两处漂移
--     ads_quality_meta    —— 同上，元数据
--     ads_forecast_batch  —— 预测批次元数据（#5 交接点）
--     ads_forecast_24h    —— seasonal-naive 降级基线（#5 未交付时顶替，见《05-PE》§8）
--     ads_forecast_metric —— 回测指标（MAE/RMSE/WAPE + 常量基线 WAPE 对照）
--     ads_event           —— 事件流文案组装（要 JOIN 站点名与订单金额）
--
-- 为什么不把质量规则也写进 SQL：`#3` 已经在 DWD 侧实现同一套规则，若 ADS 再写一遍
-- SQL 版本，两处必然漂移，反而失去「独立复算」的对账意义。规则只在
-- `warehouse/quality_audit.py` 一处实现，Spark 与 Python 各自负责自己擅长的部分。
--
-- 幂等性：全部 `INSERT OVERWRITE`，重跑结果一致。
-- 舍入口径：电量 3 位小数、利用率与各类百分比 1 位、金额整数分。
-- =============================================================================

USE ev_charging;


-- -----------------------------------------------------------------------------
-- ADS-1 站点维度快照
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_station (
    station_id        INT,
    name              STRING,
    name_raw          STRING,
    district          STRING,
    district_raw      STRING,
    address           STRING,
    longitude         DOUBLE,
    latitude          DOUBLE,
    price_fen_per_kwh INT,
    forecast_enabled  INT,
    charger_cnt       INT,
    fast_cnt          INT,
    slow_cnt          INT,
    fast_power_kw     INT,
    slow_power_kw     INT,
    idle_cnt          INT,
    reserved_cnt      INT,
    charging_cnt      INT,
    fault_cnt         INT,
    restarting_cnt    INT,
    service_radius_km DOUBLE,
    coord_imputed     INT
)
USING PARQUET
LOCATION '/ev-charging/ads/ads_station'
COMMENT 'ADS：站点维度快照（含五状态计数与快慢充结构）';

-- `service_radius_km` 口径：**运营规划参数，非实测**。按站点订单需求规模在
-- [0.8, 3.2] km 区间内折算，分母取全城订单量最大的站点。公式与 Python 侧
-- `build_local.py` 完全一致，`ads_meta.serviceRadiusNote` 里如实标注。
INSERT OVERWRITE TABLE ev_charging.ads_station
WITH ch AS (
    SELECT station_id,
           COUNT(1)                                                    AS charger_cnt,
           SUM(CASE WHEN type = 'fast' THEN 1 ELSE 0 END)              AS fast_cnt,
           SUM(CASE WHEN type = 'slow' THEN 1 ELSE 0 END)              AS slow_cnt,
           MAX(CASE WHEN type = 'fast' THEN power_kw ELSE 0 END)       AS fast_power_kw,
           MAX(CASE WHEN type = 'slow' THEN power_kw ELSE 0 END)       AS slow_power_kw,
           SUM(CASE WHEN status = 'idle'       THEN 1 ELSE 0 END)      AS idle_cnt,
           SUM(CASE WHEN status = 'reserved'   THEN 1 ELSE 0 END)      AS reserved_cnt,
           SUM(CASE WHEN status = 'charging'   THEN 1 ELSE 0 END)      AS charging_cnt,
           SUM(CASE WHEN status = 'fault'      THEN 1 ELSE 0 END)      AS fault_cnt,
           SUM(CASE WHEN status = 'restarting' THEN 1 ELSE 0 END)      AS restarting_cnt
      FROM ev_charging.dim_chargers
     GROUP BY station_id
),
tot AS (
    SELECT station_id, SUM(order_cnt) AS order_cnt
      FROM ev_charging.dws_station_day
     GROUP BY station_id
),
mx AS (
    SELECT GREATEST(MAX(order_cnt), 1) AS max_orders FROM tot
)
SELECT s.station_id,
       s.name,
       COALESCE(s.name_raw, s.name),
       s.district,
       COALESCE(s.district_raw, s.district),
       s.address,
       s.longitude,
       s.latitude,
       s.price_fen_per_kwh,
       s.forecast_enabled,
       COALESCE(ch.charger_cnt, 0),
       COALESCE(ch.fast_cnt, 0),
       COALESCE(ch.slow_cnt, 0),
       COALESCE(ch.fast_power_kw, 0),
       COALESCE(ch.slow_power_kw, 0),
       COALESCE(ch.idle_cnt, 0),
       COALESCE(ch.reserved_cnt, 0),
       COALESCE(ch.charging_cnt, 0),
       COALESCE(ch.fault_cnt, 0),
       COALESCE(ch.restarting_cnt, 0),
       ROUND(LEAST(3.2, GREATEST(0.8,
             0.8 + 2.4 * SQRT(COALESCE(t.order_cnt, 0) / mx.max_orders))), 1),
       COALESCE(s.coord_imputed, 0)
  FROM ev_charging.dim_stations s
  LEFT JOIN ch        ON ch.station_id = s.station_id
  LEFT JOIN tot t     ON t.station_id  = s.station_id
  CROSS JOIN mx;


-- -----------------------------------------------------------------------------
-- ADS-2 充电桩维度快照
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ads_charger (
    charger_id         INT,
    station_id         INT,
    code               STRING,
    type               STRING,
    power_kw           INT,
    status             STRING,
    charge_count       INT,
    total_duration_sec BIGINT,
    fault_flag         INT
)
USING PARQUET
LOCATION '/ev-charging/ads/ads_charger'
COMMENT 'ADS：充电桩维度快照';

INSERT OVERWRITE TABLE ev_charging.ads_charger
SELECT charger_id,
       station_id,
       code,
       type,
       power_kw,
       status,
       charge_count,
       total_duration_sec,
       CASE WHEN status = 'fault' THEN 1 ELSE 0 END AS fault_flag
  FROM ev_charging.dim_chargers;


-- -----------------------------------------------------------------------------
-- ADS-3 全城 × 日
-- -----------------------------------------------------------------------------
-- `new_user_cnt` = 「窗口内首次完成订单的用户数」（首单新客口径）。取 DWS 用户日表的
-- 「每个用户最早出现的那天」计数即可，不需要回 ODS。口径原因见 `ads_meta.newUserNote`。
CREATE TABLE IF NOT EXISTS ads_daily (
    dt              STRING,
    revenue_fen     BIGINT,
    energy_kwh      DOUBLE,
    order_cnt       INT,
    new_user_cnt    INT,
    active_user_cnt INT
)
USING PARQUET
LOCATION '/ev-charging/ads/ads_daily'
COMMENT 'ADS：全城日汇总';

INSERT OVERWRITE TABLE ev_charging.ads_daily
WITH s AS (
    SELECT dt,
           SUM(revenue_fen) AS revenue_fen,
           SUM(energy_kwh)  AS energy_kwh,
           SUM(order_cnt)   AS order_cnt
      FROM ev_charging.dws_station_day
     GROUP BY dt
),
act AS (
    SELECT dt, COUNT(DISTINCT user_id) AS active_user_cnt
      FROM ev_charging.dws_user_day
     GROUP BY dt
),
firstd AS (
    SELECT user_id, MIN(dt) AS first_dt
      FROM ev_charging.dws_user_day
     GROUP BY user_id
),
newu AS (
    SELECT first_dt AS dt, COUNT(1) AS new_user_cnt
      FROM firstd
     GROUP BY first_dt
)
SELECT s.dt,
       s.revenue_fen,
       ROUND(s.energy_kwh, 3)                    AS energy_kwh,
       s.order_cnt,
       COALESCE(n.new_user_cnt, 0)               AS new_user_cnt,
       COALESCE(a.active_user_cnt, 0)            AS active_user_cnt
  FROM s
  LEFT JOIN act  a ON a.dt = s.dt
  LEFT JOIN newu n ON n.dt = s.dt;


-- -----------------------------------------------------------------------------
-- ADS-4 站点 × 日
-- -----------------------------------------------------------------------------
-- 与 DWS 同构，是**契约化重命名**：字段名按前端契约固定，下游只认这张表，
-- 因此 DWS 后续增删列不会打到 Flask。
CREATE TABLE IF NOT EXISTS ads_station_day (
    dt              STRING,
    station_id      INT,
    revenue_fen     BIGINT,
    energy_kwh      DOUBLE,
    order_cnt       INT,
    fast_order_cnt  INT,
    slow_order_cnt  INT,
    avg_utilization DOUBLE,
    peak_hour       INT
)
USING PARQUET
LOCATION '/ev-charging/ads/ads_station_day'
COMMENT 'ADS：站点日汇总';

INSERT OVERWRITE TABLE ev_charging.ads_station_day
SELECT dt, station_id, revenue_fen, energy_kwh, order_cnt,
       fast_order_cnt, slow_order_cnt, avg_utilization, peak_hour
  FROM ev_charging.dws_station_day;


-- -----------------------------------------------------------------------------
-- ADS-5 站点 × 小时
-- -----------------------------------------------------------------------------
-- 不经过 DWS：DWS 四表按设计只到「日」粒度，小时明细直接取 DWD。
CREATE TABLE IF NOT EXISTS ads_station_hourly (
    dt          STRING,
    station_id  INT,
    hour        INT,
    observed_at STRING,
    pile_count  INT,
    busy_count  INT,
    load_kw     DOUBLE,
    utilization DOUBLE
)
USING PARQUET
LOCATION '/ev-charging/ads/ads_station_hourly'
COMMENT 'ADS：站点小时明细（热力图与预测输入）';

INSERT OVERWRITE TABLE ev_charging.ads_station_hourly
SELECT dt,
       station_id,
       hour,
       observed_at,
       pile_count,
       busy_count,
       ROUND(load_kw, 3)                             AS load_kw,
       ROUND(busy_count / GREATEST(pile_count, 1) * 100, 1) AS utilization
  FROM ev_charging.dwd_station_hourly;


-- -----------------------------------------------------------------------------
-- ADS-6 用户 RFM 八分层
-- -----------------------------------------------------------------------------
-- 打分口径（与 Python 侧 `build_local.py` 的 `_ads_extras.py` **逐行对齐**）：
--   R 分：recency_days **升序**，越近分越高（NTILE(5) 桶 1 → 5 分）；
--   F 分：frequency **降序**，越多分越高；
--   M 分：monetary  **降序**，越高分越高。
--   分 >= 4 记为「高」。R/F/M 的高低组合映射到 8 个中文分层。
-- `recency_days` 为**整数天**（窗口末日 +1 天 − 用户最后一次完成订单的日期），
-- 不做小时级细分：口径更直观，且两侧实现能逐行对齐。
-- ★ `ORDER BY` 必须带 `user_id` 作次级键：recency 是整数天，同一天最后一次消费的
--   用户会大量并列；不指定并列顺序，Spark 与本地会把并列用户分到不同桶，
--   分层人数与均值都会对不上，`reconcile.py` 就会报红。
CREATE TABLE IF NOT EXISTS ads_user_rfm (
    segment          STRING,
    user_cnt         INT,
    avg_recency_days DOUBLE,
    avg_frequency    DOUBLE,
    avg_monetary_fen DOUBLE,
    revenue_fen      BIGINT
)
USING PARQUET
LOCATION '/ev-charging/ads/ads_user_rfm'
COMMENT 'ADS：用户 RFM 八分层';

INSERT OVERWRITE TABLE ev_charging.ads_user_rfm
WITH per_user AS (
    SELECT u.user_id,
           SUM(u.order_cnt)  AS frequency,
           SUM(u.amount_fen) AS monetary_fen,
           DATEDIFF(a.anchor, MAX(u.dt)) AS recency_days
      FROM ev_charging.dws_user_day u
      CROSS JOIN (
            SELECT DATE_ADD(MAX(dt), 1) AS anchor
              FROM ev_charging.dws_station_day
           ) a
     GROUP BY u.user_id, a.anchor
),
scored AS (
    SELECT user_id, frequency, monetary_fen, recency_days,
           6 - NTILE(5) OVER (ORDER BY recency_days ASC,  user_id ASC) AS r_score,
           6 - NTILE(5) OVER (ORDER BY frequency    DESC, user_id ASC) AS f_score,
           6 - NTILE(5) OVER (ORDER BY monetary_fen DESC, user_id ASC) AS m_score
      FROM per_user
),
labeled AS (
    SELECT user_id, frequency, monetary_fen, recency_days,
           CASE
             WHEN r_score >= 4 AND f_score >= 4 AND m_score >= 4 THEN '重要价值客户'
             WHEN r_score <  4 AND f_score >= 4 AND m_score >= 4 THEN '重要保持客户'
             WHEN r_score >= 4 AND f_score <  4 AND m_score >= 4 THEN '重要发展客户'
             WHEN r_score <  4 AND f_score <  4 AND m_score >= 4 THEN '重要挽留客户'
             WHEN r_score >= 4 AND f_score >= 4 AND m_score <  4 THEN '一般价值客户'
             WHEN r_score <  4 AND f_score >= 4 AND m_score <  4 THEN '一般保持客户'
             WHEN r_score >= 4 AND f_score <  4 AND m_score <  4 THEN '一般发展客户'
             ELSE '一般挽留客户'
           END AS segment
      FROM scored
),
agg AS (
    SELECT segment,
           COUNT(1)                     AS user_cnt,
           ROUND(AVG(recency_days), 1)  AS avg_recency_days,
           ROUND(AVG(frequency), 1)     AS avg_frequency,
           ROUND(AVG(monetary_fen), 1)  AS avg_monetary_fen,
           SUM(monetary_fen)            AS revenue_fen
      FROM labeled
     GROUP BY segment
),
segments AS (
    SELECT '重要价值客户' AS segment, 0 AS ord UNION ALL
    SELECT '重要保持客户', 1 UNION ALL
    SELECT '重要发展客户', 2 UNION ALL
    SELECT '重要挽留客户', 3 UNION ALL
    SELECT '一般价值客户', 4 UNION ALL
    SELECT '一般保持客户', 5 UNION ALL
    SELECT '一般发展客户', 6 UNION ALL
    SELECT '一般挽留客户', 7
)
-- 用固定八行驱动 LEFT JOIN：空分层必须补 0 行，否则前端折线会缺档
SELECT g.segment,
       COALESCE(a.user_cnt, 0),
       COALESCE(a.avg_recency_days, 0.0),
       COALESCE(a.avg_frequency, 0.0),
       COALESCE(a.avg_monetary_fen, 0.0),
       COALESCE(a.revenue_fen, 0)
  FROM segments g
  LEFT JOIN agg a ON a.segment = g.segment
 ORDER BY g.ord;


-- -----------------------------------------------------------------------------
-- ADS-7 行政区汇总
-- -----------------------------------------------------------------------------
-- `population` 是行政区属性（不随日期变化），直接在此处以字面量带出，
-- 来源为北京市第七次全国人口普查常住人口，属观察性外部参考数据，
-- 已在 `ads_meta.populationSource` 标注。
-- `avg_wait_min` 必须回 DWD 订单表算（DWS 未承载等待时长）。
CREATE TABLE IF NOT EXISTS ads_district (
    district         STRING,
    station_cnt      INT,
    charger_cnt      INT,
    population       INT,
    order_cnt        INT,
    served_user_cnt  INT,
    avg_wait_min     DOUBLE,
    utilization_rate DOUBLE,
    energy_kwh       DOUBLE,
    revenue_fen      BIGINT,
    co2_saved_kg     DOUBLE
)
USING PARQUET
LOCATION '/ev-charging/ads/ads_district'
COMMENT 'ADS：行政区服务与覆盖指标';

INSERT OVERWRITE TABLE ev_charging.ads_district
WITH region AS (
    SELECT district,
           MAX(station_cnt)      AS station_cnt,
           MAX(charger_cnt)      AS charger_cnt,
           SUM(order_cnt)        AS order_cnt,
           SUM(energy_kwh)       AS energy_kwh,
           SUM(revenue_fen)      AS revenue_fen,
           SUM(served_user_cnt)  AS served_user_cnt
      FROM ev_charging.dws_region_day
     GROUP BY district
),
-- 去重用户数不能用 SUM(daily) 相加，会重复计数；`dws_user_day` 的主键是
-- (dt, user_id) 且**不含 station_id**，无法直接下钻到行政区，因此回 DWD 订单表重算。
users AS (
    SELECT s.district, COUNT(DISTINCT d.user_id) AS served_user_cnt
      FROM ev_charging.dwd_order_detail d
      JOIN ev_charging.dim_stations s ON s.station_id = d.station_id
     WHERE d.status = 'completed'
     GROUP BY s.district
),
util AS (
    SELECT s.district, ROUND(AVG(h.utilization), 1) AS utilization_rate
      FROM ev_charging.dwd_station_hourly h
      JOIN ev_charging.dim_stations s ON s.station_id = h.station_id
     GROUP BY s.district
),
wait AS (
    SELECT s.district,
           ROUND(AVG(UNIX_TIMESTAMP(d.started_at) - UNIX_TIMESTAMP(d.reserved_at)) / 60.0, 1) AS avg_wait_min
      FROM ev_charging.dwd_order_detail d
      JOIN ev_charging.dim_stations s ON s.station_id = d.station_id
     WHERE d.status = 'completed'
       AND UNIX_TIMESTAMP(d.started_at) > UNIX_TIMESTAMP(d.reserved_at)
     GROUP BY s.district
),
pop AS (
    SELECT '朝阳区' AS district, 3450000 AS population UNION ALL
    SELECT '海淀区', 3130000 UNION ALL
    SELECT '丰台区', 2010000 UNION ALL
    SELECT '通州区', 1840000 UNION ALL
    SELECT '大兴区', 1990000
)
SELECT r.district,
       r.station_cnt,
       r.charger_cnt,
       COALESCE(p.population, 1000000)                        AS population,
       r.order_cnt,
       COALESCE(u.served_user_cnt, 0)                         AS served_user_cnt,
       COALESCE(w.avg_wait_min, 0.0)                          AS avg_wait_min,
       COALESCE(t.utilization_rate, 0.0)                      AS utilization_rate,
       ROUND(r.energy_kwh, 3)                                 AS energy_kwh,
       r.revenue_fen,
       ROUND(r.energy_kwh / 1000.0 * 581.0, 2)                AS co2_saved_kg
  FROM region r
  LEFT JOIN pop p ON p.district = r.district
  LEFT JOIN users u ON u.district = r.district
  LEFT JOIN util  t ON t.district = r.district
  LEFT JOIN wait  w ON w.district = r.district;
