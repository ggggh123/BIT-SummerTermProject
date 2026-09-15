# 第二阶段 · 数据链路与运营大屏交付说明

分支：`feat/part2-prl`。本目录承载第二阶段从 ODS、PRL 清洗、SCML 数仓、Spark ML 预测到 Flask/运营大屏的可运行链路；第一阶段 Qt 客户端、管理端和发行包不在这里修改。

## 当前交付结论

2026-09-15 已在本机真实 YARN 环境完成一轮**正式规模模拟数据**的端到端批次，且将最终 ADS 以只读 SQLite 形式交给 Flask 和生产构建后的运营大屏。不是把静态 mock 数据当作验收数据：前端生产配置固定为访问 `/api`，后端从最终 `ads.db` 读取指标、质量摘要和 Spark ML 预测结果。

本次正式批次的闭环如下：

```text
ODS（#4 交接包，1,162,576 行）
  → PRL（Spark on YARN，Q1–Q10 检测/清洗/隔离）
  → DWD（7 表，883,731 行）
  → DWS（4 表）/ ADS（运营汇总）
  → Spark ML（24 个 horizon 的 GBT 选择与预测）
  → ads.db（120 个预测点 + 24 条指标）
  → Flask /api/* + web/dist（真实库展示）
```

可以在不启动 Hadoop 的演示模式下直接打开已物化 ADS：

```bash
# 下同：cd 到**你自己的仓库根**即可（本仓库脚本一律从自身位置推导根目录，无需改任何路径）
cd "$(git rev-parse --show-toplevel)"

# handoff/ads/ 为当前机器生成的交付数据（受 .gitignore 保护，不提交大库）
PART2_SKIP_HADOOP=1 bash part2/scripts/start_part2.sh
# 浏览器打开 http://127.0.0.1:5000/

PART2_SKIP_HADOOP=1 bash part2/scripts/stop_part2.sh
```

`start_part2.sh` 会优先读取 `handoff/ads/ads.db`，启动后核验已经占用的端口是否确实对应同一份 ADS，避免误连旧服务。若演示机的数据位于别处，可显式传入绝对路径：

```bash
PART2_SKIP_HADOOP=1 ADS_DB=/absolute/path/ads.db PART2_PORT=5051 \
  bash part2/scripts/start_part2.sh
```

详细数据证据见 [正式规模验证记录](docs/quality-verification.md) 与机器可读摘要 [2026-09-15-formal-full.json](docs/evidence/2026-09-15-formal-full.json)。

## 正式批次摘要

| 环节 | 实际结果 |
|---|---|
| ODS 来源 | `scml-20260914`，8 站、288 桩、5,000 用户、120,000 订单、1,000,000 遥测，共 1,162,576 行 |
| PRL | `prl-clean-20260915T024010Z-55661`；YARN `application_1789439983572_0001`；351.23 秒 |
| DWD | 7 张表、883,731 行；7 项写后读回断言全部通过 |
| DWS | `station=630`、`charger=22,320`、`user=86,889`、`region=450` |
| ADS | 7 站、251 桩、97,804 有效订单；最终库 SHA-256 为 `ab281d6d…c99585eaef` |
| 训练 | Spark MLlib 在 YARN 上完成 24 个预测 horizon，24 个负荷模型均选择 GBT |
| 预测 | `ml-20260915-111604`；5 个启用站 × 24 小时 = 120 点，24 条评估指标，`isBaseline=false` |
| 展示 | Flask 契约自测 55/55、前端形状自测 43/43、静态产物自测通过；生产前端不再内嵌 mock 结果 |

## 代码和责任边界

| 目录 | 内容 |
|---|---|
| `contracts/` | ODS/DWD 契约、质量策略、#4 SCML 字段快照 |
| `common/` | 交接 manifest、分区、哈希、行数、原始行标识校验 |
| `quality/` | Q1–Q10 规则、Spark 关联/窗口检测、质量画像和 TP/FP/FN 对账 |
| `clean/` | 标准化、隔离、DWD 投影及业务键/外键/物理范围断言 |
| `scripts/` | PRL、交接、SQLite 发布与运行时管理脚本 |
| `scml/` | ODS→DWS→ADS 的结构化数仓构建、SQL 与交接检查 |
| `ml/` | 特征、训练、预测、预测结果发布与 ADS 合并校验 |
| `../server/` | Flask 蓝图、ADS 只读层、契约/静态产物自测 |
| `../web/` | 三栏运营大屏、真实 API 客户端、mock 夹具和 Node 回归 |
| `docs/` | 设计、验证记录和小型、可提交的证据摘要 |

生成的大型 ODS/DWD/DWS/ADS、Parquet、模型、HDFS 运行目录和日志均不进 Git；它们由 manifest 与 SHA-256 对应，避免将数百 MB 的演示数据混入源码历史。

## 验证入口

从仓库根目录执行。`ev-part2` 是本机已配置好的 Java/Hadoop/Spark/Python 运行时包装器。

```bash
cd "$(git rev-parse --show-toplevel)"   # 你的仓库根

# Python 单元与本地 Spark 回归
ev-part2 python -m unittest discover -s part2/tests -p 'test_*.py' -v
ev-part2 python -m unittest part2.tests.spark_checks -v
ev-part2 python -m unittest discover -s part2/ml/tests -p 'test_*.py' -v

# 已物化 ADS 的后端/前端契约检查
ADS_DB=/absolute/path/ads.db ev-part2 python server/tools/selftest_contract.py
ADS_DB=/absolute/path/ads.db ev-part2 python server/tools/selftest_frontend_shape.py
ADS_DB=/absolute/path/ads.db ev-part2 python server/tools/selftest_static_dist.py

# web 构建与 Node 回归（生产构建强制使用真实 /api）
cd web && npm test && npm run build:live
```

要重新清洗另一个 ODS 交接包，必须使用全量闸门，且每次会产生新的不可覆盖批次：

```bash
ev-part2 start
bash part2/scripts/run_pipeline.sh /absolute/path/to/ods-handoff \
  --require-full-input --accept-draft
ev-part2 stop
```

`--require-full-input` 只校验输入规模，`--accept-draft` 只承认当前质量策略尚未由团队冻结，二者互不替代。

## 已知边界（如实保留）

- 本项目的数据是按业务规则构造的正式规模**模拟数据**，不是外部生产充电站的实时数据；演示时应表述为“基于模拟运营数据的分析与预测”。
- 正式 PRL 报告仍为 `ready_for_team_delivery=false`，原因是 Q2/Q5/Q6/Q9 等清洗策略尚待团队签字冻结；这不影响本轮演示链路的已物化数据，但不能伪称为已完成治理制度审批。
- 清洗后 7 站 × 90 天 × 24 小时应有 15,120 个小时观测，保留 15,073 个，缺 47 个；ML 特征按 `observed_at` 对齐，**不会**把“第 n 行”误认为“第 n 小时”。
- 本机为 Ubuntu 25.04 开发环境。Java 8、Hadoop 3.2.1、Spark 3.5.7、Python 3.10.21 的链路已实测；Ubuntu 22.04 的最终复验需要在目标机按同一份交接包执行。
- `web/package.json` 声明 Node ≥23；本机 Node 20.18 已通过本轮构建和测试但会产生 engine 警告。验收机建议使用声明版本，避免将当前兼容性结果误解为完整的平台认证。

## 演示建议

1. 先在服务器/大屏机执行 `PART2_SKIP_HADOOP=1 bash part2/scripts/start_part2.sh`，进入运营总览。
2. 用 `/api/health` 展示当前 `runId` 与读取的 ADS 绝对路径，再切换到企业、用户、站点、政府和预测页面，说明它们都来自同一份最终 ADS。
3. 在预测页强调本批次 `isBaseline=false`、5 站 × 24 小时、并说明训练/预测运行在 Spark on YARN。
4. 如被追问数据治理，展示质量页并如实说明已完成全量检测/隔离/对账，但策略审批仍是团队管理项。
