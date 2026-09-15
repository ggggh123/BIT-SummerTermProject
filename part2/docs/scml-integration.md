# #3 PRL ↔ #4 SCML 联调与交接说明

日期：2026-09-15。本文记录 #3 对 #4 现有交接包、DWD schema、DWS/ADS SQL 和 SQLite 展示库的实际适配结果。它是集成证据，不是对待冻结策略的默认批准。

## 1. 联调基线

| 项目 | 基线 |
|---|---|
| #3 分支起点 | `origin/dev@1bd4b89f4049b3b735fd2bc5d29e40092cc12bba` |
| #4 SCML 源提交 | `e519d8d72129415af583e00ecbb1debbc93393d6` |
| #4 最新复核提交 | `13b18f0dff21b6988074cac65d7389057bcbce01`；仅新增正式规模检查，生成器／契约／SQL 与上述源提交一致 |
| 第二阶段集成分支 | `eced236e444e8cdf311f49be22c1eb09ff17bc62` |
| #4 输入契约 | `ods-dwd-v0.1` |
| #3 输出轮廓 | `scml-dwd-v0.1` |
| 本机运行时 | Spark 3.5.7，Hadoop 3.2.1，Java 8u504，Python 3.10.21 |
| 真实调度方式 | Spark `master=yarn`，HDFS `hdfs://localhost:8020` |
| 操作系统 | Ubuntu 25.04 开发例外；Ubuntu 22.04 尚未复验 |

远端 `feat/part2-integration@eced236` 的 `part2/plans` 规定 JDK 17 + Hadoop 3.4.1，并在 Ubuntu 25.04 使用 Python 3.11 虚拟环境；这与本机先前明确要求的“全局安装、不用 venv”以及当前已验证版本不一致。该分支自己的跨机报告也已确认一键脚本写死用户、路径、Python 和 `curl` 会在另一台机器失效。PRL 业务代码未使用 Java 8 私有 API，但当前 `ev-part2` 运行入口与证据只覆盖上表版本。集成前必须由 #2 冻结唯一环境基线并复跑，同一台机器不应同时启动两套 HDFS/YARN 守护进程。

测试 ODS 由 #4 原生 `data_generator/generator.py` 生成，配置为 8 站、48 桩、100 用户、3,000 订单、8,000 遥测、1,344 小时记录、100 事件、7 日窗口，总计 12,600 行。manifest 仍标记 `kind=prl-test-fixture`，因此不得将该结果宣传为全量或真实运营数据。

集成分支另有 #4 正式规模 ADS／页面证据（112,422 单、100 万遥测、90 天），但当前没有可供本机验签的 ODS 原包，且该 ADS 是 #4 旁路清洗结果，不是本节记录的 #3 Spark 清洗产物。因此两类证据保持分列，不做“已经跑完 #3 全量”的推断。

## 2. 接收器做了什么

`part2.common.scml_handoff` 将 #4 manifest 规范成 PRL 内部读取描述，但不修改原包。校验包括：

1. `contractVersion`、`runId/run_id`、seed 和 `dataWindow` 一致性。
2. 快照表／日分区表的目录形式，真实日历日期以及分区是否位于数据窗口。
3. 每文件格式、表头、行数和 SHA-256，及每表 `rows/files` 摘要。
4. 所有原始 `_row_id` 唯一；注入日志的规则、表和原始行必须真实存在。
5. 只容许 manifest 列出的数据文件以及空 `_SUCCESS` 标记，避免 Spark 读到未受哈希保护的额外 CSV/JSONL。

原生接收命令：

```bash
ev-part2 python -m part2.scripts.verify_handoff /absolute/path/to/ods-handoff
```

正式全量验收使用：

```bash
ev-part2 python -m part2.scripts.verify_handoff \
  /absolute/path/to/ods-handoff --require-full
```

这会拒绝 `prl-test-fixture`，只接受 #4 原生 `kind=ods-handoff`；随后仍逐文件完成哈希、行数、字段、分区和注入引用校验。

指定 `--output <new-input-spec.json>` 会写出一份新的内部读取描述；输出文件必须不存在，不会覆盖原 manifest。

## 3. ODS → DWD 运行

```bash
ev-part2 start
bash part2/scripts/run_pipeline.sh /absolute/path/to/ods-handoff \
  --require-full-input --accept-draft
ev-part2 stop
```

每个批次写入独立地址：

```text
hdfs://localhost:8020/ev-charging/quality/batches/<prl-run-id>/
├── dwd/                         # #4 七张精确业务表
├── audit/findings/              # 规则命中
├── audit/quarantine/            # 原始行与全部隔离原因
├── audit/clean_events/          # 内部事件清洗快照
├── audit/dwd_lineage/           # DWD 业务键 ↔ _row_id ↔ PRL run_id
├── reports/                     # JSON/Markdown、契约与策略快照
├── DATA_VALIDATED/              # 七表读回断言完成
└── RUN_SUCCEEDED.json           # 报告也已上传后的整批成功标志
```

同时在 `/home/<user>/ev-part2/evidence/<prl-run-id>/dwd-handoff/` 导出本地试交接包。导出器会根据 `RUN_SUCCEEDED.json`、七表行数和所有文件 SHA-256 重新校验，不会发布到正式 `/ev-charging/dwd`。

## 4. 最新小样本结果

运行批次 `prl-clean-20260915T013909Z-44731`，YARN 应用 `application_1789436319409_0001`，作业内部耗时 80.67 秒。

| 表 | 输入 | DWD 保留 | 隔离 |
|---|---:|---:|---:|
| users | 100 | 99 | 1 |
| stations | 8 | 7 | 1 |
| chargers | 48 | 42 | 6 |
| orders | 3,000 | 2,553 | 447 |
| telemetry | 8,000 | 6,433 | 1,567 |
| station_hourly | 1,344 | 1,172 | 172 |
| events | 100 | 87 | 13 |
| **合计** | **12,600** | **10,393** | **2,207** |

这里的隔离量包含级联影响。例如 1 个坐标越界站点被保守隔离后，其桩、订单、遥测、小时和事件引用会被标记为 `cascade`。这不等于这些行各自又有一个原始注入问题。

七表写后读回均为：业务键数＝行数，违规数＝0。小时表在已保留 7 个站点的 7 日范围内理论为 1,176 行，实际保留 1,172 行，缺 4 个异常小时。因此报告显式给出 `row_offset_ml_safe=false`，#5 不可以直接用“第 h 行”代替“第 h 小时”。

## 5. 注入对账与必须决策的差异

| 规则 | TP | FP | FN | 结论 |
| Q1 | 55 | 0 | 0 | 必填缺失精确对上；可选头像／昵称默认值不计 Q1 |
| Q2 | 78 | 505 | 32 | 详见下文，不能对着标签改检测器 |
| Q3 | 30 | 0 | 3 | 3 条“功率×10”后仍未超关联桩额定功率 |
| Q4 | 110 | 0 | 0 | 非标准时间格式全部标准化 |
| Q5 | 13 | 0 | 0 | 当前隔离逻辑矛盾，#4 旁路实现会裁剪超占用小时 |
| Q6 | 14 | 8 | 1 | 只修正可精确证明的元／分错误 |
| Q7 | 9 | 0 | 0 | 原始孤儿引用精确对上 |
| Q8 | 2 | 0 | 0 | 手机号／枚举非法值精确对上 |
| Q9 | 1 | 0 | 0 | 检出精确；当前选择隔离而非猜测坐标 |
| Q10 | 2 | 0 | 0 | 可恢复文本标准化精确对上 |

Q2 的具体原因不是 Spark 偶发误差：#4 `_duplicate_payload` 复制业务负载但故意不复制 `id`。因此 30 条订单在“主键 id 重复”契约下全部是 FN；遥测的业务键是 `(charger_id, recorded_at)`，80 个标签中 78 个命中，同时生成器的自然随机数据还产生 505 个未标签的相同业务键。团队必须选定“主键重复”或“业务负载重复”后同时修改文档、生成器和规则，不应仅为了把召回率做成 100% 而修改报告。

Q6 使用整数 `amount_fen // 100`，已不可逆地丢失原分位。当原值×100 与参考金额不精确相等时，PRL 不会猜测丢掉的余数；未标签但超出参考价格容差的订单仍会如实显示为 FP。

## 6. DWD → DWS → ADS 对账

隔离检查入口 `part2.scripts.check_scml_downstream` 不会写正式数仓：它为每次执行生成随机 Hive database 和必须不存在的独立 HDFS 输出目录，使用哈希锁定的 #4 DDL/DWS/ADS SQL。

不启用时间适配时，原 SQL 实际运行失败：营收和订单数正确，但 `dws_charger_day.charge_duration_sec` 为 0，而 DWD 可复算值为 14,273,160 秒。根因是 #4 SQL 中：

```sql
UNIX_TIMESTAMP(ended_at) - UNIX_TIMESTAMP(started_at)
```

建议在 #4／集成分支修正为：

```sql
UNIX_TIMESTAMP(CAST(ended_at AS TIMESTAMP))
  - UNIX_TIMESTAMP(CAST(started_at AS TIMESTAMP))
```

`--iso-timestamp-adapter` 只在执行内存中做这一处转换，#4 源文件不变。启用后，YARN 应用 `application_1789436319409_0002` 实际生成 4 张 DWS 和 7 张 ADS，10 项数值对账全部精确相等：

- DWD 已完成订单：2,443 单；`dws_station_day` 与 `ads_daily` 均为 2,443 单。
- DWD 已完成订单金额：9,978,037 分；3 张 DWS 和 4 张 ADS 的营收／金额汇总均为 9,978,037 分。
- DWD 已完成订单充电时长：14,273,160 秒；`dws_charger_day` 汇总精确相等。
- 物化行数：DWS 为 49/294/668/28；ADS 为 7/42/7/49/1,172/8/4。

## 7. 质量报告→`ads.db`

发布前必须拿到同一 PRL 批次的 `quality_report.json` 和 `cleaning_report.json`，且其七张 DWD 断言必须表名齐全、全部通过。示例：

```bash
ev-part2 python -m part2.scripts.publish_quality_ads \
  --quality /absolute/path/reports/quality_report.json \
  --cleaning /absolute/path/reports/cleaning_report.json \
  --ads-db /absolute/path/handoff/ads/ads.db \
  --backup /absolute/path/handoff/ads/ads.before-prl.db \
  --receipt /absolute/path/handoff/ads/prl-quality-receipt.json \
  --accept-pending
```

入口的强制保护：

- `ads_meta.sourceRunId` （兼容回退到 `runId`）必须与质量报告 `source_run_id` 相等。
- `ads_meta`、`ads_quality_table`、`ads_quality_issue`、`ads_quality_meta` 的列名和顺序必须精确符合 #4 schema。
- 备份目标、回执目标和数据库不能重名；已有备份或回执拒绝覆盖。
- 仅删除并重写三张 `ads_quality_*` 表，写后读回行数与质量报告 SHA-256，全部通过后才 commit SQLite 事务。
- `R01～R10` 的 `injected=TP+FN`、`detected=handled=TP+FP`、`recall=TP/(TP+FN)`；precision、FP、FN 和待决策项保存在 `ads_quality_meta.exactMetrics/pendingPolicyNotes`。
- `--accept-pending` 仅允许用 `ready_for_team_delivery=false` 的报告试联调；回执会明确记录 `accepted_pending_policy=true`。

同一小样本的本地 SQLite 副本已成功发布 7 条表级记录和 10 条规则记录，发布后质量报告 SHA-256 为 `43644cd8ec45c5af570ea67e85d9a24bee1e0810e2a0817e58be3d935d81b1f7`，原库备份已保留。该库是试验副本，不是集成分支的正式 `ads.db`。

## 8. 集成方建议的合并顺序

1. 将 #3 的代码和契约合入 `feat/part2-integration`。
2. 在 #4 `warehouse/sql/dws_etl.sql` 落地 ISO TIMESTAMP cast 修复，用 `check_scml_downstream` 不带适配开关重跑，确认原 SQL 模式也全绿。
3. 完成 Q2/Q3/Q5/Q6/Q9 策略决策，同时更新生成器、注入日志、PRL 规则和本文，不允许只改一端。
4. 取得或重新生成正式全量 ODS，以 `--require-full-input` 运行，确认小时完整性、分区、资源消耗、金额平衡及 DWD/DWS/ADS 逐层对账；不得用集成分支现有的旁路 ADS 代替这一步。
5. 使用 `publish_quality_ads` 将同源质量报告写入当次正式 `ads.db`，再由 #2/#1 执行 API 与页面联调。
6. 在 Ubuntu 22.04 集成机复验后，再将该批次标为演示候选或正式发布。

小型可提交证据摘要见 [2026-09-15-scml-integration.json](evidence/2026-09-15-scml-integration.json)。完整 ODS、Parquet、Spark 日志、SQLite 副本和备份保留在本机 `/home/hushengyuan/ev-part2/evidence/`，不进 Git。
