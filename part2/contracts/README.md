# #3 PRL 输入／输出契约与冻结边界

当前状态分为两层：

- **#4 结构已适配**：已按 SCML 提交 `e519d8d72129415af583e00ecbb1debbc93393d6` 中的 manifest、ODS 字段和 `dwd_contract.sql` 实现原生接收及七表 DWD 投影。2026-09-15 又复核到该分支最新 `13b18f0dff21b6988074cac65d7389057bcbce01`；新提交只增加正式规模交付闸门，没有改变生成器、ODS/DWD 字段或数仓 SQL。对应机器文件是 [scml-dwd-v0.1.json](scml-dwd-v0.1.json)。
- **质量策略尚未冻结**：Q2/Q3/Q5/Q6/Q9、动态价格、可选展示字段和小时缺口处理仍需 #2 TL、#4 SCML、#5 PE 评审。对应文件是 [quality-policy-v0.1.json](quality-policy-v0.1.json)。

[ods-dwd-v0.1.json](ods-dwd-v0.1.json) 是 PRL 内部严格读入／规则契约，保留了第一阶段 `database/schema.sql` 的字段语义。SCML 适配层只转换清单元数据、表名和输出 schema，不改写 #4 的原始 CSV/JSONL。

## 1. #4 原生 ODS 输入

- 契约版本为 `contractVersion=ods-dwd-v0.1`；`runId` 与 `run_id` 必须一致。
- 七类表为 `ods_users`、`ods_stations`、`ods_chargers`、`ods_orders`、`ods_telemetry`、`ods_station_hourly`、`ods_events`。
- users、stations、chargers 是快照 CSV，不按日分区；orders、telemetry、station_hourly、events 必须位于 `dt=YYYY-MM-DD` 分区目录，且分区日期在 manifest `dataWindow` 内。
- 前六类数据为 UTF-8 CSV，events 为 JSONL。CSV null 写为 `\N`；空串与 null 不混用。JSON 数字在校验中保留原词法，不先经过 float 导致精度变化。
- CSV 表头允许 #4 按自己的列顺序输出，但列名集合必须精确相等；读入时按列名归一，不按位置猜测。
- 每个数据文件的相对路径、格式、行数和 SHA-256 都必须与 manifest 一致；表级 `rows/files` 摘要也要二次对上。除无业务数据的 `_SUCCESS` 外，未列入 manifest 的文件不会被上传执行。
- `injection_log.json` 本身也受 manifest 哈希和行数保护。`issues` 中的 `(rule, table, row_id)` 必须引用已存在原始行；它们只用于检测后对账，不传入检测函数。
- 日常联调允许 `kind=prl-test-fixture`；正式 PRL 全量运行必须附加 `--require-full-input`，此时只接受 #4 原生 `kind=ods-handoff`。该标记是交付等级声明，仍要与行数、哈希、分区和注入日志校验共同使用。

## 2. 内部字段语义

| 原始字段 | 清洗后字段／处理 |
|---|---|
| `users.id`、`users.mobile` | `dim_users.user_id`、`mobile` |
| `stations.id` | `dim_stations.station_id` |
| `chargers.id`、`chargers.power_kw` | `dim_chargers.charger_id`、`power_kw` |
| `orders.id` | `dwd_order_detail.order_id` |
| orders 不含 `station_id` | 通过已通过清洗的 `chargers.station_id` 派生 |
| `telemetry.energy_increment_kwh` | #4 DWD 仍使用 `energy_increment_kwh`；PRL 内部旧投影名为 `energy_delta_kwh` |
| `station_hourly_history` | 交接表 `ods_station_hourly`，输出 `dwd_station_hourly` |
| `events.charger_fault` | DWD 中归一为 `event_type=fault` |

`_row_id` 是 #4 生成的原始行追踪键，在表内唯一。清洗后的业务表不追加非 #4 DDL 字段；`_row_id`、业务键和 PRL `run_id` 的对应放在 `audit/dwd_lineage`。

## 3. 状态、时间与金额

- 金额使用整数分；清洗计算使用 Decimal，ROUND_HALF_UP。NaN、Infinity、负数和非法文本不得默认为 0。
- 当前参考金额假设“同一模拟批次内站点单价恒定”，容差为 `max(1 分, 参考值×1%)`。只有“原值×100 与参考分值精确相等”才修正元／分混入；无价格快照的真实变价历史不适用该假设。
- 订单状态为 reserved、charging、completed、cancelled。reserved 允许无开始／结束；charging 必须有开始、可无结束；completed 必须有开始与结束。
- 无时区的可识别时间按 Asia/Shanghai 解释，带时区的时间换算为 `+08:00`；支持秒／毫秒 Unix 时间。非法日期不宽松溢出修补。
- #4 DWD 的时间列是形如 `2026-06-17T12:34:56+08:00` 的 STRING。下游 SparkSQL 计算时长时必须先 `CAST(... AS TIMESTAMP)`；直接 `UNIX_TIMESTAMP(string)` 在当前 Spark 3.5.7 上得到 null，最终聚合为 0。

## 4. 质量报告对账

- TP＝正确检出的规则／记录对，FP＝未匹配注入标签的直接命中，FN＝注入标签未检出。
- 召回率＝TP/(TP+FN)，精确率＝TP/(TP+FP)；分母为 0 时返回 null，不写成 100%。
- 同一行可同时命中多条规则。问题命中按规则计数，隔离量按唯一原始行计数；不能相加 Q1～Q10 得出删除行数。
- 维度被隔离后导致事实外键失效的记录标为 `cascade`，与原始直接问题分开报告，不计入注入对账 FP。
- 报告包含来源批次、源 manifest 哈希、字段／策略版本、Spark/YARN 应用号、行数、样本、处置、金额平衡和七表读回断言。

## 5. #4 七表 DWD 交付

| 表 | 主键 | 分区／说明 |
|---|---|---|
| `dwd_order_detail` | `order_id` | `dt=started_at` 的 +08:00 日期；未开工订单允许 NULL 分区 |
| `dwd_telemetry_detail` | `telemetry_id` | 按 `recorded_at` 的 `dt` 分区 |
| `dwd_station_hourly` | `station_id, observed_at` | 按 `observed_at` 的 `dt` 分区 |
| `dwd_event` | `event_id` | 按 `created_at` 的 `dt` 分区；DWS 故障统计来源 |
| `dim_stations` | `station_id` | 当期快照，不分区 |
| `dim_chargers` | `charger_id` | 当期快照，不分区 |
| `dim_users` | `user_id` | 当期快照，不分区 |

`dim_date` 由 #4 生成，不在 #3 七表交接包中。四张事实表物理分区，包括全空表时的可读 schema 占位；读回后 `dt` 强制按 STRING 契约验证。

## 6. DWD 包与 ADS 发布边界

- DWD 试交接包使用 `prl-dwd-package-0.1-draft` manifest；表级行数来自 Spark 写后读回，文件 bytes／SHA-256 在 HDFS 导出后计算。接收方校验文件后，仍需在 Spark 中刷新分区并重跑约束。
- `ready_for_team_delivery=false` 意味着仅允许试联调。将质量结果写入 #4 `ads.db` 时需显式 `--accept-pending`；该参数只是承认当前有待决策，不会把报告标为已冻结。
- SQLite 发布器要求 `ads_meta.sourceRunId` 与质量报告 `source_run_id` 一致，验证 4 张现有表 schema，且备份文件和回执均拒绝覆盖。
- 正式 `/ev-charging/dwd`、DWS/ADS 库和演示 `ads.db` 必须由集成流程发布；PRL 默认只写 `/ev-charging/quality/batches/<run_id>` 和独立本地交接目录。

## 7. 最小冻结清单

1. ~~#2／#4：将 DWS 时长解析修正为显式 TIMESTAMP cast，重跑对账。~~
   → **已解决（2026-09-15，#2 执行）**：核查发现该 bug 有**两处**——
   ① `part2/scml/warehouse/sql/dws_etl.sql:112-113`（充电时长）：#4 已用 `CAST(... AS TIMESTAMP)` 修好；
   ② `part2/scml/warehouse/sql/ads_etl.sql:413,417`（**平均等待时长**）：**此前被遗漏**，仍是裸 `UNIX_TIMESTAMP(字符串)`，在 Spark 3.5.7 上会静默变 0，导致大屏该指标恒为 0。已由 #2 补修为显式 `CAST`。
   另注：`part2/scml/scripts/check_scml_downstream.py:32` 有一个正则适配器会动态补 `CAST`，但其自述"不改写上游 SQL 文件"，故不能替代源文件修复。
2. ~~#4：明确 Q2 的“重复”是主键重复还是业务内容重复，并使注入器与文档一致。~~
   → **已解决（2026-09-15）**：#2 TL 拍板采纳「按业务主键判重」；注入器（`scml/data_generator/generator.py` 的 `_duplicate_payload`）、策略文件（`quality-policy` 的 `duplicate_policy`，版本升至 `0.2.0-draft`）与《03》§2.3/§3.2 已同步；检测端 `quality/rules.py` 本已按契约 `primary_key` 分组，无需改动。决策说明见 [Q2-duplicate-policy-decision.md](Q2-duplicate-policy-decision.md)。
3. #4／#3：修正 Q3 中“乘 10 仍不超额定值”的标签，并明确 Q6 已丢失小数分位的处理。
4. #2／#4／#5：决定 Q5 超占用小时是隔离还是裁剪、Q9 越界坐标是隔离还是回填，并确认 ML 如何处理小时缺口。
   → **小时缺口部分（2026-09-15）：#2 已认可 #5 的方案**——特征工程前做整点网格对齐（`part2/ml/features.py` 的 `align_hourly_grid`），并以 `part2/ml/verify_coverage.py` 作为训练门禁，`row_offset_safe=false` 时直接拒绝进入特征工程，**不允许静默按行偏移处理**。
   **但按「Q2 口径已定、整体策略文件仍为 `DRAFT_PENDING_TEAM_REVIEW`」的现状，本项暂不标记冻结**（#2 决定），待与 Q2 等条目一并走完评审流程。已在本机用带 47 个缺口的真实 DWD 验证通过。
   → **Q5 超占用小时**：**#2 于 2026-09-15 决定采纳「隔离（reject）」**，与 #3 的现有实现一致（`part2/quality/record_rules.py:149`），无需改代码。理由：占用数大于总桩数属物理不可能值，无法推断真实值，隔离符合「不臆造数据」原则；由此产生的小时缺口由 ML 侧网格对齐兜住。
   → **Q9 越界坐标**：**#2 于 2026-09-15 决定改为「仅置空坐标（repair）」**，不再整行隔离。
   理由：`stations` 是维度表，被订单/充电桩/遥测引用；整行剔除会使该站关联事实全部变成孤儿引用并触发 Q7 级联——实测使 DWD 站点由 8 降至 7、订单由 12 万降至 102,332。置空字段既不臆造坐标，也不牵连其他数据。
   「回填坐标」因无外部权威数据源（坐标本身由生成器随机产生）、等同编造数据，已排除。
   **代码改动**：`part2/quality/record_rules.py` 的 Q9 分支由 `reject` 改为置空字段 + `repair`；策略文件 `bbox_note` 同步更新；测试 `test_record_rules.py::test_coordinate_bounds` 更新为断言新行为（含「坐标确实被置空」）。改动 `reject` → `repair` 不会触发整行剔除（剔除判据见 `quality/rules.py:39` 仅含 `reject`/`deduplicate`）。
   → **断言同步（2026-09-15 晚补，实测踩坑）**：坐标被**合法置空**后，两处「必填」检查必须同步声明坐标可空，否则会把 repair 结果误判为违规、直接让 PRL 作业失败（实测连续两轮）——
   ① `quality-policy-v0.1.json` 的 `optional` 增 `"stations": ["latitude", "longitude"]`（影响 `assert_dwd` 的必填检查与 Q1 缺失判定；版本升至 `0.2.2-draft`）；
   ② `scml-dwd-v0.1.json` 的 `dim_stations.optional` 增 `latitude/longitude`（影响 `assert_scml` 的写后读回验收；该断言用的是**SCML 契约自己的** optional 列表，与 ① 是两处独立配置）。
   `assert_dwd` 的 bbox 检查（`~between`）本身对 NULL 安全（`coalesce` 后按未违规处理），无需改动；`coord_imputed` 恒为 0（`clean/scml_dwd.py:37`），不触发断言。

5. ~~#2／#1：确认 `ads_quality_*` 字段及页面对 FP/FN、级联影响和“未冻结”标志的展示口径。~~
   → **#2 已完成口径决定（2026-09-15），待 #1（UI 负责人）二次确认后实施**：大屏「数据质量」面板**全部显示**以下 6 项——
   ① 各表清洗前后行数；② Q1–Q10 各规则命中数；③ FP / FN；④ 召回率与精确率；⑤ 级联影响（cascade）单独成栏；⑥ 「策略未冻结」标志。
   **注意**：Q2 的旧数字（TP 9,953 / FP 128,817 / FN 1,247）在重跑前**不得作为成果展示口径**；重跑后应更新为新数字，或在面板上标注「Q2 口径已于 9/15 修正，待重跑更新」。

6. R04「时间格式混杂」检出恒 0 的归因与口径（缺陷 D15）。
   → **归因（2026-09-15）**：「检出 0」是 09-15 12:04 那批交付证据的结论。经逐层核查：注入端正常（`ods_orders.reserved_at` 1,200 条、`ods_telemetry.recorded_at` 10,000 条，斜杠/Unix 各半，脏值完整落盘）；PRL 检测端自 `baf3b2d`（09-15 09:55）起即按契约 policy 判定，集成机实跑得 **Q4 TP=11,200 / FP=0 / FN=0（召回 1.0）**。即 PRL 侧无需改代码，只需重跑并更新文档。
   → **吸附修复（ADS 侧独立复算）**：`scml/warehouse/jobs/{_lib,build_local,_ads_extras}.py` 此前仅在「不可解析」时计 R04，与契约「区分可解析的非标准格式与不可解析时间」不符，致大屏 `ads_quality_table` 的 R04 恒为 0。已新增 `_lib.is_nonstandard_time()`（原文非标准 ISO 8601 即计，可解析与不可解析两类都算），并在 orders / station_hourly / telemetry / events 四处对齐；**清洗解析保持宽容、剔除条件不变**（仍仅不可解析剔除，不因计数口径收紧而丢数据）。策略文件新增 `time_format_policy`（版本升至 `0.2.1-draft`），测试见 `scml/tests/test_quality_r04.py`（6 例）。
