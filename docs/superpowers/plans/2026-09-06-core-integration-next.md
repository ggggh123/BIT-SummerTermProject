# 核心三端联调与展示收口 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 先消除已复现的核心联调阻塞，再用同一版本完成真实三端闭环与两次答辩彩排。

**Architecture:** Qt 管理/服务端是唯一运行时 SQLite writer，用户端和设备模拟器通过冻结的长度前缀 JSON/TCP 协作。以独立集成分支、受校验黄金库的运行副本和真实通信验证为边界；客户端保持既定手机式 UI 方向。服务端和模拟器修复分别评审，联调通过后再汇入共享发布路径。

**Tech Stack:** Ubuntu、C++17、Qt 6.2+、CMake 3.22+、Ninja、SQLite、Python；用户端腾讯地图 Web API，Node 仅用于已有地图 HTML 回归。

**Spec:** `docs/test/server-progress-2026-09-06.md`；`docs/superpowers/specs/2026-09-04-core-scope-rebaseline-design.md`；`docs/design/interface-contract.md`。

## Global Constraints

- 最终截止：2026-09-10。
- 团队资源集中到用户端、管理/服务端、数据库/模拟器组成的可运行核心闭环。
- 原始需求附件保持只读，不因内部范围调整而篡改。
- 统一云端协作路径为 `feat/* -> dev -> main`。
- 无人工直接修改运行时数据库。
- 金额一律为整数分（fen）。
- `Timestamp` 是有效 ISO 8601 字符串，必须显式带 `+08:00`，形式为 `YYYY-MM-DDTHH:mm:ss[.fraction]+08:00`。
- 本计划不自动读取、检查、编辑、导出、渲染或验证需求矩阵。
- 不删除 Web/ML 成果，不把预测或 Web 大屏重新加入核心门槛。
- 本机已有 UI 修改必须保留；首次检查仅授权检查与规划，随后获准 Task 1。本轮追加授权已允许本机直接介入服务端、数据库和模拟器，按以下 Tasks 2–3 的既定缺陷与验收要求推进；共享分支推送、发布或合并仍单独过门槛，不改写队友远端分支。

---

## 计划层级与时间安排

本文件是团队集成与验收计划，不替代 #2、#4 各自修复 PR 的细化实现设计。它给出已定位文件、精确输入、验收断言和串联顺序；业务修复按各自负责模块实施，不让本机无声接管队友工作。

| 建议日期 | 主要产出 | 不可省略的通过条件 |
|---|---|---|
| 9 月 6 日 | 保护 UI 成果、组装集成基线、关闭时间戳与计费主阻塞 | 实际 Qt 可登录和预约；停止不覆盖遥测金额 |
| 9 月 7 日 | 幂等/事务、模拟器同步及真实主链路 | 充值不重复、订单/余额/管理统计一致 |
| 9 月 8 日 | 第二批 UI 收口、故障路径、受控复位 | 充电/导航/结算视觉统一；三端能从黄金库重启 |
| 9 月 9 日 | 冻结候选版本、两次完整彩排与证据 | 同一 SHA、同一黄金库哈希，两轮均通过 |
| 9 月 10 日 | 按已彩排版本交付与答辩 | 不临时合入未经回归的新功能 |

以上是工作顺序建议，不是已取得的进度，也不是自动排程承诺。任何前置门槛未通过，都先修闭环，压缩装饰性扩展。

## Task 1：保护本机成果并建立唯一集成基线

**负责人：** #3 与 #4，#2 确认服务端可评审提交。

**Files:** 本机 `apps/user-client/` 已有 UI 改动；顶层 `CMakeLists.txt`；`apps/admin-server/`；`simulator/`；`runtime/golden/core.db` 及配套 manifest/checksum。不得包含需求矩阵。

**Interfaces:** 消费服务端 `661c91e`、数据 `10034fd`、dev `e21c73a` 和本机 UI 工作；产出一个可以记录 SHA 的独立集成候选版本，不使用多台机器各自不同的 HEAD 作为共同验收版本。

- [x] 在执行获授权后，逐文件复核并将现有 UI 成果形成独立提交；不得使用 `git add .` 将无关材料一起收走。新文档、图片及测试一并按 UI 范围核对。
- [x] 再次 fetch 后检查三个功能来源是否前进。保存每个来源 SHA，不能用本报告里的 SHA 代替执行时检查。

```bash
git fetch origin
git rev-parse HEAD origin/dev origin/feat/server origin/feat/data
git diff --name-only origin/dev...origin/feat/server -- apps shared tests CMakeLists.txt
git diff --name-only origin/dev...origin/feat/data -- simulator database scripts tests
```

- [x] 使用独立 worktree 组装诊断/集成分支，保持本机用户端工作区和共享 main/dev 不被未通过的集成覆盖。先检查重叠文件再合并，不以强制覆盖解决冲突。
- [x] 在原生 Linux 构建目录运行全量门槛，并保存 SHA、命令、输出和失败项。以下从该集成 worktree 根目录运行；先用 `mktemp` 生成独立构建目录，变量只属于当前终端。

```bash
integration_build=$(mktemp -d /home/hushengyuan/.cache/ev-core-integration-build-XXXXXX)
cmake -S . -B "$integration_build" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$integration_build" -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir "$integration_build" --output-on-failure -j4
node --test apps/user-client/tests/test_navigation_html.mjs
```

**验收：** 构建与测试都针对同一组源码；不得沿用本次服务端 19/19 作为加入新模拟器/UI 后的结果。此任务只建立候选基线，不据此宣布发布。

**2026-09-06 执行记录：** UI 归档 `a8ef5f0`、测试同步 `48ee40c`；来源复核未前进，独立集成工作区已建。干净组合 CTest 20/20、地图 15/15、数据库 9/9、高 DPI UI 16/16；真实 TCP 仍复现三个 P0。详细修前基线保存在集成分支的 `docs/test/core-integration-baseline-2026-09-06.md`。

## Task 2：关闭真实客户端和计费/重放阻塞

**负责人：** #2 修复，#3 验证客户端解码，#5 验证跨服务行为。

**Files:** `apps/admin-server/src/services/AuthService.cpp`、`UserService.cpp`、`TelemetryService.cpp`、`RequestLogService.cpp`、`apps/admin-server/src/network/ApiServer.cpp`、`apps/admin-server/src/db/DatabaseManager.cpp`；回归落点 `tests/admin-server/tst_user_flow.cpp`、`tst_telemetry_service.cpp`、`tst_request_log.cpp` 和新增真实 TCP 集成测试。客户端对照 `apps/user-client/src/services/UserApi.cpp`、`src/domain/ContractTimestamp.cpp`，不放宽已有规则。

**Interfaces:** 输入真实 `auth.user_login/charge.reserve/charge.start/telemetry.push/charge.stop/charge.settle/wallet.recharge` 请求；输出冻结五字段 ResponseEnvelope 和契约一致的 User/Order 对象。

- [ ] 先让回归复现报告的红灯：黄金库用户预约 `INVALID_RESPONSE`、新用户登录 `INVALID_RESPONSE`。检查 `registeredAt/reservedAt/startedAt/endedAt/updatedAt/acceptedAt`，在本机时区和 UTC 进程环境下都要求显式 `+08:00`。
- [ ] 修复后使用实际本机 `UserApi/TcpJsonClient` 再连接服务端，要求新老用户均正常登录、预约成功收到 `chargeOrderChanged`，不产生 `uncertain=true`。检查真实回包，不能只修测试 fixture。
- [ ] 新订单按 150 分/kWh 站点注入两次遥测，增量依次为 0.25 和 0.50。停止前后断言均为 `energyKwh=0.75`、`amountFen=113`；`elapsedSec` 来自开始/停止时间，不是固定 0/3600。
- [ ] 同一 `requestId` 重发充值 100 分：余额只增加 100；同一遥测请求只累加一次；同一结算请求只扣款一次；每组重复请求取得相同首次 ACK，设备 cursor 不先于重放检查。
- [ ] 注入 SQL 失败/锁冲突：业务不得部分成功后返回 OK；余额、订单、桩状态和已保存响应必须一致。失败必须使用合同错误域，不直接返回 SQL 文本。
- [ ] 单项红灯转绿后，再运行全部相关回归，不只运行新用例。

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir "$integration_build" \
  -R '^(admin_auth|user_flow|telemetry_service|request_log|user_api|user_tcpjsonclient)$' \
  --output-on-failure
```

**验收：** 三个 P0 各有“修复前失败、修复后通过”的真实证据；服务端修复以独立评审提交交接。测试通过不授权本计划执行者越过团队合并流程。

## Task 3：使模拟器、黄金库和管理窗口使用同一运行状态

**负责人：** #4 主负责模拟器/数据，#2 负责服务端 GUI 配置和管理端状态，#5 验证。

**Files:** `simulator/src/core/TelemetryEngine.cpp`、`simulator/src/net/SimulatorClient.cpp`、`simulator/src/ui/SimulatorWindow.cpp`、`simulator/tests/tst_simulatorclient.cpp`、`tst_telemetryengine.cpp`；`database/build_golden.py`、`database/README.md`；`apps/admin-server/src/main.cpp`、`src/ui/MainWindow.cpp`、`src/services/DashboardService.cpp`。

**Interfaces:** `simulator.status` 返回权威桩列表；`telemetry.push` 和 `simulator.fault_set` 共用每桩时间游标；管理窗口和网络服务必须由同一 AppContext 使用同一运行数据库。

- [ ] 新增线上编码回归：两个时间分别为 `.001+08:00` 和 `.002+08:00` 的设备事件，经过实际发送/解码后仍严格递增，不能都变成整秒。覆盖遥测、故障、恢复及断线队列重发。
- [ ] 验证初始暂停 → 运行 → 暂停的实际服务端状态；默认空 token 必须识别为鉴权失败，不将“已连接”误记为业务接入成功。
- [ ] 验证用户预约/开始/停止、管理重启之后的权威桩状态同步。首轮临时使用手动刷新时写入记录；最终演示流程中的自动/显式刷新必须明确且稳定。
- [ ] 修正或明确 GUI 参数入口：现在传 `--db` 会进入无界面模式。必须能启动使用指定黄金库运行副本的管理窗口；不并行启动第二个占用 9100 的服务端，不让管理窗口显示另一数据库。
- [ ] 校验核心黄金库哈希与 manifest；重建时输出到新目录，分别保留 core/demo 的 manifest，不覆盖旧成果。运行副本启动前校验，启动迁移后不再要求运行文件哈希等于封存文件。
- [ ] 复验管理端月营收按自然月统计，故障/充电/结算后页面取得新的权威数据。若没有告警逻辑，界面和答辩不得宣称已实现实时告警。

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir "$integration_build" \
  -R '^(simulator_engine|simulator_client|simulator_window|admin_service|dashboard_service)$' \
  --output-on-failure
```

**验收：** 快速故障/恢复不因毫秒丢失而被误拒；正在充电的桩在模拟器中不会继续当作 idle；管理窗口展示的状态与用户当前订单属于同一运行库。

## Task 4：完成真实主线、异常路径和第二批 UI

**负责人：** #3 用户端与腾讯地图，#2/#4 配合，#5 记录证据。

**Files:** `apps/user-client/src/ui/ChargePage.cpp`、`NavigationPage.cpp`、`HistoryPage.cpp`、`ProfilePage.cpp`、`UiTheme.cpp`；对应 `apps/user-client/tests/`；新增 `docs/test/core-integration-2026-09-07.md` 记录实际执行时间、版本、步骤、结果与截图。

**Interfaces:** 使用 Tasks 2–3 通过的真实服务端与模拟器，不用协议 mock 代替业务联调。地图仍通过本机已配置的腾讯 Web API，不在日志/文档中输出 Key。

- [ ] 从新黄金库运行副本启动唯一管理/服务端，再启动用户端与模拟器。模拟器连通示例：

```bash
"$integration_build/simulator/ev_charger_simulator" \
  --host 127.0.0.1 --port 9100 --token sim-token
EV_SERVER_HOST=127.0.0.1 EV_SERVER_PORT=9100 \
  "$integration_build/apps/user-client/ev_user_client"
```

两条命令在不同终端运行；Qt 管理窗口启动方式先由 Task 3 验证，不套用当前会隐藏 GUI 的参数组合。腾讯在线验证必须显式运行并记录，默认回归不自动消耗额度。

- [ ] 正向链路逐步核对：登录 → 查站查桩 → 腾讯驾车/步行导航 → 预约 → 开始 → 模拟器产生至少两次遥测 → 停止 → 结算 → 余额/订单/管理营收同步。
- [ ] 以订单 ID 串联证据；停止前后电量和金额连续，结算余额差等于订单金额；历史新增一条完成订单，桩按当前权威状态释放或保持故障，不用人工 SQL 修补。
- [ ] 异常链路分别验证：重复充值/结算、冻结账户、余额不足、断线后查询恢复、预约中故障、充电中故障、故障重启但旧订单未结算时禁止再次占用。不存在的桩和被占用的桩按合同返回业务错误。
- [ ] 真实金额、站名、长订单及错误文案稳定后，再将充电/导航/结算/历史/账户页与既定浅色青绿、手机式单列主题统一。沿用已批准设计方向，不重新发散整套设计。
- [ ] 在实际 Linux 显示环境核对滚动、按钮遮挡、地图区域和状态文案，再运行全量 CTest 与地图 Node 测试。截图标注真实业务联调还是受控 UI 测试，两类证据不混用。

**验收：** 形成一条可连续展示的真实主线，以及能解释的失败与恢复路径。若 P0 重现，停止扩展 UI，回到对应负责模块修复。

## Task 5：复位、冻结版本并连续彩排两次

**负责人：** #4 版本/黄金库，#5 执行复验，#1 答辩组织，全员参与。

**Files:** `docs/test/core-integration-2026-09-07.md`；新增 `docs/test/core-rehearsal-2026-09-09.md`；必要时更新 `database/README.md` 和核心启动说明，不修改需求矩阵。

**Interfaces:** 输入经审核的唯一候选 SHA、黄金库 checksum、明确的 GUI/模拟器/客户端启动方式；输出两轮可追溯通过记录和可交付版本。

- [ ] 先演练受控复位：退出三端并确认服务进程结束；校验黄金库；生成新的运行副本；按 Task 3 的入口启动；确认 6 站/48 桩与初始订单、余额一致。保留上一轮运行库供排查，不覆盖仍被 SQLite 打开的文件。
- [ ] `demo.reset` 已由合同第 27 条定义，但当前实现缺失；不得将未知动作响应当作合同未定义，也不得用新 action 替代它。人工冷启动复位可作为明确记录的首版操作流程，但不能关闭在线 reset 合同缺口或宣称一键自动恢复已完成。
- [ ] 冻结候选 SHA；两轮从同一黄金库重新开始，执行 Task 4 主线和选定故障路径。每轮记录开始/结束时间、SHA、黄金库哈希、订单 ID、金额核对和截图。
- [ ] 两轮均通过、开放 P0 为零后，再按评审流程执行功能分支到 dev、dev 到 main；确认 main 的最终 SHA 与经过门槛的发布版本一致。
- [ ] #1 整理答辩讲解：真实模拟数据来源、Qt 手机式交互、腾讯地图调用、三端状态联动、数据库一致性和恢复过程；不把保留但未启用的 Web/ML 描述为当前在线能力。

**验收：** 同一版本连续两次通过，没有运行时手工改库，没有无法解释的金额/状态矛盾；发布材料与代码、数据版本一致。

## 本机建议立即执行的下一小步

Task 1 已完成。用户已明确扩大授权，由本机按 Tasks 2–3 补交核心阻塞修复；模块归属和来源保留。先处理时间戳、计费、幂等/事务及模拟器线上时间与同步，之后在同一集成版本验证真实主线，再推进第二批 UI 和彩排。不得把本机诊断成功写成业务修复成功。
