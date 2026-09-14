# SCML Handoff Contract

契约版本：`ods-dwd-v0.1`（ODS/DWD）· `dws-v0.1`（DWS）· `ads-flask-v1`（ADS）
负责：**#4 SCML**。任何改动都要通知 #3 / #2 / #1 / #5。

---

## ODS To #3

本地路径 `handoff/ods`，HDFS 路径 `/ev-charging/ods`。

必需文件：

- `manifest.json`：seed、run id、数据窗口、每表行数与 SHA-256。
- `injection_log.json`：Q1–Q10 的逐条注入记录（见文末）+ `summary` 汇总块。
- `ods_users/part-00000.csv`
- `ods_stations/part-00000.csv`
- `ods_chargers/part-00000.csv`
- `ods_orders/dt=YYYY-MM-DD/part-00000.csv`
- `ods_telemetry/dt=YYYY-MM-DD/part-00000.csv`
- `ods_station_hourly/dt=YYYY-MM-DD/part-00000.csv`
- `ods_events/dt=YYYY-MM-DD/part-00000.jsonl`

`kind` 取值：`ods-handoff`（正式规模）/ `prl-test-fixture`（小样例）。**不得混用** ——
小样例建出来的 `ads.db` 只有几十行，拿它联调会得到满屏空图。

## ODS Partitioning

四张时序表按 Hive 风格 `dt=YYYY-MM-DD` 目录分区；三张维度快照
（`ods_users`/`ods_stations`/`ods_chargers`）保持扁平，因为它们是当期快照而不是日事实。

| Table | Partition column | Window |
|---|---|---|
| `ods_orders` | `reserved_at` | `[start_date, start_date + history_days)` |
| `ods_telemetry` | `recorded_at` | 同上 |
| `ods_station_hourly` | `observed_at` | 同上 |
| `ods_events` | `created_at` | 同上 |

#3 必须知道的两条：

1. **分区键取「注入前」的干净时间戳。** Q4 会把部分 `reserved_at` / `recorded_at`
   改写成 `yyyy/MM/dd HH:mm:ss` 或 Unix 秒，那些行仍留在原值所属的分区里。
   脏值是**要被检出的对象**，不是用来定位的依据。
2. **`dt` 不是文件内的列。** CSV/JSONL 载荷里没有 `dt` 字段，Spark 必须从目录名发现：

   ```python
   spark.read.option("basePath", f"{ODS}/ods_telemetry").csv(f"{ODS}/ods_telemetry/dt=*")
   ```

   不要在 #4 侧往载荷里加 `dt` 列 —— 那样会产生重复列。

`manifest.json` 带 `dataWindow`（`start`/`end`/`days`）与每表的 `partitions[]`
（`dt`/`rows`/`path`/`sha256`）。校验器会拒绝任何落在窗口外的分区。

ODS 保留脏数据。#3 应把 ODS 列按字符串读入，清洗后写成 DWD。

## DWD From #3

本地路径 `handoff/dwd`，HDFS 路径 `/ev-charging/dwd`。
**下面这份字段清单是 #4 冻结的接口** —— `dws_etl.sql` 直接按它写，改名即断链子。

| 表 | 字段 | 说明 |
|---|---|---|
| `dwd_order_detail` | `order_id, user_id, charger_id, station_id, status, reserved_at, started_at, ended_at, energy_kwh, amount_fen, wait_min, dt, hour` | `station_id` 由 `charger_id` 反查桩维表补上（ODS 订单表**没有**这一列）；`wait_min` = `started_at − reserved_at` 的分钟数；`dt`/`hour` 取 `started_at` 的 +08:00 日历日与整点 |
| `dwd_telemetry_detail` | `telemetry_id, charger_id, station_id, recorded_at, dt, hour, power_kw, energy_increment_kwh, event_type` | |
| `dwd_station_hourly` | `station_id, observed_at, dt, hour, pile_count, rated_power_kw, busy_count, load_kw, utilization, temperature_c, is_holiday` | `utilization = busy_count / pile_count × 100`，保留 1 位 |
| `dwd_event` | `event_id, event_type, entity_type, entity_id, message, created_at, dt, hour` | `event_type` 取值 `fault` / `order_completed`（由 ODS 的 `charger_fault` / `order_completed` 归一）；**DWS 的故障数只能从这里取**，遥测表的 `event_type` 恒为 `telemetry` |
| `dim_stations` | `station_id, name, name_raw, address, district, district_raw, latitude, longitude, price_fen_per_kwh, forecast_enabled, coord_imputed` | `district`/`name` 清洗后的展示值，`*_raw` 保留原值；`coord_imputed = 1` 表示坐标曾被 R09 回填 |
| `dim_chargers` | `charger_id, station_id, code, type, power_kw, status, charge_count, total_duration_sec` | `type` ∈ `fast`/`slow` |
| `dim_users` | `user_id, mobile, nickname, balance_fen, status, registered_at` | |
| `dim_date` | `dt, year, month, day_of_week, is_weekend` | 由 #4 生成：`bash scripts/run_dim_date.sh` |

三条口径约束：

1. **订单口径统一 `status = 'completed'`**。DWS/ADS 只统计完成单；进行中/已取消不进营收与电量。
2. **金额一律整数分**，不出现「元」。DWD 侧要完成 R06 的修正或剔除。
3. **时间一律 ISO 8601 带 `+08:00`**。不要保留 Q4 的混合格式，否则 DWS 的
   `UNIX_TIMESTAMP()` 会静默返回 NULL。

## DWS To #2 / #5

路径 `handoff/dws`、`/ev-charging/dws`。
DDL 见 `warehouse/sql/dws_schema.sql`，ETL 见 `warehouse/sql/dws_etl.sql`。

四张表（《04-SCML》§3.2）：`dws_station_day`、`dws_charger_day`、`dws_user_day`、
`dws_region_day`（后者按 §3.1 由 #5 分担，#4 已给出等价实现）。

交接包格式与 ODS 同风格：每表一个 `part-00000.csv` + `_SUCCESS`，
外加 `manifest.json`（`kind = dws-handoff`，含行数与 SHA-256）。

> **ADR：金额用 BIGINT 而不是 INT。** 4 亿分已经越过 INT 上限，不加宽会在
> 第 90 天附近静默溢出。改列类型要走契约变更流程。

## DWS/ADS To #2/#1

路径 `handoff/ads`、`/ev-charging/ads`。

ADS 的 **SQLite 契约是 `warehouse/sql/ads_schema.sql`**（15 张表 + 7 个设计文档兼容视图）。
#2 的 Flask 用 Python 标准库 `sqlite3` **只读**打开 `handoff/ads/ads.db`，
进程内不起 Spark、不连 MySQL。表名与列名以该文件为准。

`ads_etl.sql`（Hive 方言）产出其中 7 张业务指标表；另外 8 张由 Python 侧产出，
分工与理由见 `warehouse/README.md` §3。`tests/test_ads_schema_contract.py` 会断言
两侧列集一致。

## ODS 追加字段（v0.1 冻结之后）

`ods_stations.created_at`：站点投运时间。生成器刻意把各站拉开（1 号站约 108 天前、
末号站约 24 天前），用于支撑《04-SCML》§2.1 的「新站利用率爬坡」——
`_station_hourly` 的占用率会按站点已投运天数做爬坡。

`ods_station_hourly` 的三层因子模型（`data_generator/generator.py`）：

```text
占用率 = 日内形状(HOURLY_DEMAND_SHAPE)
       × 星期修正(WEEKDAY_FACTOR × WEEKEND_HOUR_FACTOR)
       × 站点规模(STATION_SCALE，与投运时间反序)
       × 新站爬坡(_ramp_factor，60 天趋于饱和)
       × 噪声(0.88–1.12)
load_kw = 额定总功率 × 占用率 × 充电效率(0.78–0.94)
```

> **ADR：为什么必须建模日内形状。** 早期版本用 `randint(0, pile_count)` 均匀抽占用，
> 24 小时曲线是平的、`peak_hour` 在 0–23 均匀散落。`load_kw` 是 #5 的**预测目标**，
> 没有日内规律就没有可学的模式，Spark MLlib 只能拟合噪声；大屏的热力图也变成一片雪花。
> 建模后 seasonal-naive 基线的 WAPE 从 67.6% 降到 17.6%。

## 注入日志格式

`injection_log.json` 的每条记录：

```json
{"rule": "Q1", "table": "ods_orders", "row_id": "orders-000000123",
 "business_key": 123, "description": "缺失值：completed 订单 ended_at 置空 / 遥测 power_kw 置空"}
```

`rule` 是生成器的 `Q1..Q10`，PRL 的 `R01..R10` 按位一一对应
（`_lib.INJECTION_TO_RULE`）。汇总块 `summary[rule]` 给出逐表条数与总数。

| Rule | Tables | Rate |
|---|---|---|
| Q1 missing value | `ods_orders`, `ods_telemetry` | 0.5% |
| Q2 duplicate record | `ods_orders`, `ods_telemetry` | 0.5%（按主键去重后可检出） |
| Q3 outlier | `ods_orders`, `ods_telemetry` | 0.3% |
| Q4 mixed timestamp format | `ods_orders`, `ods_telemetry` | 1% |
| Q5 logical contradiction | `ods_orders`, `ods_station_hourly` | 0.3% |
| Q6 wrong money unit | `ods_orders` | 0.5% |
| Q7 orphan reference | `ods_orders` | 0.3% |
| Q8 illegal field value | `ods_users`, `ods_chargers` | 0.3% |
| Q9 out-of-range coordinate | `ods_stations` | 1–2 行 |
| Q10 dirty text | `ods_stations`, `ods_users` | 0.5% |
