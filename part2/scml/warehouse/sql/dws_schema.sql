-- =============================================================================
-- DWS 层表结构（Hive / SparkSQL 方言，Spark 3.5.7）
-- =============================================================================
-- 设计依据：《04-SCML》§3.1、§3.2。四张「按维度 × 日」的汇总宽表，是 ADS 的唯一输入。
--
-- 加工输入：DWD 层（#3 产出，字段契约见 `contracts/handoff_contract.md`）。
-- 加工输出：本文件建表 → `dws_etl.sql` 灌数 → Parquet 落地 `/ev-charging/dws`。
--
-- 落地约定：
--   * 库名固定 `ev_charging`，与 ODS/DWD/ADS 同库；
--   * **不做 Hive 分区**：四表都是「维度 × 日」的窄表（最大 8×90 行 / 300×90 行），
--     分区收益为负（小文件 + 动态分区配置负担）。`dt` 作为普通列保留，查询带
--     `WHERE dt BETWEEN ...` 即可。ODS 之所以分区，是因为它是百万行级流水；
--   * `STORED AS PARQUET` 便于 ADS 层直接读，也便于 `export_ads_db.py` 导出。
--
-- 单位口径（全组冻结）：金额整数分（`*_fen`）、时间 ISO 8601 +08:00、百分比 0–100。
-- =============================================================================

CREATE DATABASE IF NOT EXISTS ev_charging
COMMENT '第二阶段电动汽车充电桩数仓';

USE ev_charging;


-- -----------------------------------------------------------------------------
-- DWS-1 站点 × 日
-- -----------------------------------------------------------------------------
-- 用途：站点营收/利用率排行（企业视角）、利用率趋势（政府视角）。
-- `fault_cnt` 口径：该日该站**发生故障上报的去重充电桩数**，来源
-- `dwd_telemetry_detail.event_type = 'fault'`（不是「当前处于 fault 的桩数」这种
-- 快照口径，否则历史日会被当期状态污染）。
CREATE TABLE IF NOT EXISTS dws_station_day (
    dt               STRING  COMMENT '业务日期 YYYY-MM-DD',
    station_id       INT     COMMENT '站点 ID',
    order_cnt        INT     COMMENT '当日完成订单数',
    energy_kwh       DOUBLE  COMMENT '当日充电电量',
    revenue_fen      BIGINT  COMMENT '当日营收（整数分）',
    avg_utilization  DOUBLE  COMMENT '当日 24 个整点利用率均值，0-100',
    peak_hour        INT     COMMENT '当日 busy_count 最大的整点，0-23',
    fast_order_cnt   INT     COMMENT '快充订单数',
    slow_order_cnt   INT     COMMENT '慢充订单数',
    fault_cnt        INT     COMMENT '当日故障上报的去重桩数'
)
USING PARQUET
LOCATION '/ev-charging/dws/dws_station_day'
COMMENT 'DWS：站点日汇总宽表';


-- -----------------------------------------------------------------------------
-- DWS-2 充电桩 × 日
-- -----------------------------------------------------------------------------
-- 用途：桩健康度、故障率、单桩产出（充电站视角）。
-- `charge_duration_sec` = Σ(ended_at − started_at)，单位秒；口径与桩表自带的
-- `total_duration_sec`（全生命周期累计）区分开，后者在 DWD 的 `dim_chargers`。
CREATE TABLE IF NOT EXISTS dws_charger_day (
    dt                   STRING  COMMENT '业务日期 YYYY-MM-DD',
    charger_id           INT     COMMENT '充电桩 ID',
    order_cnt            INT     COMMENT '当日完成订单数',
    energy_kwh           DOUBLE  COMMENT '当日充电电量',
    charge_duration_sec  BIGINT  COMMENT '当日累计充电时长（秒）',
    fault_flag           INT     COMMENT '当日是否有故障上报，1/0'
)
USING PARQUET
LOCATION '/ev-charging/dws/dws_charger_day'
COMMENT 'DWS：充电桩日汇总宽表';


-- -----------------------------------------------------------------------------
-- DWS-3 用户 × 日
-- -----------------------------------------------------------------------------
-- 用途：用户增长、RFM 的 Frequency/Monetary 输入（企业视角）。
-- `active_flag` 恒为 1：本表只对**当日有完成订单的用户**出行，行的存在即活跃，
-- 该列保留是为了给下游做 LEFT JOIN 时显式判断，避免依赖 NULL 语义。
CREATE TABLE IF NOT EXISTS dws_user_day (
    dt           STRING  COMMENT '业务日期 YYYY-MM-DD',
    user_id      INT     COMMENT '用户 ID',
    order_cnt    INT     COMMENT '当日完成订单数',
    energy_kwh   DOUBLE  COMMENT '当日充电电量',
    amount_fen   BIGINT  COMMENT '当日消费金额（整数分）',
    active_flag  INT     COMMENT '是否活跃，恒为 1'
)
USING PARQUET
LOCATION '/ev-charging/dws/dws_user_day'
COMMENT 'DWS：用户日汇总宽表';


-- -----------------------------------------------------------------------------
-- DWS-4 行政区 × 日
-- -----------------------------------------------------------------------------
-- 用途：区域覆盖密度、服务指标、碳减排（政府视角）。
-- **由 #5 分担**（见《04-SCML》§3.1）。本文件给出 #4 冻结的字段契约；
-- 若 #5 未产出，`dws_etl.sql` 中的等价 SELECT 可直接顶替，字段完全一致。
-- `population` 不在此表：它是不随日期变化的行政区属性，放 ADS 的 `ads_district`
-- 一次性带出，避免在 90 天 × 5 区里重复 450 遍。
CREATE TABLE IF NOT EXISTS dws_region_day (
    dt               STRING  COMMENT '业务日期 YYYY-MM-DD',
    district         STRING  COMMENT '行政区（中文，如 朝阳区）',
    station_cnt      INT     COMMENT '该区站点数',
    charger_cnt      INT     COMMENT '该区充电桩数',
    order_cnt        INT     COMMENT '当日完成订单数',
    energy_kwh       DOUBLE  COMMENT '当日充电电量',
    revenue_fen      BIGINT  COMMENT '当日营收（整数分）',
    served_user_cnt  INT     COMMENT '当日被服务去重用户数'
)
USING PARQUET
LOCATION '/ev-charging/dws/dws_region_day'
COMMENT 'DWS：行政区日汇总宽表';
