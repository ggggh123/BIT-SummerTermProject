# 第二阶段分工编号与姓名对照（修订记录）

> 维护人：#1 王浩恩（PM）｜版本：v1｜日期：2026-09-14
> 用途：第二阶段编号↔姓名↔模块↔分支的唯一对照表。桌面 `Part2/00-第二阶段总体设计说明书.md` §8.1 只有编号没有姓名，本表为其姓名列的落地件；两处冲突时以本表 §3 为准。

---

## 1. 第二阶段有效对照表

| 编号 | 姓名 | 第二阶段模块 | 分支 | 远端最新提交（2026-09-14） |
|---|---|---|---|---|
| #1 | 王浩恩 | 前端大屏 + 联调 + 演示与答辩材料 | `feat/part2-web`、`feat/part2-integration` | `18e000f` |
| #2 | 倪宇骏 | 环境推广 + 系统集成 | `feat/part2-02`（设计文档归档在 `part2/plans/`） | `fef732c` |
| #3 | 胡晟源 | PySpark 质量发现 + 清洗 | 待建 | — |
| #4 | 杨佳车 | 生成器 + 数仓（ODS→DWD→DWS→ADS） + **Flask 后端** | `feat/part2_SCML` | `497c1e7` |
| #5 | 庞项祯 | Spark MLlib 预测 + 分担 `dws_region_day`、`ads_gov_service` | `feat/part2-ml` | `2119398` |

集成分支 `feat/part2-integration` = `feat/part2-web` + `feat/part2_SCML`，本地 `351693f`，**尚未推送**。

## 2. 与第一阶段的差异（编号发生了调换）

第一阶段 `docs/superpowers/plans/2026-09-01-project-kickoff.md` 第 74–78 行的原编号：

| 编号 | 第一阶段姓名 | 第一阶段模块 |
|---|---|---|
| #1 | 王浩恩 | Web 大屏 |
| #2 | 杨佳车 | Qt 管理/服务端（TL） |
| #3 | 胡晟源 | Qt 用户端与腾讯地图（PRL） |
| #4 | 倪宇骏 | SQLite 与模拟器（SCML） |
| #5 | 庞项祯 | ML（PE） |

第二阶段 **#2 与 #4 互换**：杨佳车 `#2 → #4`，倪宇骏 `#4 → #2`；`#1`/`#3`/`#5` 编号不变，但 #3 的模块从 Qt 用户端换成 PySpark 质量发现与清洗。

因此凡引用第一阶段文档（`README.md`、`docs/management/scope-baseline.md`、`docs/superpowers/plans/2026-09-01-project-kickoff.md` 等）里的「#2 杨佳车」「#4 倪宇骏」，**不可直接套到第二阶段**；带姓名的写法只在第一阶段语境下有效。

## 3. 与《Part2/00》§8.1 的差异（模块归属）

| §8.1 写的 | 实际 | 处理 |
|---|---|---|
| #1 PM：Vue3 + ECharts 前端大屏 | 一致 | — |
| #2 TL：Hadoop/Spark 环境 + **Flask 后端** + 系统集成 | #2 只做环境推广 + 系统集成；**Flask 后端归 #4** | 引用 §8.1 时按本表，#2/#4 分工处不照抄 |
| #3 PRL：PySpark 质量发现 + 清洗 | 一致 | — |
| #4 SCML：生成器 + SparkSQL 数仓分层 | 一致，另加 **Flask 后端** | 同上 |
| #5 PE：Spark MLlib 预测 + 分担两表 | 一致 | — |

另：《06-TL-Hadoop伪分布式安装部署指南》按 §8.1 署名 #2（TL）。该指南的路线本身仍有效，但实际执行与脚本落点是 #1 的 `scripts/part2/**`（自建版本基线 JDK 17 / Hadoop 3.4.1 / Spark 3.5.7），署名与归属需要 #2 确认一次。

## 4. 使用约定

1. 文档与提交信息中首次出现用「#编号 姓名」，其后只用编号。
2. `Part2/00` §8.1 的姓名列仍需回填：该文件只存在于 `origin/feat/part2-02` 的 `part2/plans/` 与本分支之外，需 #2 在其分支补列，或合并后由本表替代引用。
3. #3 的分支尚未建立，`feat/part2-03` 建立后补进 §1。
4. 本表随分工变动更新，不改任何指标口径（口径见 `docs/design/part2-api-contract.md`）。

## 5. 事实来源

- 分支与提交：`git log -1 <branch>`、`git worktree list`（2026-09-14 实测）。
- 第一阶段编号：`docs/superpowers/plans/2026-09-01-project-kickoff.md` 第 74–78 行。
- 第二阶段编号与模块：本组 2026-09-14 确认的分工口径。
- 前端与第一阶段回归测试：`node --test web/tests/*.test.mjs`（36 项通过）、`node --test dashboard/tests/*.test.mjs`（36 项通过）。
