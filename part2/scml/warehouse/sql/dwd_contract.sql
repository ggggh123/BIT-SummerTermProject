-- =============================================================================
-- DWD 层表结构（Hive / SparkSQL 方言）—— **#4 冻结、#3 照此产出**
-- =============================================================================
-- 这份 DDL 是 `dws_etl.sql` / `ads_etl.sql` 的输入契约：字段名、类型、分区方式
-- 一旦改动，DWS/ADS 的 SQL 会直接断链子。字段语义见 `contracts/handoff_contract.md`。
--
-- 用法（#3 侧）：
--     hdfs dfs -mkdir -p /ev-charging/dwd
--     spark-sql -f warehouse/sql/dwd_contract.sql
--     # 然后把清洗结果 INSERT OVERWRITE 进这些表，或直接写 Parquet 到对应 LOCATION
--
-- 用法（#4 自检）：在 DWD 尚未交付时，可先跑本文件建出**空表**，再跑
--     spark-submit warehouse/jobs/build_dws.py
--     spark-submit warehouse/jobs/build_ads.py
-- 这样能验证全部 INSERT 的列引用在 Spark 上可解析（表是空的，结果自然是空的）。
-- **空表验证不能替代真实数据验收** —— 数值层面对账见 `jobs/reconcile.py` 的 D 组。
--
-- 分区策略：全部 `PARTITIONED BY (dt)`。DWD 是 **百万行级** 事实表，按日分区才
-- 能避免全表扫描；DWS/ADS 是「维度 × 日」的小表，刻意不分区（收益为负）。
-- =============================================================================

CREATE DATABASE IF NOT EXISTS ev_charging
COMMENT '第二阶段电动汽车充电桩数仓';

USE ev_charging;


-- -----------------------------------------------------------------------------
-- 事实表
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS dwd_order_detail (
    order_id        INT     COMMENT '订单 ID',
    user_id         INT     COMMENT '用户 ID',
    charger_id      INT     COMMENT '充电桩 ID',
    station_id      INT     COMMENT '站点 ID（由 charger_id 反查补上，ODS 订单表没有这一列）',
    status          STRING  COMMENT 'reserved/charging/completed/cancelled',
    reserved_at     STRING  COMMENT '预约时刻 ISO 8601 +08:00',
    started_at      STRING  COMMENT '开工时刻 ISO 8601 +08:00',
    ended_at        STRING  COMMENT '结算时刻 ISO 8601 +08:00',
    energy_kwh      DOUBLE  COMMENT '充电电量，>= 0',
    amount_fen      BIGINT  COMMENT '金额，整数分',
    wait_min        DOUBLE  COMMENT '等待时长（分钟）= started_at − reserved_at',
    hour            INT     COMMENT '开工整点 0-23'
)
USING PARQUET
PARTITIONED BY (dt STRING COMMENT '业务日期 YYYY-MM-DD，取 started_at 的 +08:00 日历日')
LOCATION '/ev-charging/dwd/dwd_order_detail'
COMMENT 'DWD：订单明细（已完成 R01-R07/R10 清洗）';


CREATE TABLE IF NOT EXISTS dwd_telemetry_detail (
    telemetry_id        INT     COMMENT '遥测帧 ID',
    charger_id          INT     COMMENT '充电桩 ID',
    station_id          INT     COMMENT '站点 ID',
    recorded_at         STRING  COMMENT '采样时刻 ISO 8601 +08:00',
    hour                INT     COMMENT '采样整点 0-23',
    power_kw            DOUBLE  COMMENT '采样功率',
    energy_increment_kwh DOUBLE COMMENT '该采样区间的电量增量',
    event_type          STRING  COMMENT '事件类型（生成器恒为 telemetry）'
)
USING PARQUET
PARTITIONED BY (dt STRING COMMENT '业务日期 YYYY-MM-DD，取 recorded_at')
LOCATION '/ev-charging/dwd/dwd_telemetry_detail'
COMMENT 'DWD：遥测明细（已去重、已剔除越界功率与坏时间戳）';


CREATE TABLE IF NOT EXISTS dwd_station_hourly (
    station_id      INT     COMMENT '站点 ID',
    observed_at     STRING  COMMENT '观测时刻 ISO 8601 +08:00',
    hour            INT     COMMENT '整点 0-23',
    pile_count      INT     COMMENT '该站总桩数',
    rated_power_kw  DOUBLE  COMMENT '额定总功率',
    busy_count      INT     COMMENT '占用桩数，必须 <= pile_count（R05 已裁剪）',
    load_kw         DOUBLE  COMMENT '站点小时负荷（预测目标）',
    utilization     DOUBLE  COMMENT '利用率 = busy_count / pile_count × 100，保留 1 位',
    temperature_c   DOUBLE  COMMENT '温度',
    is_holiday      INT     COMMENT '是否周末/节假日 1/0'
)
USING PARQUET
PARTITIONED BY (dt STRING COMMENT '业务日期 YYYY-MM-DD，取 observed_at')
LOCATION '/ev-charging/dwd/dwd_station_hourly'
COMMENT 'DWD：站点小时事实（ML 训练与热力图输入）';


CREATE TABLE IF NOT EXISTS dwd_event (
    event_id    INT     COMMENT '事件 ID',
    event_type  STRING  COMMENT 'fault / order_completed（由 ODS 的 charger_fault / order_completed 归一）',
    entity_type STRING  COMMENT '关联实体类型 charger / order',
    entity_id   INT     COMMENT '关联实体 ID',
    message     STRING  COMMENT '原始文案（展示层文案由 ADS 侧组装）',
    created_at  STRING  COMMENT '发生时刻 ISO 8601 +08:00',
    hour        INT     COMMENT '发生整点 0-23'
)
USING PARQUET
PARTITIONED BY (dt STRING COMMENT '业务日期 YYYY-MM-DD，取 created_at')
LOCATION '/ev-charging/dwd/dwd_event'
COMMENT 'DWD：事件流（DWS 的 fault_cnt / fault_flag 唯一来源）';


-- -----------------------------------------------------------------------------
-- 维表（当期快照，不分区）
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS dim_stations (
    station_id        INT     COMMENT '站点 ID',
    name              STRING  COMMENT '清洗后展示名（中文，如「朝阳区3号充电站」）',
    name_raw          STRING  COMMENT 'ODS 原始名，保留以便答辩追溯',
    address           STRING  COMMENT '地址',
    district          STRING  COMMENT '中文行政区（朝阳区/海淀区/…）',
    district_raw      STRING  COMMENT 'ODS 原始值（拼音）',
    latitude          DOUBLE  COMMENT '纬度（R09 越界已按行政区质心回填）',
    longitude         DOUBLE  COMMENT '经度',
    price_fen_per_kwh INT     COMMENT '单价，整数分/度',
    forecast_enabled  INT     COMMENT '是否参与预测 1/0',
    coord_imputed     INT     COMMENT '坐标是否曾被回填 1/0'
)
USING PARQUET
LOCATION '/ev-charging/dwd/dim_stations'
COMMENT 'DWD：站点维度（R09 回填、R10 去脏文本）';


CREATE TABLE IF NOT EXISTS dim_chargers (
    charger_id         INT     COMMENT '充电桩 ID',
    station_id         INT     COMMENT '所属站点 ID',
    code               STRING  COMMENT '桩编号',
    type               STRING  COMMENT 'fast / slow',
    power_kw           INT     COMMENT '额定功率 7/30/60/120',
    status             STRING  COMMENT 'idle/reserved/charging/fault/restarting',
    charge_count       INT     COMMENT '累计充电次数',
    total_duration_sec BIGINT  COMMENT '累计充电时长（秒）'
)
USING PARQUET
LOCATION '/ev-charging/dwd/dim_chargers'
COMMENT 'DWD：充电桩维度（R08 非法状态已剔除）';


CREATE TABLE IF NOT EXISTS dim_users (
    user_id       INT     COMMENT '用户 ID',
    mobile        STRING  COMMENT '11 位手机号（R08 非法号码已剔除）',
    nickname      STRING  COMMENT '昵称（R10 已去全角与首尾空白）',
    balance_fen   BIGINT  COMMENT '余额，整数分',
    status        STRING  COMMENT 'active / frozen',
    registered_at STRING  COMMENT '注册时刻 ISO 8601 +08:00'
)
USING PARQUET
LOCATION '/ev-charging/dwd/dim_users'
COMMENT 'DWD：用户维度';


CREATE TABLE IF NOT EXISTS dim_date (
    dt          STRING  COMMENT '日期 YYYY-MM-DD',
    year        INT     COMMENT '年',
    month       INT     COMMENT '月',
    day_of_week INT     COMMENT 'ISO 星期（1=周一 … 7=周日）',
    is_weekend  INT     COMMENT '是否周末 1/0'
)
USING PARQUET
LOCATION '/ev-charging/dwd/dim_date'
COMMENT 'DWD：日期维度（由 scripts/run_dim_date.sh 生成）';
