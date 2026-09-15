-- =============================================================================
-- ADS 层表结构契约（SQLite 方言）
-- =============================================================================
-- 这是 ADS 层**唯一**的表结构真源，被三处消费，改动必须同步：
--   1. `warehouse/jobs/build_local.py`  无 Spark 环境下的同构物化（直接 executescript 本文件）
--   2. `warehouse/jobs/export_ads_db.py` HDFS Parquet → ads.db 的导出端
--   3. `tests/test_ads_schema_contract.py` 契约测试
-- 并须与 `warehouse/sql/ads_etl.sql`（Hive/SparkSQL 方言）的列集保持一致，
-- 由 `test_ads_schema_contract.py` 自动比对两侧列名。
--
-- 下游消费者：#2 Flask 用 Python 标准库 `sqlite3` **只读**打开 `handoff/ads/ads.db`；
-- #1 Web 大屏经 Flask 间接读取。见《04-SCML》§3.3、《02-TL》§3.2。
--
-- 命名与单位口径（与第一阶段 `database/schema.sql` 一致，全组冻结）：
--   * 金额一律整数**分**，列名以 `_fen` 结尾；
--   * 时间一律 ISO 8601 带 `+08:00` 偏移；
--   * 百分比一律 0–100（不是 0–1）；
--   * 布尔/开关列用 INTEGER 0/1；
--   * `dt` 为 `YYYY-MM-DD` 业务日期字符串。
--
-- 落地方式：ADS 各表在 HDFS 以 Parquet 物化（`/ev-charging/ads`），再导出为
-- SQLite 单文件 `handoff/ads/ads.db`。选 SQLite 的理由见《04-SCML》§3.3。
-- =============================================================================


-- -----------------------------------------------------------------------------
-- 0. 运行元数据
-- -----------------------------------------------------------------------------
CREATE TABLE ads_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);


-- -----------------------------------------------------------------------------
-- 1. 维度层：站点 / 桩（清洗后的当期快照）
-- -----------------------------------------------------------------------------
CREATE TABLE ads_station (
    station_id          INTEGER PRIMARY KEY,
    name                TEXT    NOT NULL,   -- 清洗后展示名（去脏文本、缺名回填）
    name_raw            TEXT    NOT NULL,   -- ODS 原始值，保留以便答辩追溯
    district            TEXT    NOT NULL,   -- 中文行政区（朝阳区/海淀区/…）
    district_raw        TEXT    NOT NULL,   -- ODS 原始值
    address             TEXT,
    longitude           REAL,               -- Q9：越界坐标「仅置空、不臆造」（#2 TL 2026-09-15 拍板），故允许为空
    latitude            REAL,
    price_fen_per_kwh   INTEGER NOT NULL,
    forecast_enabled    INTEGER NOT NULL,   -- 1=该站参与预测（刻意留 2 站为 0）
    charger_cnt         INTEGER NOT NULL,   -- 该站清洗后桩数
    fast_cnt            INTEGER NOT NULL,
    slow_cnt            INTEGER NOT NULL,
    fast_power_kw       INTEGER NOT NULL,   -- 该站快充最大额定功率
    slow_power_kw       INTEGER NOT NULL,
    idle_cnt            INTEGER NOT NULL,   -- ↓ 五状态计数，之和 = charger_cnt
    reserved_cnt        INTEGER NOT NULL,
    charging_cnt        INTEGER NOT NULL,
    fault_cnt           INTEGER NOT NULL,
    restarting_cnt      INTEGER NOT NULL,
    service_radius_km   REAL    NOT NULL,   -- 运营规划参数，非实测，口径见 ads_meta
    coord_imputed       INTEGER NOT NULL    -- 1=坐标曾被回填（Q9 命中）
);

CREATE TABLE ads_charger (
    charger_id          INTEGER PRIMARY KEY,
    station_id          INTEGER NOT NULL,
    code                TEXT    NOT NULL,
    type                TEXT    NOT NULL,   -- fast / slow
    power_kw            INTEGER NOT NULL,
    status              TEXT    NOT NULL,   -- idle/reserved/charging/fault/restarting
    charge_count        INTEGER NOT NULL,   -- 累计充电次数（桩表自带）
    total_duration_sec  INTEGER NOT NULL,   -- 累计充电时长（桩表自带）
    fault_flag          INTEGER NOT NULL    -- 1=当前处于 fault
);
CREATE INDEX idx_ads_charger_station ON ads_charger(station_id);


-- -----------------------------------------------------------------------------
-- 2. 日粒度事实：全城 / 站点
-- -----------------------------------------------------------------------------
CREATE TABLE ads_daily (
    dt               TEXT    PRIMARY KEY,   -- YYYY-MM-DD
    revenue_fen      INTEGER NOT NULL,
    energy_kwh       REAL    NOT NULL,
    order_cnt        INTEGER NOT NULL,
    new_user_cnt     INTEGER NOT NULL,      -- 首单新客口径，见 ads_meta.newUserNote
    active_user_cnt  INTEGER NOT NULL       -- 当日下单去重用户数
);

CREATE TABLE ads_station_day (
    dt               TEXT    NOT NULL,
    station_id       INTEGER NOT NULL,
    revenue_fen      INTEGER NOT NULL,
    energy_kwh       REAL    NOT NULL,
    order_cnt        INTEGER NOT NULL,
    fast_order_cnt   INTEGER NOT NULL,
    slow_order_cnt   INTEGER NOT NULL,
    avg_utilization  REAL    NOT NULL,      -- 该站当日 24 个整点利用率均值（0–100）
    peak_hour        INTEGER NOT NULL,      -- 当日 busy_count 最大的整点
    PRIMARY KEY (dt, station_id)
);


-- -----------------------------------------------------------------------------
-- 3. 小时粒度事实：站点 × 24h
-- -----------------------------------------------------------------------------
CREATE TABLE ads_station_hourly (
    dt            TEXT    NOT NULL,
    station_id    INTEGER NOT NULL,
    hour          INTEGER NOT NULL,         -- 0–23
    observed_at   TEXT    NOT NULL,         -- ISO 8601 +08:00
    pile_count    INTEGER NOT NULL,
    busy_count    INTEGER NOT NULL,         -- 已按 ≤ pile_count 裁剪（Q5）
    load_kw       REAL    NOT NULL,
    utilization   REAL    NOT NULL,         -- busy_count / pile_count × 100，0–100
    PRIMARY KEY (dt, station_id, hour)
);


-- -----------------------------------------------------------------------------
-- 4. 用户 RFM 分层（窗口聚合，非日粒度）
-- -----------------------------------------------------------------------------
CREATE TABLE ads_user_rfm (
    segment           TEXT    PRIMARY KEY,  -- 八分层，顺序见下方视图与 Python 侧常量
    user_cnt          INTEGER NOT NULL,
    avg_recency_days  REAL    NOT NULL,
    avg_frequency     REAL    NOT NULL,
    avg_monetary_fen  REAL    NOT NULL,
    revenue_fen       INTEGER NOT NULL       -- 该分层贡献营收，用于分层营收占比
);


-- -----------------------------------------------------------------------------
-- 5. 区域粒度：政府服务 / 覆盖密度 / 碳减排
-- -----------------------------------------------------------------------------
CREATE TABLE ads_district (
    district         TEXT    PRIMARY KEY,
    station_cnt      INTEGER NOT NULL,
    charger_cnt      INTEGER NOT NULL,
    population       INTEGER NOT NULL,      -- 外部参考数据，来源见 ads_meta.populationSource
    order_cnt        INTEGER NOT NULL,
    served_user_cnt  INTEGER NOT NULL,
    avg_wait_min     REAL    NOT NULL,      -- 未建模排队则恒为 0，见 ads_meta.avgWaitNote
    utilization_rate REAL    NOT NULL,      -- 0–100
    energy_kwh       REAL    NOT NULL,
    revenue_fen      INTEGER NOT NULL,
    co2_saved_kg     REAL    NOT NULL       -- = energy_kwh / 1000 × 581，系数见 ads_meta
);


-- -----------------------------------------------------------------------------
-- 6. 数据质量对账（ADS 侧按《03-PRL》§3.2 规则对 ODS 独立复算）
-- -----------------------------------------------------------------------------
CREATE TABLE ads_quality_table (
    name         TEXT    PRIMARY KEY,
    rows_before  INTEGER NOT NULL,          -- ODS 原始行数
    rows_after   INTEGER NOT NULL           -- 清洗后行数
);

CREATE TABLE ads_quality_issue (
    rule      TEXT    PRIMARY KEY,          -- R01..R10，与生成器 Q1..Q10 一一对应
    type      TEXT    NOT NULL,             -- 中文问题类型
    injected  INTEGER NOT NULL,             -- 生成器注入量（来自 injection_log.json）
    detected  INTEGER NOT NULL,             -- ADS 侧检出行数
    handled   INTEGER NOT NULL,             -- 已修正或剔除行数
    recall    REAL    NOT NULL              -- detected / injected，上限 1.0
);

CREATE TABLE ads_quality_meta (
    key    TEXT PRIMARY KEY,
    value  TEXT NOT NULL
);


-- -----------------------------------------------------------------------------
-- 7. 预测批次（#5 Spark MLlib 产出入库；未交付时为 seasonal-naive 基线）
-- -----------------------------------------------------------------------------
CREATE TABLE ads_forecast_batch (
    run_id        TEXT    PRIMARY KEY,
    model_version TEXT    NOT NULL,
    activated_at  TEXT    NOT NULL,
    source        TEXT    NOT NULL,
    horizon_h_max INTEGER NOT NULL,
    is_baseline   INTEGER NOT NULL,         -- 1=降级基线，前端显示角标
    note          TEXT
);

CREATE TABLE ads_forecast_24h (
    run_id                TEXT    NOT NULL,
    station_id            INTEGER NOT NULL,
    forecast_at           TEXT    NOT NULL,
    horizon_h             INTEGER NOT NULL,
    predicted_load_kw     REAL    NOT NULL,
    predicted_busy_count  INTEGER NOT NULL,
    predicted_idle_count  INTEGER NOT NULL,
    congestion_level      TEXT    NOT NULL, -- low / medium / high（≥80% 占用为 high）
    is_peak               INTEGER NOT NULL, -- 未来 24h 负荷最大的连续 2h
    PRIMARY KEY (run_id, station_id, horizon_h)
);

CREATE TABLE ads_forecast_metric (
    horizon_h      INTEGER PRIMARY KEY,
    mae            REAL NOT NULL,
    rmse           REAL NOT NULL,
    wape           REAL NOT NULL,           -- Σ|误差| / Σ|实际| × 100
    baseline_wape  REAL NOT NULL            -- 朴素常量基线的 WAPE，用于自证增益
);


-- -----------------------------------------------------------------------------
-- 8. 事件流（大屏实时滚动）
-- -----------------------------------------------------------------------------
CREATE TABLE ads_event (
    event_id     INTEGER PRIMARY KEY,
    event_type   TEXT NOT NULL,             -- fault / order_completed / …
    message      TEXT NOT NULL,             -- 中文可读文案（已关联站点/桩/订单）
    message_raw  TEXT,                      -- ODS 原始 message
    created_at   TEXT NOT NULL
);
CREATE INDEX idx_ads_event_created ON ads_event(created_at DESC);


-- =============================================================================
-- 附：设计文档《04-SCML》§3.3 的 7 张 ADS 表名 —— 兼容视图
-- =============================================================================
-- 设计文档列的是**面向视角的 ADS 表名**，实际物化为上面更细粒度的表（同一份数据，
-- 按站点 × 日、站点 × 小时拆开，以便契约里的 27 个端点各自只需单表扫描）。
-- 下列视图给出文档表名 → 实际物化表的映射，答辩时可直接 `SELECT * FROM ads_gov_service`
-- 复现文档口径；**不是**第二份数据，不占存储。
-- =============================================================================

CREATE VIEW ads_revenue_overview AS
SELECT dt,
       revenue_fen,
       energy_kwh,
       order_cnt,
       active_user_cnt
  FROM ads_daily;

CREATE VIEW ads_station_ranking AS
SELECT s.dt,
       s.station_id,
       t.name                AS station_name,
       t.district,
       s.revenue_fen,
       s.energy_kwh,
       s.order_cnt,
       s.avg_utilization,
       t.idle_cnt            AS idle_charger_cnt,
       RANK() OVER (PARTITION BY s.dt ORDER BY s.revenue_fen DESC) AS rank_no
  FROM ads_station_day s
  JOIN ads_station   t ON t.station_id = s.station_id;

CREATE VIEW ads_user_profile_rfm AS
SELECT segment,
       user_cnt,
       avg_recency_days,
       avg_frequency,
       avg_monetary_fen
  FROM ads_user_rfm;

CREATE VIEW ads_peak_hour AS
SELECT dt,
       station_id,
       hour,
       busy_count,
       pile_count,
       load_kw,
       utilization
  FROM ads_station_hourly;

-- 设计文档的 ads_charger_health 是「桩健康画像」，对应 ads_charger 的当期快照；
-- 文档里的「故障率 / 状态分布」是聚合口径，由调用方对该视图做 GROUP BY 得到，
-- 不在此处预聚合（预聚合会失去按站/按行政区下钻的能力）。
CREATE VIEW ads_charger_health AS
SELECT c.station_id,
       c.charger_id,
       c.code                AS charger_code,
       c.type,
       c.power_kw,
       c.status,
       c.fault_flag,
       c.charge_count        AS order_cnt,
       c.total_duration_sec  AS charge_duration_sec
  FROM ads_charger c;

CREATE VIEW ads_gov_service AS
SELECT district,
       station_cnt,
       charger_cnt,
       order_cnt,
       energy_kwh,
       revenue_fen,
       served_user_cnt,
       co2_saved_kg          AS carbon_reduction_kg
  FROM ads_district;

CREATE VIEW ads_forecast_result AS
SELECT station_id,
       forecast_at,
       horizon_h,
       predicted_load_kw,
       predicted_busy_count,
       predicted_idle_count,
       congestion_level,
       is_peak
  FROM ads_forecast_24h;
