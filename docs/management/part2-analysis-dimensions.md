# 分析维度与对比分析对照表（对老师"开发环境/要求"第 3 条）

> 维护人：#1 王浩恩（PM）｜日期：2026-09-15
> 老师要求原文：「使用 spark 进行数据清洗 数据分析，其中**分析的维度不少于 8 个**，**需要至少两个维度的数据对比分析**，使用 flask 进行 web 请求处理，获取数据，进行数据响应」
> 用法：答辩问答直接照本表念；每行都给「来源表 → 接口 → 页面」三段证据，可当场点开页面核对。
> 数据口径：**官方 PRL 清洗链路的正式规模批**（`handoff/ads/ads.db`，`runId=ads-20260915145956`，
> **7 站 / 251 桩 / 97,804 单 / 90 天**，窗口 2026-06-17→09-14）。
> 上一版填的 8 站 / 287 桩 / 112,422 单是本地 Python 物化批，与官方 PRL 链路不是一套数据，已整体替换（详见 `part2-metric-checklist.md`）。

## 1. 分析维度（共 15 条，要求 ≥8）

| # | 维度 | 粒度 | 来源表 / 字段 | Flask 接口 | 大屏位置 |
|---|---|---|---|---|---|
| 1 | 时间（日） | 每天 | `ads_daily.revenue_fen/energy_kwh/order_cnt` | `/api/enterprise/revenue-trend?days=7\|30\|90` | 企业页营收趋势、主页近 30 日 |
| 2 | 时间（月） | 自然月 | `ads_daily` 按月聚合 | `/api/enterprise/monthly` | 企业页月度经营汇总 |
| 3 | 时间（小时/时段） | 站×小时 | `ads_station_hourly.load_kw/busy_count/utilization` | `/api/overview/load-24h`、`/api/gov/peak-load`、`/api/user/peak-heatmap` | 主页 24h 负荷、政府页全城峰谷、用户页时段热力 |
| 4 | 站点 | 站×日 | `ads_station`、`ads_station_day` | `/api/overview/stations`、`/api/enterprise/station-ranking` | 主页北京地图与利用率排行 |
| 5 | 区域（行政区） | 区×日 | `ads_district.*`（覆盖率/服务/利用率/碳减排） | `/api/gov/coverage`、`/service-stats`、`/utilization`、`/carbon` | 政府页四图 |
| 6 | 用户（RFM 分层） | 用户 | `ads_user_rfm.segment/user_cnt/avg_monetary_fen` | `/api/enterprise/user-rfm` | 企业页 RFM 分层 |
| 7 | 用户（增长/活跃） | 天 | `ads_daily.new_user_cnt/active_user_cnt` | `/api/enterprise/user-growth` | 企业页用户增长曲线 |
| 8 | 充电桩（状态） | 桩快照 | `ads_charger.status`、`ads_station.{idle,reserved,charging,fault,restarting}_cnt` | `/api/overview/charger-status` | 主页桩状态环图 |
| 9 | 充电桩（类型与功率） | 站 | `ads_station.fast_cnt/slow_cnt/fast_power_kw/slow_power_kw` | `/api/station/{id}/mix` | 充电站页快慢充结构 |
| 10 | 设备健康 | 桩 | `ads_charger.fault_flag/charge_count/total_duration_sec` | `/api/station/{id}/health` | 充电站页 Top 桩与故障率 |
| 11 | 价格 | 站 | `ads_station.price_fen_per_kwh` | `/api/user/price-compare`、`/price-distance` | 用户页电价对比、距离-价格散点 |
| 12 | 交易与营收 | 站×日 | `ads_station_day.revenue_fen/order_cnt/energy_kwh` | `/api/overview/kpis`、`/api/enterprise/*` | 主页 KPI、企业页三指标 |
| 13 | 数据质量 | 规则×表 | `ads_quality_issue(rule,injected,detected)`、`ads_quality_table(rows_before,rows_after)` | `/api/quality/summary` | 主页数据质量面板 |
| 14 | 业务事件 | 事件 | `ads_event.event_type/message/created_at` | `/api/overview/events` | 主页实时事件流 |
| 15 | 预测 | 站×horizon | `ads_forecast_24h`、`ads_forecast_metric`、`ads_forecast_batch` | `/api/forecast/24h`、`/metrics`、`/recommend` | 主页未来 24h 预测曲线、用户页低拥堵推荐 |

## 2. 维度对比分析（共 5 组，要求 ≥2）

| # | 对比分析 | 对比口径 | 来源字段 | 大屏位置 |
|---|---|---|---|---|
| A | **快充 vs 慢充** | 同站两类桩的数量/功率结构与订单占比 | `ads_station.fast_cnt/slow_cnt/fast_power_kw/slow_power_kw`、`ads_station_day.fast_order_cnt/slow_order_cnt` | 充电站页「快慢充结构」；企业页快/慢单量对比 |
| B | **行政区 vs 行政区** | 各区 站点数/桩数/每万人桩数、利用率、等效碳减排横向对比 | `ads_district.station_cnt/charger_cnt/population/utilization_rate/co2_saved_kg` | 政府页「区域覆盖」「服务指标」「设施利用率」 |
| C | **站点 vs 站点** | 营收、订单、客单价、利用率、空闲桩排行 | `ads_station_day.revenue_fen/order_cnt`、`ads_station.idle_cnt` | 主页「利用率排行」、企业页「站点营收排行」 |
| D | **时段 vs 时段（峰谷）** | 同一站点 24h 内负荷峰谷与利用率的时段差异 | `ads_station_hourly.hour/load_kw/utilization` | 主页 24h 负荷曲线、政府页全城峰谷 |
| E | **用户分层 vs 分层** | RFM 八层的人数与客单价差异 | `ads_user_rfm.segment/user_cnt/avg_monetary_fen` | 企业页「用户 RFM 分层」 |

> 答辩口径建议：老师问"哪两个维度对比"，答 **A（快充/慢充）与 B（区/区）**，两者都能当场点开页面看数值，且字段在 ADS 表里可核对。

> **维度 7（用户增长）的现状提醒**：`ads_daily.new_user_cnt` 整列为 0，新增用户曲线是一条贴地零线——
> 生成器的 4,985 个用户注册时间全部早于业务窗口（`ads_meta.newUserNote`），窗口期内没有注册事件。
> 答辩被问到这条线时按此说明，或先与 #4 定下口径（改标题为「窗口内首单新客」/ 让生成器在窗口内注入注册时间）。

## 3. 与"文件存储放 Hadoop"的对应（老师第 2 条）

| 层 | HDFS 路径 | 现状（2026-09-15 已全部由官方链路产出） | 产出方式 |
|---|---|---|---|
| ODS | `/ev-charging/ods` | **104.7 MB**：`ods_orders`/`ods_telemetry`/`ods_users`/`ods_chargers`/`ods_stations`/`ods_events`/`ods_station_hourly` + `manifest.json`、`injection_log.json`、`_SUCCESS` | `part2/scml/scripts/hdfs_put_ods.sh`（正式规模，`run_id=scml-20260914`） |
| DWD | `/ev-charging/dwd` | **19.5 MB / 8 张**：`dwd_order_detail`、`dwd_station_hourly`、`dwd_telemetry_detail`、`dwd_event`、`dim_stations`、`dim_chargers`、`dim_users`、`dim_date` | **Spark on YARN** PRL 清洗（`application_1789453914196_0001`，712.83 秒） |
| DWS | `/ev-charging/dws` | **1.0 MB / 4 张**：`dws_station_day`、`dws_charger_day`、`dws_user_day`、`dws_region_day` + `manifest.json` | SparkSQL `run_dws_ads.sh`（`dws_etl.sql`） |
| ADS | `/ev-charging/ads` | **179.4 KB / 11 张 Parquet**：`ads_daily`、`ads_station`、`ads_station_day`、`ads_station_hourly`、`ads_charger`、`ads_charger_health`、`ads_district`、`ads_gov_service`、`ads_peak_hour`、`ads_station_ranking`、`ads_user_rfm` | SparkSQL `ads_etl.sql` + `export_ads_db.py` |
| ADS（本地交接库） | `handoff/ads/ads.db` | **15 张表**：Spark 侧导出的业务表 + Python 侧旁路表（`ads_event`、`ads_quality_issue`、`ads_quality_table`、`ads_quality_meta`、`ads_meta`）+ 预测三表 | `export_ads_db.py`（Spark 侧指标 + Python 侧旁路） |
| 质量批次 | `/ev-charging/quality/batches` | **961.6 MB**（每次清洗一批，含报告与隔离数据） | PRL `spark_quality_clean.py` |

> 如实声明：**DWD 及以下全部层现在都由官方 SparkSQL on YARN 链路产出，不再是本地物化**；
> 但 `ads.db` 里的数据质量面板（`ads_quality_*`）仍由 `export_ads_db.py` 的 Python 旁路复算，
> 其订单口径（112,422）与业务表（97,804）不同源，见 `part2-metric-checklist.md` 备注④。
> 小时表另有 47 个小时缺口（expected 15,120 / actual 15,073），ML 侧已按整点网格对齐处理，见 `part2/ml/MODEL_REPORT.md` §12。

## 4. 版本基线（老师第 1、4 条）

| 项 | 要求 | 实际 | 说明 |
|---|---|---|---|
| Python | 3.11 或 3.12 | **3.11.16**（`~/venvs/part2`） | PySpark 3.5 不支持 3.13；VM 系统 python3 是 3.13，**所有 PySpark/数仓命令必须用 venv 解释器** |
| PySpark / Spark | 版本不限 | **Spark 3.5.7** | `part2/scml` 与 `part2/ml` 均在同版本上运行 |
| Hadoop | 3.x | **3.4.1** | 单机伪分布式，诚实标注（非多节点集群） |
| Flask | 版本不限 | 3.1.3（+ flask-cors 6.0.5） | `server/requirements.txt` |
| NodeJS | ≥23 | **v24.15.0**（构建机） | 前端在构建机出 `dist`，演示时由 Flask 托管，**虚拟机不需要 Node** |
| Vue | Vue3 | **3.5.13** | `web/package.json`，并已声明 `engines.node >= 23` |
