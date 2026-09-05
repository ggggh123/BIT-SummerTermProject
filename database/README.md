# database — SQLite schema、seed 与黄金库 profile

责任人：#4（SCML）。运行期 SQLite writer 始终是 Qt 管理/服务端；本目录脚本只离线构造、校验或导出数据工件，不能替代运行期服务端事务。

## 文件与职责

- `schema.sql` — v1 DDL、约束和索引；即使 core 不启用预测，也保留预测表和合同兼容性。
- `seed_demo.py` — 确定性 demo/history generator（`seed_database`）。
- `build_golden.py` — 构建不含 active forecast 的基础黄金库及 manifest。
- `export_ml_history.py` — 从基础库只读导出 ML 历史 CSV。
- `finalize_golden.py` — 导入一份已批准的 144 条预测，封存预测增强黄金库及 hash/manifest。

## Core：基础黄金库

core profile 的黄金库只要求业务 schema、确定性 seed、站点/充电桩/订单历史和模拟器所需数据。它不要求在线 ML、Web snapshot 或 active forecast；预测表和 v1 合同能力仍保留，缺少 active forecast 是合法状态。

本诊断候选已引入 `feat/data@10034fd` 封存的 `runtime/golden/core.db`，清单为 `core.manifest.json`，校验文件为 `core.db.sha256`。SHA-256 为 `5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7`，6站48桩、30用户、431个已完成订单，不含 active forecast。该封存文件仍为旧三字段 request_log，服务端在运行副本启动时迁移；不得直接用封存原件运行服务。

需要重建时，先输出到新目录。现有构建器固定写 `manifest.json`，不能直接向旧黄金目录重建，以免覆盖 demo 的清单。下面生成的是新的候选文件，不表示完成集成或发布：

```bash
core_candidate_dir=$(mktemp -d /tmp/ev-core-golden-candidate-XXXXXX)
python3 database/build_golden.py \
  --output-dir "$core_candidate_dir" --seed 20260901 \
  --cutoff 2026-09-01T09:00:00+08:00 --name core.db
```

构建后应按核心验收清单校验 hash、SQLite 完整性与核心闭环，再由 #4 明确发布候选库、`core.manifest.json` 和 `core.db.sha256`，保留历史 demo 工件。当前 schema 已包含服务端日志扩展，不能要求新建库的二进制哈希仍等于旧封存库。不得由用户端、模拟器或人工直接修改运行期数据库。

## Optional：预测增强黄金库

当且仅当显式启用 ML optional profile 时，先从基础库导出历史，完成独立训练/验证并得到已批准的发布 payload，再封存预测增强库：

```bash
# 从基础黄金库导出训练历史（12,960 行）
python3 database/export_ml_history.py \
  --db runtime/golden/core.db --out runtime/ml/station_hourly_history.csv

# 可选 ML 批次获批后，导入 144 条预测并封存增强库
python3 database/finalize_golden.py \
  --output-dir runtime/golden --base runtime/golden/core.db \
  --forecast runtime/ml/forecast_last_good.json --name demo.db
```

预测增强库不是 core release 的前置条件。启用后仍必须满足 v1 `forecast.publish`/`forecast.latest`、`ForecastRun`/`ForecastRecord` 和 snapshot contract；它也不能把 Web 或 ML 变成核心闭环的隐式依赖。

## 已封存的历史/可选 demo 工件

当前已跟踪的 `runtime/golden/demo.db`、`runtime/golden/demo.db.sha256` 和 `runtime/golden/manifest.json` 是**已封存的预测增强 demo 库**，manifest 记录 1 个 forecast run 和 144 条 forecasts。该工件继续保留，不能为范围重置而删除、覆盖或改称为无预测的 core 基础库。它可作为 optional 演示和兼容性证据；其存在不表示 core release 已要求或已完成 ML/Web 联调。

## 数据库测试与确定性

```bash
python3 -m pytest database/tests -v
```

- 固定 seed 为 `20260901`，默认 cutoff 为 `2026-09-01T09:00:00+08:00`。
- 相同 seed/cutoff/schema 的基础库产生相同 canonical data hash。
- 金额使用整数分；时间使用带 `+08:00` 的 ISO 8601。
- `finalize_golden` 写入的 `activated_at` 是导入时间；运行期 `activatedAt` 由服务端在副本激活时设置。
