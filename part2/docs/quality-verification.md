# PRL 质量检测、清洗与 SCML 联调验证记录

日期：2026-09-15。本记录取代 2026-09-14 的“只验证 PRL 24 行夹具”作为当前有效摘要；历史开工证据仍保留在 [kickoff-verification.md](kickoff-verification.md) 和 `docs/evidence/2026-09-14-*`。

## 1. 验证结论

#3 已完成与 #4 SCML 原生小样本的可重现技术联调：ODS 交接验签、Q1～Q10 检测／清洗、七表 DWD 投影、写后读回断言、DWD→DWS→ADS 分层数值对账和 `ads.db` 质量三表发布副本均已成功。

本结论仍有明确边界：#3 数据是 12,600 行小样本，策略未冻结，#4 时长 SQL 修复尚未落到对方分支，还未做 #3 全量性能、正式数仓发布、Flask/页面实库端到端或 Ubuntu 22.04 复验。因此报告保持 `ready_for_team_delivery=false`。集成分支新增的正式规模页面／ADS 证据来自 #4 旁路产物，不等于本清洗链已经全量通过。

## 2. 回归测试

| 层级 | 结果 | 覆盖重点 |
|---|---:|---|
| 纯 Python `test_*.py` | **78/78 通过** | 契约、时间／金额边界、manifest 篡改／路径越界，SCML 分区／表头，SQLite 批次／schema／备份／事务 |
| 本地 Spark `local[2]` | **19/19 通过** | 分布式去重／关联，Q1～Q10 精确夹具，内部 DWD，SCML 七表分区读回，空分区 schema，类型／字段顺序漂移拒绝 |
| #4 原生交接校验 | **通过** | 32 个 manifest 条目、表级摘要、分区、哈希、原始行标识和注入引用；原包字节未改 |

本地 Spark 日志仍包含 PySpark 第三方 `ResourceWarning: unclosed socket`，但测试进程退出码为 0 且 19 项全通过。不会删去原始警告冒充“无警告”。

## 3. 最终 YARN 清洗批次

| 字段 | 实际值 |
|---|---|
| PRL 批次 | `prl-clean-20260915T013909Z-44731` |
| 来源批次 | `scml-20260914` |
| YARN 应用 | `application_1789436319409_0001` |
| master | `yarn` |
| Spark／Python | Spark 3.5.7；driver 及 2 个 worker 均为 Python 3.10.21 |
| 作业内部耗时 | 80.67 秒 |
| HDFS 批次 | `hdfs://localhost:8020/ev-charging/quality/batches/prl-clean-20260915T013909Z-44731` |
| 数据规模 | 12,600 行输入；10,393 行 DWD；2,207 个唯一原始行隔离 |
| 小时覆盖 | 清洗后 7 站×7 日×24 理论 1,176 行，保留 1,172，缺 4；`row_offset_ml_safe=false` |

| DWD 表 | 行数 | 唯一业务键 | 违规 |
|---|---:|---:|---:|
| `dwd_order_detail` | 2,553 | 2,553 | 0 |
| `dwd_telemetry_detail` | 6,433 | 6,433 | 0 |
| `dwd_station_hourly` | 1,172 | 1,172 | 0 |
| `dwd_event` | 87 | 87 | 0 |
| `dim_stations` | 7 | 7 | 0 |
| `dim_chargers` | 42 | 42 | 0 |
| `dim_users` | 99 | 99 | 0 |

七表均使用 #4 精确字段名、类型和顺序；四张事实表已按 `dt` 物理分区。业务表不追加 `_row_id/_run_id`，追踪对应放入 `audit/dwd_lineage`。交接包已从 HDFS 导出并二次逐文件验签。

## 4. 质量注入对账

| 规则 | TP | FP | FN | 召回率 | 精确率 |
|---|---:|---:|---:|---:|---:|
| Q1 | 55 | 0 | 0 | 100.0% | 100.0% |
| Q2 | 78 | 505 | 32 | 70.9% | 13.4% |
| Q3 | 30 | 0 | 3 | 90.9% | 100.0% |
| Q4 | 110 | 0 | 0 | 100.0% | 100.0% |
| Q5 | 13 | 0 | 0 | 100.0% | 100.0% |
| Q6 | 14 | 8 | 1 | 93.3% | 63.6% |
| Q7 | 9 | 0 | 0 | 100.0% | 100.0% |
| Q8 | 2 | 0 | 0 | 100.0% | 100.0% |
| Q9 | 1 | 0 | 0 | 100.0% | 100.0% |
| Q10 | 2 | 0 | 0 | 100.0% | 100.0% |

Q1 旧的“可选头像也记为缺失填充”假阳性已修正：本批次 FP 从 100 降为 0。Q2/Q3/Q6 仍未被人为“调成全绿”，因为它们是 #4 生成器／标签与契约的真实语义差异，而非运行失败。详细根因和建议见 [scml-integration.md](scml-integration.md)。

## 5. 下游数仓实际消费

第二个 YARN 应用 `application_1789436319409_0002` 使用 #4 提交 `e519d8d` 的 DDL、DWS SQL、ADS SQL 和 `dim_date` 生成函数，在随机隔离 Hive database/独立 HDFS 目录中实际执行。

原始 SQL 模式曾在应用 `application_1789434286185_0001` 稳定重现一个错误：营收和订单数正确，但充电时长为 0。将 `UNIX_TIMESTAMP(string)` 在内存中适配为 `UNIX_TIMESTAMP(CAST(string AS TIMESTAMP))` 后，10 项对账全部通过：

- 已完成订单 2,443 单，DWD/DWS/ADS 一致。
- 营收 9,978,037 分，3 张 DWS 和 4 张 ADS 汇总均与 DWD 精确相等。
- 充电时长 14,273,160 秒，`dws_charger_day` 与 DWD 精确相等。
- 4 张 DWS 的行数为 49、294、668、28；7 张 ADS 的行数为 7、42、7、49、1,172、8、4。

联调入口报告 `source_sql_files_unmodified=true`、`executed_sql_mode=iso-timestamp-adapter`，明确区分了“源文件未改”和“本次执行语句已适配”。该修复仍需正式落到 #4／集成分支。

## 6. SQLite 质量面板发布副本

同一 #4 ODS 小样本通过对方纯 Python `build_local.py` 生成一份新 `ads.db`，再用 `publish_quality_ads` 发布本批 PRL 结果。结果：

- `ads_meta.sourceRunId=scml-20260914` 与质量报告来源批次一致。
- 七条表级统计、十条 R01～R10 和精确 TP/FP/FN 元数据写后读回通过。
- 原库备份与发布回执均已生成；只替换三张 `ads_quality_*`，其他表保留。
- 质量报告 SHA-256：`43644cd8ec45c5af570ea67e85d9a24bee1e0810e2a0817e58be3d935d81b1f7`。
- 报告仍未冻结，因此回执如实为 `accepted_pending_policy=true`，未冒充正式发布。

## 7. 可核对证据

完整本机证据不进 Git，位于：

- 最终批次：`/home/hushengyuan/ev-part2/evidence/prl-clean-20260915T013909Z-44731/`。
- 对方源快照、原 SQL 失败报告、SQLite 副本和回归日志：`/home/hushengyuan/ev-part2/evidence/prl-scml-adapter-l5NxvOnU/`。
- HDFS 批次：`hdfs://localhost:8020/ev-charging/quality/batches/prl-clean-20260915T013909Z-44731`。

关键 SHA-256：

| 文件 | SHA-256 |
|---|---|
| 本批次源码 ZIP | `87eaf6dc2c7b10be3779bfea9f0bfcf0fa19a52b45327668be31f67cb1dd3669` |
| `quality_report.json` | `43644cd8ec45c5af570ea67e85d9a24bee1e0810e2a0817e58be3d935d81b1f7` |
| `cleaning_report.json` | `fa70b038bf30ab766d22ac30905c9d64eba360b0d1c9486ae8e2815bc91d0732` |
| `run_result.json` | `82a219966274b689da2817ce87fb8751a6596822f5289da0d4b47e10941c8438` |
| 最终下游对账报告 | `9102885886a3c28b7037ae6f4d8084cdcbbd81a2f7bf628768029b6d7d341433` |
| DWD 交接 manifest | `d9f084fbc11d57a86bade14f5fd5edb5fa1eb7beb2c506054ca77202ca474d4a` |
| SQLite 发布回执 | `3129da4898113168dbb70a7d04afb1d7d416ed0d45d3aaf7d844c2c3745a0a1b` |
| 本地 Spark 19 项回归日志 | `d69e2c02570b0fc3791690dd4835d4a3280da7ad9c57ee98c41af8820afb0588` |

仓库只纳入小型机器可读摘要 [evidence/2026-09-15-scml-integration.json](evidence/2026-09-15-scml-integration.json)，不纳入 ODS、Parquet、全量日志、SQLite 库或备份。

## 8. 环境与收尾状态

- 最后两个 YARN 应用均成功后，本轮启动的 NameNode、DataNode、SecondaryNameNode、ResourceManager、NodeManager 已正常停止；数据与日志保留。
- Spark 的 native-hadoop 加速库不可用，回退到 Java 实现；功能通过，但不代表全量性能已验证。
- #3 本机用全局、非 venv 的 Java 8/Hadoop 3.2.1/Spark 3.5.7/Python 3.10.21；远端集成分支文档使用 Java 17/Hadoop 3.4.1/Python 3.11 venv。两套基线尚未统一，必须由 #2 在最终集成前选定并复跑。
- 尚未执行全量规模、Ubuntu 22.04、正式 DWD 路径、Flask/页面实库联调和完整演示链路验收。
- 已复核 #4 最新 `13b18f0`：它只新增 `--require-full` 交付规模闸门，生成器、ODS/DWD 契约及本次使用的 DWS/ADS SQL 均未改变；因此小样本技术结论仍有效，但不能升级成正式规模结论。
