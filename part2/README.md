# 第二阶段 · #3 PRL 数据质量与清洗

分支：`feat/part2-prl`。起点为远端 `dev` 提交 `1bd4b89`；第一阶段 Qt、数据库和发行包不在本目录的修改范围。

## 当前结论

#3 的可执行主链路已完成，并已用 #4 SCML 提交 `e519d8d` 的原生小样本和数仓 SQL 做过真实 YARN 联调；随后复核了最新 `13b18f0`，确认其仅增加正式规模闸门，数据结构和 SQL 未变：

- 接收并验证 #4 的 `ods-dwd-v0.1` manifest、分区 CSV/JSONL、表级行数、SHA-256 及注入标签，不改写原始 ODS。
- 在 Spark on YARN 上独立执行 Q1～Q10 检测、修正、隔离、直接／级联影响区分和 TP／FP／FN 对账；注入标签只在检测完成后进入对账。
- 输出与 #4 DDL 字段名、类型、顺序和分区一致的七张 DWD：`dwd_order_detail`、`dwd_telemetry_detail`、`dwd_station_hourly`、`dwd_event`、`dim_stations`、`dim_chargers`、`dim_users`。四张事实表按 `dt` 分区，追踪字段单独放入 `audit/dwd_lineage`。
- 七表写入后重读，验证 schema、顺序、行数、业务键、外键、时间、金额、物理范围和派生值；随后实际跑通 #4 的 4 张 DWS 和 7 张 ADS。
- 提供质量报告→#4 `ads.db` 适配器：校验来源批次与四张既有 schema，先备份，再事务性替换三张 `ads_quality_*` 表，不覆盖其他 ADS 数据。

最新可核对结果、命令和局限见 [SCML 联调与交接说明](docs/scml-integration.md) 及 [验证记录](docs/quality-verification.md)。

## 不应被误读为已完成的事项

- 本轮 #3 清洗证据使用确定性小样本（12,600 行），不是十万订单／百万遥测的正式规模；#3 全量性能和长时间窗口尚未验收。远端集成分支虽已有 #4 旁路生成的 112,422 单／100 万遥测 ADS 与页面证据，但它没有经过本 PRL 清洗链，不能混作 #3 全量结果。
- #4 注入器与文档在 Q2、Q3、Q6 上有可重现的语义差异；Q5 是裁剪还是隔离、Q9 是坐标回填还是隔离也需团队冻结。实现不读标签“反推”答案，所以报告保留了真实 FP/FN。
- #4 `dws_etl.sql` 原语句的 `UNIX_TIMESTAMP(ISO8601+08:00)` 会把充电时长聚合为 0；联调入口只在内存中改为 `UNIX_TIMESTAMP(CAST(... AS TIMESTAMP))` 后通过，未静默改写 #4 的源 SQL。合并时需将这一行修复落到集成分支。
- `dim_date` 仍由 #4 生成；#3 的下游校验使用 #4 原函数构造独立日历表。
- 未向正式 `/ev-charging/dwd` 发布，未使用小样本覆盖队友数仓；正式发布要等策略冻结、全量跑通和团队评审。
- 本机运行系统是 Ubuntu 25.04。Java 8、Hadoop 3.2.1、Spark 3.5.7、Python 3.10.21 链路已通，但不能代替 Ubuntu 22.04 集成机复验。

## 阅读顺序

1. [当前总体设计](plans/00-第二阶段总体设计说明书.md) 与 [当前 PRL 设计](plans/03-PRL-数据质量检测与清洗设计.md)。
2. [ODS／DWD 契约及当前冻结边界](contracts/README.md)。
3. [质量／清洗运行说明](docs/quality-cleaning.md)。
4. [SCML 联调与交接说明](docs/scml-integration.md)。
5. [最新验证记录](docs/quality-verification.md)、[本机运行时](docs/local-runtime.md) 与 [开工阶段历史记录](docs/kickoff-verification.md)。

总体设计和五人分册由集成分支统一归档在 `part2/plans/`；本分支不再复制一套旧版 `docs/design/`，避免两份文档互相漂移。运行事实以本 README、`docs/` 下的验证记录和机器证据为准。

## 代码入口

| 目录 | 内容 |
|---|---|
| `contracts/` | PRL 内部 ODS 契约、质量策略及 #4 精确 DWD schema 快照 |
| `common/` | 交接 manifest、行数、表头、分区、哈希、原始行标识和注入标签校验 |
| `quality/` | 单行 worker 规则、Spark 关联／窗口规则、质量画像与对账报告 |
| `clean/` | 时间／金额标准化、内部 DWD 断言、#4 七表投影及读回断言 |
| `scripts/` | 运行时管理、ODS 校验、YARN 清洗、交接包、下游 SQL 检查和 SQLite 发布 |
| `tests/` | 纯 Python 回归与需显式运行的本地 Spark 回归 |
| `docs/` | PRL 运行说明、联调记录和小型证据摘要 |

## 本机执行

从仓库根目录执行：

```bash
cd /mnt/hgfs/Desktop/SummerTermProject/worktrees/part2-prl

# 不启动 Hadoop 的快速回归
ev-part2 python -m unittest discover -s part2/tests -p 'test_*.py' -v
ev-part2 python -m unittest part2.tests.spark_checks -v

# 真实 HDFS/YARN；中间路径替换为 #4 ODS 交接包
ev-part2 start
bash part2/scripts/run_pipeline.sh /absolute/path/to/ods-handoff --accept-draft
ev-part2 stop
```

正式规模必须再加输入等级闸门：

```bash
bash part2/scripts/run_pipeline.sh /absolute/path/to/ods-handoff \
  --require-full-input --accept-draft
```

`--require-full-input` 拒绝 `prl-test-fixture`；`--accept-draft` 只表示团队策略仍待冻结，二者检查的是不同维度。

`run_pipeline.sh` 每次生成独立 `run_id`，拒绝覆盖既有输出。不传 ODS 路径时，仅生成 24 行 PRL 回归夹具；传入 #4 原生包时，自动输出 SCML 七表 schema。最后给出人读报告、机器报告、HDFS 批次和本地 `dwd-handoff/` 路径。

若失败，先查当次 `spark.log`，再正常执行 `ev-part2 stop`。不要重新格式化 HDFS；失败批次保留证据，修复后使用新批次。

原始数据、Parquet、HDFS 运行目录和 SQLite 试联调库不提交 Git；仓库只保留代码、契约、测试和可审查的小型证据摘要。

## 下一步

1. #4 先将 ISO 时长 SQL 修复和 #3 七表交接合入第二阶段集成分支。
2. #2／#4／#5 冻结 Q2/Q3/Q5/Q6/Q9、动态价格和小时缺口策略；冻结前 `ready_for_team_delivery` 始终为 `false`。
3. 用 #4 正式全量 `ods-handoff` 经 `--require-full-input` 重跑，记录总耗时、分区量、最大 executor 内存、全量 TP/FP/FN 和小时覆盖率。
4. 将通过评审的质量三表发布到 #4 `ads.db`，由 #2 Flask 和 #1 展示页进行端到端联调。
5. 在 Ubuntu 22.04 集成机复验完整命令，然后再决定正式 DWD 发布和演示数据冻结。
