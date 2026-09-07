# 核心集成诊断基线（2026-09-06）

## 结论

已保存首批手机式 Qt UI，并在独立分支组合最新服务端、模拟器和核心黄金库。干净构建与现有自动化全部通过，但真实 TCP 探针仍复现三个 P0；**这是修复前基线，不是 GO 或发布验收证明**。

本记录对应后续修复提交之前的诊断快照。用户随后授权本机直接参与服务端、数据库和模拟器修复；后续修复结果必须另记，不改写这里的修前观察。

## 版本与范围

| 来源 | 精确提交 | 引入范围 |
|---|---|---|
| dev 基线 | `e21c73a9f3d118cac169cfeb7feb3d6d7f7d6f02` | 当前核心合同、共享库及其余已有内容 |
| 服务端 | `661c91e3dbe252628b4b8411a73495d9d3137005` | `apps/admin-server/`、`tests/admin-server/`、顶层 CMake、schema 与 schema 测试 |
| 模拟器/数据 | `10034fd3be016a5cb53f1ae417451b590ae6e50a` | `simulator/`、`runtime/golden/core.db` 及其 checksum/manifest |
| 用户端 UI | `48ee40cd90ad91f6138aeebf7d742dfd7e8fbde7` | `apps/user-client/`、`design-qa.md`、9 月 5 日 UI 计划/测试记录与截图 |

UI 主归档提交为 `a8ef5f09d5419c1be947dc478e6758b7f63d4afe`，后续 `48ee40c` 仅补充测试同步与核实后的文档说明。原截图未替换。本轮复验发现长内容滚动测试依赖固定等待，改为有超时的条件等待；保留原可见性、无横向裁剪、末项点击和选择断言，没有修改生产 UI 来规避失败。

本机分支：`integration/core-20260906`；目录：`/mnt/hgfs/Desktop/SummerTermProject/worktrees/core-integration`。UI 仍独立保存在 `feat/user-client-mobile-ui`。主 checkout 和共享 `dev/main` 未被修改、推送或合并。

以上是**精确路径快照组合**，不是整条功能分支的 ancestry merge。来源路径逐组与源树比较无差异；新增诊断文档另列。后续正式 PR 必须继续按来源 SHA 核对，不得假定这些功能分支已经合入共享分支。需求矩阵未读取、检查或编辑；Web/ML 成果保留而不恢复为核心门槛。

## 干净构建与自动化

工作目录为上述集成 worktree，构建目录位于 Linux 原生缓存：

```bash
integration_build=/home/hushengyuan/.cache/ev-core-integration-build-0R6scP
cmake -S . -B "$integration_build" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$integration_build" --clean-first -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir "$integration_build" --output-on-failure -j4
node --test apps/user-client/tests/test_navigation_html.mjs
python3 -m pytest database/tests -q
bash tests/scripts/test_check_env.sh
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2.18718 \
  "$integration_build/apps/user-client/tst_user_mobileui" -silent
```

| 检查 | 实测结果 |
|---|---|
| CMake/Ninja 全量 clean-first | 155/155 构建步骤完成，退出码 0 |
| 同组合 CTest | 20/20 通过，0 失败，39.76 秒 |
| 地图 HTML Node 回归 | 15/15 通过，0 失败，369.979 毫秒 |
| 数据库 pytest | 9/9 通过，1.30 秒 |
| 环境检查脚本的受控回归 | 13/13 通过；不代表启动了可选 Web/ML |
| 高 DPI 手机 UI QtTest | 16/16 通过，0 失败，1287 毫秒 |

归档导入保留历史 mtime，因此正式组合验证使用 `--clean-first`，未用旧模拟器编译产物替代最新 data 代码。CTest 原始输出留在 `evidence/core-integration-2026-09-06/ctest-before-fixes.log`。

CMake 的可选 XKB/Cups 提示、既有模拟器测试的 Qt 时间 API 弃用警告、Qt offscreen 的平台提示均未全局压制。完整 `git diff --check` 还报告源服务端 17 个文件末尾空行；为保留队友快照，本基线未清理这些源文件。不能将这份结果描述为“无任何警告”。

## 真实通信红灯

在最终干净构建后，于 `2026-09-06T03:04:48+08:00` 启动独立真实服务端，随机回环端口、全新诊断 SQLite，运行留档的长度前缀 TCP 探针：

- 重复同一 requestId 充值 100 分：`50000 → 50100 → 50200`，两次响应不同；应只加一次。
- 重复同一遥测：首次成功，重复返回 `ORDER_STATE_CONFLICT`；应重放首次 ACK。
- 两次遥测累计 `0.75 kWh / 113 分`，停止后变为 `1 kWh / 100 分 / 3600 秒`。
- 本次开始、结束时间均为 `2026-09-06T03:04:49`，缺少合同要求的 `+08:00`。此前真实 Qt UserApi 拒收证据见[原检查报告](server-progress-2026-09-06.md)。
- 健康回包五字段正确；`degraded`、`forecastRunId=null` 是当前核心无预测数据的允许状态。
- 数据库完整性 `ok`，6 站 48 桩，request_log 迁移后五字段。探针自建服务进程已停止。

诊断目录：`/home/hushengyuan/.cache/ev-core-final-probe-T4Qm07`。结构化观察见[修前 TCP 观察](evidence/core-integration-2026-09-06/tcp-observation.json)，复现命令见[探针说明](evidence/core-integration-2026-09-06/README.md)。探针退出码 0 仅表示运行结束，**不是业务通过断言**。

探针不是完整三端窗口、实际模拟器线上编码、SQL 故障原子性或腾讯在线导航验收。本轮没有消耗腾讯额度。

## 黄金库与后续

封存黄金库 SHA-256：`5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7`。导入前后校验一致；只在额外运行副本上启动服务和迁移，不改封存原件。

下一批按[核心联调计划](../superpowers/plans/2026-09-06-core-integration-next.md) Task 2–3 修复上述 P0、模拟器毫秒编码/状态同步、管理窗口同库启动与数据复位流程，再以真实 UserApi、模拟器和服务端复测。团队问题清单见[交接单](../management/core-integration-handoff-2026-09-06.md)。没有完成修复前失败/修复后通过及后续彩排，不能关闭发布门槛。
