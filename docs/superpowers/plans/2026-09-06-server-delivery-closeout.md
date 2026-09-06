# 服务端线程归属与在线复位收尾 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在已合并的三端基线上完成真实 DatabaseWorker、管理/网络统一调度和合同在线复位，交付可回归的服务端候选。

**Architecture:** 沿用已确认的管理 UI 主线程、网络连接工作线程、唯一 DatabaseWorker 线程。已有业务服务保留并由 worker 创建和调用，UI 和 socket 不持有数据库服务指针。在线 reset 使用同一串行队列与持久化 receipt，禁止替换运行中的数据库文件。

**Tech Stack:** C++17、Qt 6.2+ Core/Network/Sql/Widgets/Charts/Test、CMake/Ninja、SQLite、Python pytest。

**Spec:** `docs/design/interface-contract.md`；`docs/superpowers/specs/2026-09-04-core-scope-rebaseline-design.md`；既有 `docs/superpowers/plans/2026-09-01-admin-server.md` Tasks 1–2、5、7 的尚未实现部分。2026-09-06 用户已批准按整项目交付路线继续实施，本计划细化其中服务端工作线，不另行改变协议或产品范围。

## 执行状态（2026-09-06）

Task 1 已以 `47bcdae` 完成并通过独立任务审查；Task 2 已以 `876edaa` 实现，`91e87be` 修复默认库路径预检缺口后通过限定复审。两任务组合重新构建及 CTest 29/29 通过。下方保留开工时的步骤清单，完成证据及最终整分支审查状态以[服务端收尾验证记录](../../test/server-delivery-closeout-2026-09-06.md)为准；不据此宣布整项目 GO。

## Global Constraints

- 基线 `origin/dev@97c6da13adb3b50503091dab9456fa704728771a`，截止 2026-09-10。
- 用户端、管理/服务端、数据库/模拟器为核心；Web/ML 保留但不是核心启动或发布前置条件。
- 不读取、检查、编辑、导出、渲染或验证任何需求矩阵工作簿；Git 广泛检查排除所有 xlsx。
- 不改写原始附件、黄金库或队友分支；不推送或合并共享分支。
- 金额一律为整数分（fen）。订单金额为 `qRound64(energyKwh * priceFenPerKwh)`。
- `Timestamp` 必须显式带 `+08:00`，形式为 `YYYY-MM-DDTHH:mm:ss[.fraction]+08:00`。
- Qt 管理/服务端是运行期 SQLite 的唯一 writer。所有业务 mutation 和 request_log 幂等记录在串行 DatabaseWorker 上执行；UI、socket worker、timer 不直接写 SQLite。
- SQLite 启用 foreign_keys、WAL 和 3000 ms busy_timeout；连接只在所属线程创建、使用和关闭，不跨线程传递 QSqlDatabase。
- 冻结 v1 action/schema/权限/错误域不删除、不改名、不缩窄；无 active forecast 合法，health degraded + ok=true 不判核心失败。
- 同 requestId 的同一变更重放返回首次保存的 ACK bytes；金额、订单、设备事件与 ACK 的原子性不得退化。
- 所有新行为先有失败测试；报告写中文，记录实际命令、结果及限制；不调用腾讯 API、不输出任何密钥。

---

## Task 1: 将现有服务迁入线程隔离的统一调度器

**Files:**
- Create: `apps/admin-server/src/db/DatabaseWorker.h/.cpp`，负责唯一数据库及服务生命周期、串行命令与管理内部只读视图。
- Create: `apps/admin-server/src/network/ConnectionWorker.h/.cpp`，负责线程内 socket/framing，生命周期与请求关联。
- Create: `apps/admin-server/src/services/RequestDispatcher.h/.cpp`，从 ApiServer 移入现有分发/ACK逻辑，复用服务，不复制业务 SQL。
- Modify: `apps/admin-server/src/app/AppContext.h/.cpp`、`src/network/ApiServer.h/.cpp`、`src/db/DatabaseManager.h/.cpp`、`src/services/AdminService.h/.cpp`、`src/ui/LoginDialog.h/.cpp`、`src/ui/MainWindow.h/.cpp`、`src/main.cpp`、对应 CMake。
- Test: `tests/admin-server/tst_database_worker.cpp`、`tests/admin-server/tst_server_threads.cpp`、`tests/admin-gui/tst_admin_window_refresh.cpp` 及现有相关测试。
- Document: `apps/admin-server/README.md`。

**Interfaces:**
- Consumes: 现有 RequestEnvelope / ResponseEnvelope、Auth/Admin/Dashboard/Forecast/RequestLog/Telemetry/UserService，保持线上格式。
- Produces: AppContext 的异步本地请求接口，形式 `executeLocal(RequestEnvelope, QObject *receiver, std::function<void(QByteArray)> callback)`；所有 GUI mutation 均使用该接口和真实 admin token。
- Produces: 如管理日志/聚合表需要额外内部只读视图，可使用有闭合枚举和 admin 校验的队列查询；不能添加未批准 TCP action，也不能暴露数据库或服务指针。
- Produces: DatabaseWorker 的异步请求/响应连接、启动结果、数据库路径及不可变健康快照；健康请求读取缓存并生成当前 serverTime，不逐次查 SQL。
- Network: 每个连接 worker 在自己的 QThread 内创建 QTcpSocket，最多 16 个连接；超额以 SERVER_BUSY 明确拒绝。listener 及 UI 不执行 SQLite。
- Lifecycle: 停止接收新请求 → 停止/清理归属连接 → 排空已接纳 DB 命令 → 在 DB 线程销毁服务/关闭并移除唯一命名连接 → 退出线程；不使用 terminate，不让晚回包访问已销毁 UI/socket。

- [ ] **Step 1: 测试先复现现有线程耦合。** 新增真实 AppContext 测试：用单独测试连接对临时 DB 持有 `BEGIN IMMEDIATE`，发送一个充值命令等待 DB；主线程 QTimer 必须继续触发，独立 health 请求必须在数据库等待期间返回合法缓存对象。生产中的 UI/socket 同步 SQL 将导致此测试失败。测试锁连接仅用于注入故障，不能成为生产入口。

```cpp
QElapsedTimer elapsed;
elapsed.start();
QTimer::singleShot(30, &receiver, [&] { uiTickMs = elapsed.elapsed(); });
// sendRecharge(token, 100) 与 sendHealth() 使用真实 TCP fixture。
QTRY_VERIFY_WITH_TIMEOUT(healthReply.has_value(), 500);
QVERIFY(uiTickMs >= 0 && uiTickMs < 500);
QCOMPARE(healthReply->code, QStringLiteral("OK"));
// 释放测试锁后，核对充值一次、同 requestId 原始响应逐字节重放。
```

- [ ] **Step 2: 记录 RED。** 构建新增目标并运行针对测试，确认失败来自阻塞/缺少线程接口，不得将拼写或环境错误作为 RED。
- [ ] **Step 3: 最小线程迁移。** worker 在自己的线程中创建 DatabaseManager 和所有服务；将 dispatch/handleRequest 原逻辑移入 RequestDispatcher。DB 使用唯一连接名与确定性 close；独立单元测试仍可在线程内创建原服务。
- [ ] **Step 4: 迁移网络和 UI。** 网络采用排队的 request/response；本地 UI 登录、创建站点、冻结和重启走同一 dispatcher、token、requestId 与 request_log。保留筛选、分页和稳定 ID 选择语义；忙时禁用受影响动作，异步失败显示可解释错误，已销毁 receiver 自动丢弃回调。
- [ ] **Step 5: 收口线程相关约定。** 消费 shared Permissions/Actions 区分 AUTH_REQUIRED 和 FORBIDDEN，未知 action 优先 INVALID_REQUEST。重启 1500 ms 后由 worker 的确定性任务事务完成，定时器只排队命令。取消/失败事务不能留下错误定时写入。关闭后的回调不能访问 QObject。
- [ ] **Step 6: 验证行为。** 新增帧拆/粘包、无效帧、容量、断开时尚有排队命令、重复请求、重启、两个独立上下文连接隔离与正常退出验证；对 timer/DB 生命周期只运行正常功能测试，不接续此前被工具限制中止的诊断。保持既有 UTC/上海及 core_workflow 测试通过。

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -R 'database_worker|server_threads|admin_window_refresh|core_workflow|server_tcp_p0' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
```

- [ ] **Step 7: 写运行说明与提交。** 说明线程图、CLI 与关闭行为、测试证据和未实施 reset 状态；精确暂存本任务文件，提交并交独立任务评审。

## Task 2: 合同在线 demo.reset 与恢复凭证

**Files:**
- Create: `apps/admin-server/src/services/DemoResetService.h/.cpp`。
- Modify: Task 1 的 DatabaseWorker、RequestDispatcher、AppContext、管理 UI、main CLI 与对应 CMake；必要的 ForecastService/SnapshotWriter 集成。
- Test: `tests/admin-server/tst_demo_reset.cpp`、真实 TCP 测试；运行说明。

**Interfaces:**
- Consumes: Task 1 异步统一队列；`demo.reset`、admin token、payload `{confirmation:"RESET_DEMO"}`。
- Config: 显式 `--golden` / `--golden-hash` 配对；默认仓库封存 `runtime/golden/core.db` 与核验过的 core checksum。运行库和黄金原件不得同路径/同文件；黄金库有 WAL/SHM/journal 或 hash/integrity/FK/schema 不通过均在事务前拒绝。
- Success: `{resetAt:Timestamp,goldenHash:string}`；无预测不是失败；snapshot 写失败返回 OK + 中文警告，不能宣称事务回滚。
- Receipt: SQLite 内部表保存 requestId、actor、有效 payload 身份、state=pending|final、稳定 resetAt/goldenHash/snapshotVersion、最终 ACK bytes；后续不同 reset 不删除已保存 reset receipts。查 receipt 早于清表；pending 只续 snapshot/ACK，final 逐字节重放。
- Explicit table set: `admins,users,stations,chargers,orders,telemetry,station_hourly_history,forecast_runs,forecasts,events`；schema_version 仅校验；旧 request_log 清理；snapshot_meta.version 从运行库版本递增，不回退为黄金库版本。

- [ ] **Step 1: 添加失败测试。** 对真实 admin reset 断言黄金哈希、恢复到 6 站/48 桩/30 用户、旧业务写入移除、request_log 清理、version 增长；当前未知 action 响应必须使测试失败。

```cpp
const QByteArray first = request("demo.reset", adminToken,
    {{"confirmation", "RESET_DEMO"}}, "reset-one");
QVERIFY(parse(first).ok);
QCOMPARE(parse(first).data.toObject().value("goldenHash").toString(), approvedHash);
rechargeExistingUser(100);
QCOMPARE(request("demo.reset", adminToken,
    {{"confirmation", "RESET_DEMO"}}, "reset-one"), first);
QCOMPARE(existingUserBalance(), goldenBalance + 100); // 重放不能再清一次库。
```

- [ ] **Step 2: 实现核心事务。** 开始前校验封存输入并只读 ATTACH；按子到父删除、父到子插入显式表，外键保持开启；清理旧日志、递增 version、记录 pending receipt 在同一个 BEGIN IMMEDIATE 中完成；任何失败全部回滚。
- [ ] **Step 3: 实现提交后阶段。** 用对应 version 尝试 SnapshotWriter；失败保留 last-good 并安排版本受控重试；第二个串行事务保存最终 ACK、转 final、写本次 request_log；已提交核心不可在错误路径再次执行。复位时清除失效的重启任务/内存设备运行状态；不得让旧回调改写新黄金状态。
- [ ] **Step 4: 新增异常和恢复测试。** 验证匿名/用户/模拟器被拒、confirmation 缺失/错误类型/大小写不符被拒、坏哈希和 SQL trigger 强制失败不改库/receipt/snapshot、snapshot 输出不可写仍成功并重放稳定、final ACK 写失败后的 pending 恢复不重复清库、重启进程后 final/pending receipt 保持语义。注入通过测试 SQLite trigger/临时文件实现，不新增生产测试开关。
- [ ] **Step 5: 增加管理 UI 确认入口。** 明确将丢失当前演示业务状态；确认后异步调用同一 admin action，成功刷新视图；失败显示原因，禁止绕过协议用文件复制。
- [ ] **Step 6: 全量回归并提交。** 构建、全部 CTest、数据库 pytest；核验黄金原件未变，写实际中文证据，不宣称正式人工彩排或腾讯在线已通过；提交独立任务评审。

## 本计划完成边界

本计划交付服务端收尾候选。客户端第二批 UI、启动/停止/冷复位脚本、另一台机器运行、腾讯在线与两次人工正式彩排仍在整项目路线中继续执行；不得因服务端测试通过就签 GO。远端推送、PR 与共享分支合并按用户授权另行执行。
