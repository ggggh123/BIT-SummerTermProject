# #3 PRL 质量检测与清洗运行说明

状态：2026-09-15 SCML 原生小样本联调版。#4 的 manifest、ODS 字段和七表 DWD schema 已适配，真实 YARN 清洗及 DWD→DWS→ADS 对账已通过；质量处置策略仍未经团队冻结。本说明不修改第一阶段 Qt／数据库代码。

## 1. 当前能独立完成什么

输入经本地交接校验后上传 HDFS。Spark 在 YARN 上以 StringType 读取七类 ODS，Python worker 完成严格类型转换与单行检测，DataFrame 完成引用核验、金额参考校验和窗口去重。清洗结果在独立批次中写为 Parquet，重新读取后执行 DWD 断言，再生成质量／清洗报告及本地试交接包。#4 原生 manifest 会自动进入 `scml-dwd-v0.1` 投影；PRL 自带的 24 行夹具仍保留旧六表路径做回归兼容。

检测函数没有注入日志参数，也不读取标签。Spark 作业直到问题明细、隔离行和 DWD 均落盘并通过断言后，才读取注入日志做 TP／FP／FN 对账。

当前独立批次位于 `/ev-charging/quality/batches/<run_id>/`；正式 `/ev-charging/dwd` 不被覆盖或追加。后者的发布须在契约冻结和真实数据验收后另行处理。

## 2. 实际入口

从本机工作树执行：

```bash
cd /mnt/hgfs/Desktop/SummerTermProject/worktrees/part2-prl
ev-part2 python -m unittest discover -s part2/tests -p 'test_*.py' -v
ev-part2 python -m unittest part2.tests.spark_checks -v
ev-part2 start
bash part2/scripts/run_pipeline.sh
ev-part2 stop
```

`run_pipeline.sh` 默认生成 24 行 PRL 夹具，不请求 #4 的私有文件、不生成全量运营数据。传入 #4 交接包时，先完成原生 manifest 校验，然后输出七张 SCML DWD。每次生成新 run_id；输入、输出、报告和交接包均不覆盖旧批次。脚本不自动启停集群，失败后也请正常执行 `ev-part2 stop`，不重新格式化 HDFS。

脚本结尾会给出本地 `reports/quality_report.md`、`dwd-handoff/` 和 HDFS 批次地址。纯 Python 和本地 Spark 回归不要求 Hadoop 已启动；本地 Spark 文件使用明确的 `file://` URI，避免被全局 HDFS 默认文件系统误解释。

对 #4 原生交接包，在明确按待冻结策略试跑的前提下使用：

```bash
bash part2/scripts/run_pipeline.sh /实际收到的ODS包目录 --accept-draft
```

若 #4 的 manifest／字段不同，入口会先失败并保留源包。应讨论后调整映射，不应跳过校验、手改原始数据或把错误列强行补成零。

## 3. 十类规则和处置

| 规则 | 当前实现 | 处置 |
|---|---|---|
| Q1 缺失 | 必填字段；completed 必需结束时间；charging 必需开始时间 | 关键缺失隔离；可选昵称／头像可设展示默认值，但不计 Q1 命中 |
| Q2 重复 | 标准化业务键与内容比较，确定性 `_row_id` 排序 | 内容相同留字典序最小的一行；内容冲突整组隔离；不假造更新时间 |
| Q3 物理异常 | 非有限／负／越界电量与功率；遥测对关联桩额定功率；站点负载与温度 | 隔离，不将异常功率截成上限或零 |
| Q4 时间 | 多格式、秒／毫秒时间戳、时区归一；严格日期与项目年份范围 | 可解析非标准格式修正；无法解析或超范围隔离 |
| Q5 逻辑 | 订单时间与状态矛盾、占用数大于桩数、小时记录非整点 | 隔离，不猜测应归属时刻 |
| Q6 金额 | Decimal，非负整数分；completed 按策略核对单价×电量 | 原值×100与参考金额精确吻合才修正；其余超容差隔离 |
| Q7 引用 | 先后关联 users／stations／chargers／orders；events 按实体类型关联 | 原始即不存在标 direct；原始存在但被清洗隔离标 cascade |
| Q8 非法字段 | 手机号、主外键正整数、整数精度、已约定枚举 | 非法隔离；frozen 是合法账户状态，历史记录不因此删除 |
| Q9 坐标 | 北京近似包围框，非有限坐标 | 隔离，后续引用失效另计级联；不猜坐标 |
| Q10 文本 | 首尾空白、全角字母数字、零宽／控制字符、U+FFFD | 可恢复标准化；乱码替换字符隔离；正常中文标点不作为脏数据 |

Q7 的级联不与原始注入直接问题混算。一个记录可以同时有多个问题；`quarantined` 按唯一原始行算，不能将 Q1～Q10 命中量相加得到剔除量。

若一条最终隔离的记录同时有修正建议，报告不会将它计为“修正后保留行”；完整建议与原值仍在审计 Parquet。

## 4. 待确认参数，不分散硬编码到页面或下游

字段和目标类型来自 `contracts/ods-dwd-v0.1.json`；独立策略来自 `contracts/quality-policy-v0.1.json`。本轮未改变已发送的字段 JSON。

- 金额默认：假设批次内站点单价恒定，ROUND_HALF_UP；容差为 `max(1 分, 参考值×1%)`。这只是模拟数据提案，不适用于单价曾变化且缺少快照的历史账单。
- `money.reference=units_only` 只检查非负整数分，不回算历史账单，也不自动判断小金额就是元。这种运行不能宣称“参考单价一致性已验收”。
- 功率／电量／温度范围、坐标包围框、枚举、昵称默认值均在策略中。
- 年份暂允许 1900～2100；这是防止无业务意义的极端日期进入 Parquet 的宽范围规则，并不是已验证 90 天业务窗口。实际时间窗口仍须 #4 给出 cutoff 等元数据后冻结。
- 每表／规则／来源最多给 5 条问题样本；完整原始值在隔离审计表。

未冻结事项包括：动态价格快照、Q2 主键／业务负载重复口径、Q3 功率注入条件、Q5 隔离／裁剪、Q6 已丢失分位的处理、Q9 隔离／回填、小时缺口、数据截止时刻和全量分区性能。不要将当前实现的十类规则等同于覆盖所有可能的业务错误。

## 5. CSV 的重要读入约定

源 CSV 的 `\N` 表示 null；未加引号的空字段及 `""` 表示空字符串。两者必须区分。

Spark 解析 CSV 时会把未加引号的空字段解析为空值，因此读取器先仅以空串为解析空值，再将其恢复为空字符串，最后单独解码 `\N`。不能在 `nullValue=\N` 之后把所有 null 填空，否则真正的缺失值也会被抹掉。

先行交接校验要求 CSV 行列数与表头一致，不把缺列视为合法 null。CSV 使用 Python 标准双引号转义形式；读取器显式配置双引号 escape，并支持字段中的逗号、引号和换行。多行 CSV 按文件并行；全量时应按双方确认的分区拆文件，不宜将百万记录都塞在唯一一个 CSV 文件中。

## 6. DWD、审计与成功标志

#4 原生包使用的批次结构：

```text
<run_id>/
  dwd/
    dwd_order_detail/       # 事实表，dt 分区
    dwd_telemetry_detail/   # 事实表，dt 分区
    dwd_station_hourly/     # 事实表，dt 分区
    dwd_event/              # 事实表，dt 分区
    dim_users/              # 当期快照
    dim_stations/           # 当期快照
    dim_chargers/           # 当期快照
  audit/
    findings/               # 规则、原始行标识、直接／级联、处置与理由
    quarantine/             # 原始 JSON 和全部隔离原因
    clean_events/           # PRL 内部清洗事件快照
    dwd_lineage/            # 七表业务键与 _row_id/run_id 追踪
  _work/checkpoints/        # 可靠 HDFS checkpoint；不进入试交接包
  reports/                  # 质量、清洗、运行结果、契约与策略快照
  DATA_VALIDATED/           # 七表写后读回断言完成
  RUN_SUCCEEDED.json        # 报告也已上传，整个 HDFS 批次成功
```

SCML DWD 业务字段的名称、类型、顺序和分区与 #4 `dwd_contract.sql` 一致，不附加 `_row_id/_run_id`。金额为 BIGINT 整数分，业务电量和功率按 #4 契约输出 DOUBLE，时间为 `ISO8601 +08:00` STRING。未开工订单的 `started_at/hour/dt` 保留 null，不伪造为预约日期。

写后断言独立于注入标签：精确 schema 及顺序、业务键唯一、必填非空、枚举、物理范围、时间先后、时间与 `dt/hour`、外键、订单派生站点、等待时长和金额参考约束。读回行数还须等于清洗实际保留量。PRL 自带夹具仍用旧六表，只作内部回归，不是 #4 交接产物。

下游须检查整个批次的 `RUN_SUCCEEDED.json`，不能只看到某个 Parquet 目录 `_SUCCESS` 就使用半成品。失败批次保留，重跑使用新批次，不自动删除旧数据。

## 7. 报告接口草案（给 #1／#2 评审）

| 文件／字段 | 含义 |
|---|---|
| 公共元数据 | contract_version、policy_version、run_id、source_run_id、source_kind、data_cutoff、generated_at、application_id、master |
| quality_report.profile | 每表原始总行数、各列 null／空串计数、近似 distinct、可解析值范围（含隔离行） |
| quality_report.rules | Q1～Q10 名称、每表／direct 或 cascade 命中量和比例、有限样本 |
| quality_report.injection_comparison | 唯一 `(rule, table, row_id)` 的 TP／FP／FN、recall、precision；无分母时为 null |
| cleaning_report.tables | before、after、quarantined、repaired_retained_rows、filled_retained_rows、cascade_affected_rows、retention_rate |
| cleaning_report.rule_action_hits | 规则级 reject／deduplicate／repair／fill 建议量，不等于唯一剔除数 |
| cleaning_report.order_amount_reconciliation | 原始声明分值合计、隔离原金额、保留修正差额、输出整数分合计、不可解析金额数量、balanced |
| cleaning_report.dwd_assertions | 写后读回的每表行数、业务唯一键数、违规数和是否通过；原始行追踪在 lineage 表 |
| run_result.json | 真实作业版本、运行方式、耗时、范围、结果及 DWD 断言汇总 |

金额对账使用 Decimal 字符串序列化，避免 JSON 浮点损失。原始金额列含“元混入”时，它的原始数值合计不是实际营收；样本对账是 `6010 - 4000 + 990 = 3000`（分）。不可解析金额不被当作零业务金额，单独计数。

FP 是未匹配注入日志的直接命中。只有标签穷尽已知问题时，才可将它直接解释为误报；#4 正式数据出现额外真实缺陷时需要人工复核。`cascade` 始终另报，不能为凑召回率将其写进直接注入数。

## 8. 交接包与校验

`run_pipeline.sh` 会自动导出到本批次本地 `dwd-handoff/`。也可对已成功的独立批次使用 `ev-part2 python -m part2.scripts.export_dwd --batch <HDFS批次URI> --directory <新目录>`；只校验本地包时省略 `--batch`。

对 #4 原生输入，包内包含七张 DWD、`audit/dwd_lineage`、reports、成功标志、中文说明及 manifest；旧 PRL 夹具包仍为六张回归表。manifest 在表级记录 Spark 读回行数，在文件级记录 bytes／SHA-256。它与 ODS manifest 是不同格式，不能互换校验器。

导出器检查完整成功标志、拒绝已有输出目录、导出后核验每个文件。接收方做文件校验后，仍需在自己的 Spark 环境重读 Parquet：SCML 七表执行 `part2.clean.scml_dwd.assert_scml`，旧夹具表执行 `part2.clean.assertions.assert_dwd`。导出器本身只做交接文件与成功回执核验，不冒充接收端 Spark 复验。

试交接包不是 Git 提交内容。小样本类型明确为 `prl-dwd-fixture`；正式 ODS 即使试跑成功，在策略未冻结前也只会生成 `dwd-handoff-draft`。脚本不自动发送给队友，也不发布至正式数仓。

## 9. 下一步与队友接入

1. #4／集成分支先修正 `dws_etl.sql` 对 ISO8601 时间的时长计算，然后使用原 SQL 模式重跑分层对账。
2. #2／#4／#5 冻结 Q2/Q3/Q5/Q6/Q9 及小时缺口策略，再将 `ready_for_team_delivery` 由 false 转为正式状态。
3. 对 #4 全量 `ods-handoff` 执行性能、分区、时间／金额／站点及 DWD/DWS/ADS 逐层对账。
4. 通过 `publish_quality_ads` 发布同源质量三表，再由 #2 Flask 和 #1 页面读取验证。
5. 真正的 Ubuntu 22.04 集成机复验后，再决定正式 `/ev-charging/dwd` 和演示数据发布。
