# 第二阶段 · 数据链路与运营大屏交付说明

分支：`feat/part2-02`（第二阶段集成分支）。本目录承载第二阶段从 ODS、PRL 清洗、SCML 数仓、Spark ML 预测到 Flask/运营大屏的可运行链路；第一阶段 Qt 客户端、管理端和发行包不在这里修改。

## 当前交付结论

2026-09-15 / 09-16 已在演示机 `niyujun01`（Ubuntu 22.04.3）完成一轮**正式规模模拟数据**的端到端批次，且将最终 ADS 以只读 SQLite 形式交给 Flask 和生产构建后的运营大屏。不是把静态 mock 数据当作验收数据：前端生产配置固定为访问 `/api`，后端从最终 `ads.db` 读取指标、质量摘要和 Spark ML 预测结果。该批次为团队冻结的数据基线（后续改动都在其上做，不重跑）。

本次批次的闭环如下：

```text
ODS（#4 交接包 scml-20260914，1,162,576 行）
  → PRL（Spark on YARN application_1789457728162_0004，963.64 秒，Q1–Q10 检测/清洗/隔离）
  → DWD（7 表，1,017,419 行；7/7 写后读回断言通过）
  → DWS（4 表）/ ADS（运营汇总，本地 Spark SQL 物化）
  → Spark ML（YARN _0008/_0009/_0010：24 个 horizon 的 GBT 选择与预测）
  → ads.db（144 个预测点 + 24 条指标）
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
| PRL | `prl-clean-20260915T133614Z-56524`；YARN `application_1789457728162_0004`；963.64 秒；隔离 145,157 行 |
| DWD | 7 张表、1,017,419 行；7 项写后读回断言全部通过；小时覆盖 17,228 / 17,280（缺 52） |
| DWS | `station=720`、`charger=25,514`、`user=97,893`、`region=450`（本地 Spark SQL 物化） |
| ADS | `ads-20260915235603`；8 站、287 桩、110,696 有效订单；`ads.db` SHA-256 为 `7b9ba3d7…891d4148`（补发质量元数据后为 `cfb0b15e…`，业务数值不变） |
| 训练 | Spark MLlib 在 YARN 上完成 24 个预测 horizon，24 个负荷模型均选择 GBT |
| 预测 | `f-20260915-232113`；6 个启用站 × 24 小时 = 144 点，24 条评估指标，`modelVersion=gbt-3.5.7`，`isBaseline=false` |
| 展示 | Flask 契约自测 57/57、前端形状自测 47/47、Node 回归 55/55、静态产物自测通过；生产前端不再内嵌 mock 结果 |

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

`--require-full-input` 只校验输入规模，`--accept-draft` 只承认当前质量策略尚未由团队冻结（质量策略已于 2026-09-16 冻结，见 `contracts/README.md` §7 第 7 条；该参数保留以兼容历史批次），二者互不替代。

## 已知边界（如实保留）

- 本项目的数据是按业务规则构造的正式规模**模拟数据**，不是外部生产充电站的实时数据；演示时应表述为“基于模拟运营数据的分析与预测”。
- 质量策略已于 **2026-09-16 由 #2 TL 逐条评审并冻结**（`quality-policy-v0.1.json` = `0.2.2 / FROZEN_TEAM_REVIEWED`，`scml-dwd-v0.1.json` / `ods-dwd-v0.1.json` = `FROZEN_TEAM_REVIEWED`，见 `contracts/README.md` §7 第 7 条）。已物化批次早于口径修正：其 Q2 的 FP/FN 与报告中 `pending_policy_notes` 属冻结前的运行记录，**不作为成果口径**（演示库的质量面板已按冻结状态标注）。
- 清洗后 8 站 × 90 天 × 24 小时应有 17,280 个小时观测，保留 17,228 个，缺 52 个；ML 特征按 `observed_at` 对齐，**不会**把“第 n 行”误认为“第 n 小时”。
- 演示机 `niyujun01` 为 Ubuntu 22.04.3（Java 17 / Hadoop 3.2.1 / Spark 3.5.7 / Python 3.10.12），本批 PRL 清洗与 ML 已在该机 YARN 上完成；DWS/ADS 由同一套 Spark SQL 在本机 local 模式物化（日志中无 application id），**不冒称为集群作业**。
- `web/package.json` 声明 Node ≥23；演示机 Node 为 23.11.1，满足声明；开发机 Node 24.19 亦通过构建与回归。

## 演示建议

1. 先在演示机执行 `PART2_SKIP_HADOOP=1 bash part2/scripts/start_part2.sh`，浏览器打开 `http://<演示机IP>:5000/`，进入运营总览。
2. 用 `/api/health` 展示当前 `runId`（`ads-20260915235603`）与读取的 ADS 绝对路径，再逐页切换：主页（24h 实际/预测负荷）、用户视角（未来 1h 空闲推荐）、充电站视角（单站未来 24h 预测与满载风险）、企业视角、政府视角（全城未来 24h 预测与高峰预警）——强调所有页面来自同一份最终 ADS。
3. 预测统一口径：`f-20260915-232113`、`gbt-3.5.7`、`isBaseline=false`、6 站 × 24 小时 = 144 点；页面已区分显示“ML 模型预测 / 降级基线”。
4. 本组答辩**不现场启动 Hadoop**：若被追问平台，直接用 `part2/docs/evidence/2026-09-15-formal-full.json` 里的 YARN application id（PRL `_0004`、ML `_0008/_0009/_0010`）与 `run_result.json` 佐证；注意 YARN 重启会清空应用列表、且未开启日志聚合，不要现场敲 `yarn logs`。
5. 如被追问数据治理，打开质量面板：策略状态为「已确认」（0.2.2），TP/FP/FN 与级联明细均来自 #3 PRL 逐行对账；并如实说明本批数据由冻结前的注入端产出、Q2 的 FP/FN 不作成果口径。
