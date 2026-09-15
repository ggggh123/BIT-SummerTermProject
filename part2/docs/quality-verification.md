# 第二阶段正式规模验证记录

日期：2026-09-15。本文是当前有效的正式规模验证摘要，取代此前仅使用 12,600 行小样本的阶段性说明。历史小样本证据没有删除，仍位于 `docs/evidence/2026-09-14-*`，但不得再被当作本次最终规模的结论。

## 1. 结论与范围

本机已完整跑通：#4 ODS 交接包验签 → #3 PRL Spark 清洗 → 七张 DWD → #4 DWS/ADS → Spark MLlib 训练/24 小时预测 → 事务性合并到 ADS → Flask/生产大屏。交付检查显示 ODS、DWD、DWS、ADS 全部存在且 31/31 项对账通过；后端契约、前端数据形状和静态托管也均通过。

这是一轮全量规模的**模拟运营数据**验证，并不等价于真实生产数据，也不掩盖规则审批仍待团队冻结这一管理事实。PRL 报告中的 `ready_for_team_delivery=false` 只反映策略签字状态，不是本次技术流水线失败。

## 2. ODS、PRL 与 DWD

| 字段 | 实际值 |
|---|---|
| ODS 来源批次 | `scml-20260914`（`ods-handoff`） |
| ODS 输入行数 | **1,162,576** |
| ODS 构成 | 8 站、288 桩、5,000 用户、120,000 订单、1,000,000 遥测、17,280 站点小时、20,000 事件 |
| PRL 批次 | `prl-clean-20260915T024010Z-55661` |
| YARN 应用 | `application_1789439983572_0001` |
| 环境 | Spark 3.5.7 / Python 3.10.21 / `master=yarn` |
| 处理耗时 | 351.23 秒 |
| 隔离行数 | 278,845 |
| DWD 保留行数 | **883,731** |
| DWD 写后读回 | 7/7 表均通过业务键、外键、时间、金额、物理范围和派生值断言 |

| DWD 表 | 行数 | 业务键 | 违规 |
|---|---:|---:|---:|
| `dwd_order_detail` | 102,332 | 102,332 | 0 |
| `dwd_telemetry_detail` | 743,937 | 743,937 | 0 |
| `dwd_station_hourly` | 15,073 | 15,073 | 0 |
| `dwd_event` | 17,146 | 17,146 | 0 |
| `dim_stations` | 7 | 7 | 0 |
| `dim_chargers` | 251 | 251 | 0 |
| `dim_users` | 4,985 | 4,985 | 0 |

小时覆盖检查为：理论 15,120（7 站 × 90 天 × 24 小时）、保留 15,073、缺失 47；`row_offset_ml_safe=false`。预测特征以显式时间戳构造，绝不依赖行号偏移。

## 3. Q1–Q10 对账

注入标签在检测完成后才用于对账，检测规则不会读取标签反推答案。下表为本次全量结果；FP/FN 如实保留，不将上游语义差异粉饰为“全绿”。

| 规则 | TP | FP | FN |
|---|---:|---:|---:|
| Q1 | 5,600 | 52 | 0 |
| Q2 | 9,953 | 128,817 | 1,247 |
| Q3 | 3,008 | 0 | 352 |
| Q4 | 11,200 | 0 | 0 |
| Q5 | 412 | 0 | 0 |
| Q6 | 512 | 294 | 88 |
| Q7 | 360 | 0 | 0 |
| Q8 | 16 | 0 | 0 |
| Q9 | 1 | 0 | 0 |
| Q10 | 26 | 0 | 0 |

待团队冻结的政策项包括：Q2 上游主键语义、Q5 超占用小时的隔离/裁剪口径、Q6 仅可精确证明的分/元转换、Q9 越界坐标不凭空回填，以及未开工订单的时间分区策略。它们保存在正式报告 `pending_policy_notes`，而非被静默忽略。

## 4. DWS、ADS 与预测

| 层级 | 实际结果 |
|---|---|
| DWS | station 630 行、charger 22,320 行、user 86,889 行、region 450 行 |
| ADS | 7 站、251 桩、97,804 有效订单；`ads_station_hourly=15,073` |
| ML 训练 | YARN `application_1789442046774_0001` 成功；24 个负荷模型均选择 GBT |
| 预测 | YARN `application_1789442046774_0003` 成功；5 个 forecast-enabled 站 × 24 horizon = 120 点 |
| 发布 | YARN `application_1789442046774_0004` 成功；预测指标 24 条；`is_baseline=0` |
| ADS 合并 | 事务性替换预测三表，先备份 `ads.before-ml.db`，合并回执 `ok=true` |

正式预测批次为 `ml-20260915-111604`，模型版本为 `spark-mllib-direct-20260915`。预测发布器按 horizon 逐个物化至最多“启用站点数 × 24”的轻量结果，避免把 24 个树模型的长 lineage 一次下发到低内存 executor；合并前会校验站点集合、1–24 horizon 完整性、峰值标记、占用/负荷物理边界和非负指标。

## 5. 展示与回归验证

| 检查 | 结果 |
|---|---:|
| PRL Python 回归 | 81/81 通过 |
| PRL 本地 Spark 回归 | 19/19 通过 |
| SCML 回归 | 49/49 通过 |
| ML 回归 | 7/7 通过 |
| Web Node 回归 | 36/36 通过 |
| Flask API 契约 | 55/55 通过 |
| 前端真实 API 形状 | 43/43 通过 |
| Flask 托管生产 `web/dist` | 通过 |
| ODS/DWD/DWS/ADS 交付检查 | 31/31 对账通过 |

生产构建通过 `web/.env.production` 固定 `VITE_USE_MOCK=false` 和 `VITE_API_BASE=/api`。因此由 Flask 托管的成品大屏调用真实 ADS API；mock 夹具仅保留给开发期与形状回归使用。

本轮“一键启动”还在端口 `5051` 上实测通过：`PART2_SKIP_HADOOP=1` 模式读取 `handoff/ads/ads.db`，健康接口返回 `runId=ads-20260915105500`，KPI 返回 97,804 单，预测接口返回批次 `ml-20260915-111604` 的 120 点、`isBaseline=false`。

## 6. 可核对证据

完整数据、模型和日志不提交 Git，保留在本机以下路径：

- 主证据根目录：`/home/hushengyuan/ev-part2/evidence/scml-full-20260915T023058Z-53775/`
- PRL 批次：`/home/hushengyuan/ev-part2/evidence/prl-clean-20260915T024010Z-55661/`
- DWD HDFS：`hdfs://localhost:8020/ev-charging/quality/batches/prl-clean-20260915T024010Z-55661`
- ML HDFS：`hdfs://localhost:8020/ev-charging/forecast/prl-clean-20260915T024010Z-55661/ads_forecast_24h_ml_r2`

| 文件 | SHA-256 |
|---|---|
| `quality_report.json` | `6f7bc109dcf153a4ea1465571db760ffee6a45d965917d1fbbd9dead9854f0d8` |
| `cleaning_report.json` | `9649892798a43e211cccfd34d19887e53819b9e6f4441008002dd5b684342b54` |
| `run_result.json` | `e17307bb7b301ed2de20c82197f75cd8550a50fd45211045620bbb176ae8d2cb` |
| DWD handoff manifest | `8e60c584bc5d755becb23b136a152f172b6bf060f99f8a56876053b37a47ad2f` |
| ML metrics | `eec9d247395a328aace6ed66b3a084619b6c33903038e9a48ddfd58f3dfcca0c` |
| ML forecast.db | `95c753854e16bc6a22c7ebf5f15b81234f98315346a9c56b9b15f25860cde2bf` |
| 最终 ADS `ads.db` | `ab281d6d7f71a7029de00b913a7a405065c68f0a2f3e678bd383c8c99585eaef` |
| 最终 ADS manifest | `4cdf39e86c573da407aceb537a8f78a0ba1a79230138bb8d2d9f0b96d5794aaf` |

同一事实的机器可读、适合评审/脚本复核的摘要在 [evidence/2026-09-15-formal-full.json](evidence/2026-09-15-formal-full.json)。

## 7. 尚需在目标机复验的事项

1. Ubuntu 22.04 演示机按同一份 ADS 和包执行一键启动、浏览器访问和 API 自测。
2. 如需再次生成而非展示已物化 ADS，先在目标机核对 Java/Hadoop/Spark/Python 基线及可用内存，再重跑完整 YARN 批次。
3. 团队负责人确认并冻结 Q2/Q5/Q6/Q9 等质量策略后，更新报告的 `ready_for_team_delivery` 状态；在此之前，不应把技术完成等同于策略审批完成。
