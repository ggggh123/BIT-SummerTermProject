# 服务端最终修复限定复审（2026-09-06）

归档范围：`604c8c2..3ca7e4b`。以下保留独立复审原判定；后续只归档文档，未修改被审源码。

**I1：满队列拒绝覆盖冻结的认证／payload／业务错误优先级** — **NOT ADDRESSED**（部分修复）。`apps/admin-server/src/app/AppContext.cpp:83` 已在容量判定前复用身份和基础参数预检；`apps/admin-server/src/services/RequestPreflight.h:23`、`:50` 保证匿名／未知 token、错误角色及无效 confirmation 分别得到 AUTH_REQUIRED、FORBIDDEN、INVALID_REQUEST。`tests/admin-server/tst_server_threads.cpp:269` 的确定性 256 项填充用例覆盖这些修复，并保留未知 action、合法请求忙和缓存 health 的断言。但 `AppContext.cpp:87` 仍在 DB worker 的实体／业务检查及 forecast 整批语义检查之前返回 SERVER_BUSY；正整数但不存在的 stationId、非 fault 的已知 charger、类型正确但不足 144 条的 forecast 均仍可触发原合同偏差。`docs/design/interface-contract.md:140`、`:141`、`:142` 没有新增容量例外。用户尚未批准例外，有限方案 A 和绿色回归不能关闭整项 I1。

**M1：锁等待测试 timer 上下文长于捕获局部对象** — **ADDRESSED**。`tests/admin-server/tst_database_worker.cpp:46` 的局部 `timerScope` 声明在 lock、elapsed、uiTick 之后，两个 singleShot 在 `:47`、`:48` 均使用它；正常离开及断言早退均先析构上下文、取消回调，再销毁捕获对象。该结论只关闭本次 timer 捕获缺陷，不用于判断历史被中止的异常诊断。

**M2：重启 ACK 后立即关闭／首次 ACK 保存失败后无延迟写入缺组合断言** — **ADDRESSED**。`tests/admin-server/tst_server_threads.cpp:179` 在真实 TCP 成功 ACK 后立即 shutdown，并在关闭后直接检查 idle、恰一条 done event 和逐字节相同的 request_log ACK（`:191`、`:193`、`:195`）。`:200` 用 request_log INSERT trigger 使首次 ACK 事务失败，撤销 trigger 后单次等待 1700 ms，再直接检查 fault、零启动／完成事件、零 ACK 和 snapshot version 0（`:215` 至 `:222`）。这些是状态断言，不只是等待或“未崩溃”。

**M3：旧 pending 穿过新 reset 后跨进程恢复／GUI ACK 失败后的 ID 复用缺组合断言** — **ADDRESSED**。`tests/admin-server/test_tcp_demo_reset.py:163` 按旧 pending → 新 reset → 充值 → 进程停止／启动 → 旧 pending 恢复执行，并验证旧 receipt 元数据、final 状态、新余额、新日志、version=2、快照原 bytes 和最终 ACK 重放（`:179` 至 `:187`）。`tests/admin-gui/tst_admin_window_refresh.cpp:97` 使用真实窗口和 ACK INSERT trigger；失败后检查按钮恢复及 ID 保留（`:130` 至 `:136`），再次确认时 ID 相同，仅成功后清空（`:151`、`:152`），同时核对唯一 final receipt、稳定元数据、新充值余额、版本及唯一成功 reset 日志（`:153` 至 `:161`）。

### New Breakage in the Fix Diff

- **N1（Important）：forecast 的负整数 horizonH 被新增预检改写为 INVALID_REQUEST，正常负载也改变原业务错误域。** 位置：`apps/admin-server/src/services/RequestPreflight.h:103`；默认下界定义在 `:27`。一个其余字段合法、144 条结构完整且仅将一条 `horizonH` 改为 `-1` 的 forecast 请求，其 horizon 仍是合同允许表示的 safe integer；原 `ForecastService.cpp:122`、`:125` 将不在 1..24 的 horizon 与其他 forecast 记录语义错误一起返回 FORECAST_INVALID。新增调用 `integer(record.value("horizonH"))` 隐含 minimum=0，因而前台与 dispatcher 现在均提前返回 INVALID_REQUEST，根本不到原服务。同类越界 `0` 和 `25` 仍通过此预检并得到 FORECAST_INVALID，形成由符号决定错误域的不一致。依据 `docs/design/interface-contract.md:68` 的 signed safe integer、`:273` 的 horizon 1..24、`:140` 的 forecast 有效类型记录语义专属域及 `:518` 的逐 action 错误顺序；这不是满队列尚未执行业务校验的 I1 剩余。修复报告声称保持 forecast 业务语义错误域，当前这一处尚不成立。建议这里只检查完整 signed safe integer，沿用与 predictedBusyCount／predictedIdleCount 相同的纯类型边界，将 horizon 1..24 交由 FORECAST_INVALID 判定；对 `-1/0/25` 添加有限一致性断言。该发现由明确控制流和原／新差异直接确定，本复审未为它运行新 suite。

### Out-of-Scope Observations

- 无新增越界观察。未重新审查整个分支，未接续历史中止诊断；本报告没有把已有未证实异常或正式联调／换机／双彩排未完成项升级为本修复的新缺陷。

### Checks

- 完整读取限定复审模板、原 final-review.md、task-1-brief.md、task-2-brief.md 和归档修复报告；按连续六个区段完整读取 `review-final-fix-wave.diff`，共 1,332 行、94,175 bytes、3 个提交，范围为 604c8c2 → 3ca7e4b；未重新生成 git diff。
- 核对 `AuthService.cpp:120` 和 `services/TokenRoles.h:5`：发布内容是 QHash<QString,QString> 值，未传递 SQL 句柄或服务指针。`DatabaseWorker.cpp:100` 的 tokenRolesChanged 先于 `:107` 的 completed；两者从同一个 DB worker 排队至同一 AppContext，`:26` 更新角色副本在 `deliver` 发出登录回调之前完成。前台和 worker 均调用 RequestPreflight，内部 AdminView 只调用 authorize，未错误要求 dashboard rangeDays。未知非空 token 不再借匿名登录权限通过，health 仍按合同放行。
- 核对新增预检只读取 action 声明字段，不因额外未知字段本身拒绝请求；DemoResetService 的 confirmation 判定只是提取到共享函数，receipt 查询和 final 原 bytes 返回路径 `DemoResetService.cpp:142`、`:158` 保留。合法相同 payload 的 mutation 仍走 `RequestLogService.cpp:127` 的已存 ACK 查询、`:136` 的原 bytes 返回，预检不执行 cursor 或业务状态判断。除 N1 的 forecast 负 horizon 错误域外，本轮没有确认预检新增的其他破坏。
- 对照修复 diff 的删除／添加正文，`BusinessTime::timestampKey` 的 regex、QDate/QTime 整数部分校验、只去小数末尾零及字符串键构造与原 TelemetryService 算法一致；`TelemetryService.cpp:20` 仅改为 using 共享函数。未引入 QDateTime 小数解析、浮点转换或精度截断。修复报告保留初次 27/29 的真实 Timestamp 回归及修后 UTC／上海 2/2，没有把失败擦除。
- 已核对修复报告列出的定向测试名称、命令、QtTest／unittest 输出计数及其对应新增断言；最终全量结果由 controller 在实现 76ff1de 上运行，文档记录 build exit 0、CTest 29/29（38.29 秒）、数据库 pytest 15/15（1.11 秒）、黄金 hash 未变。本复审未独立重跑这些 suite，绿色结果不代替 I1／N1 的合同判断。可选 XKB/Cups 和 offscreen 告警仍被如实记录。
- 仅做限定文本静态读取，并用 apply_patch 写入本报告；没有改代码、index、HEAD、分支或黄金库，没有读取工作簿、调用腾讯、输出凭据、派生代理、推送或合并。

### Verdict

**Fix round: Findings remain open。** M1、M2、M3 已关闭；I1 仍为 NOT ADDRESSED（已修认证和基础参数分支，权威业务及剩余 forecast 整批语义的容量优先级仍未关闭）；本修复另引入 N1 Important（负整数 horizonH 的错误域改变）。最终剩余清单为 **I1、N1**，两项互相独立。当前不能宣布本批整体 Ready to merge；这一结论也不授权继续新修复波次或改冻结合同。
