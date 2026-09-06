# 服务端最终评审：唯一修复波次报告

日期：2026-09-06。修复基线 `604c8c21c379965183b92269340e6f3431a5e32c`。实现与测试提交：`76ff1dee62f4d3c268f2e671ceca8303742334cb`（18 个精确文件，414 additions / 32 deletions）。本报告随后作为独立文档提交，不改变上述已验证源码。

本轮完整读取 final-review.md、task-1-brief.md、task-2-brief.md，并核对冻结合同第 2、3、5、6 节及相关逐 action 条款。使用 receiving-code-review、TDD、verification-before-completion；遇到新 Timestamp 回归后使用 systematic-debugging。未派生代理，未重新执行整份计划。

**结论：I1 仅部分修复，仍有真实 Important 未关闭；M1、M2、M3 已处理并补充直接证据。不能据此宣布 Ready to merge。** Controller 已裁决本波次采用有限方案 A：修身份及可共享基础参数，不引入完整业务状态镜像，不改冻结合同、不添加容量例外。用户对合同边界尚无明确批准。

## 发现处理结果

| 发现 | 本波次处理 | 状态 |
| --- | --- | --- |
| I1：容量拒绝先于鉴权和 payload | worker 发布会话角色值副本；入口与 worker 复用 RequestPreflight，满队列匿名 reset 返回 AUTH_REQUIRED，有效 user/simulator 返回 FORBIDDEN，admin 缺失/非 string/大小写错误 confirmation 返回 INVALID_REQUEST。保留未知 action、合法请求 SERVER_BUSY、缓存 health 和 16/256 容量。 | 部分修复；权威业务检查与部分 forecast 整批语义仍可能被容量错误覆盖，详见下节 |
| M1：singleShot 捕获局部对象早退生命周期 | 在 lock、elapsed、uiTick 之后声明局部 QObject timerScope；两个 singleShot 都使用该上下文，早退先取消回调再销毁捕获对象。 | 已修；原锁等待测试通过 |
| M2：重启 ACK 后关闭；首次 ACK 事务失败无延迟写 | 各新增一次普通功能测试。成功 ACK 后立即调用 shutdown，退出后检查 idle、唯一 done 事件、request_log 原 ACK；用临时库 trigger 让首次 ACK INSERT 失败，移除 trigger 后单次等待 1700ms，检查仍为 fault、启动/完成事件均为 0、无 ACK、version 未增加。 | 已覆盖；不含压力或旧诊断 |
| M3：旧 pending 穿过新 reset 和进程重启 | TCP 新用例：旧 reset ACK 失败留下 pending → 新 reset 成功 → 充值 321 分 → 进程正常停止/启动 → 旧 pending 完成。检查新业务、version=2、旧 resetAt/hash/version、现有 snapshot bytes 均保持，旧 final ACK 逐字节重放。 | 已覆盖 |
| M3：GUI ACK 失败复用 ID | 用真实 MainWindow 和临时 trigger 注入 final ACK 失败；检查失败对话框、按钮恢复可用、ID 保留；写入新充值 123 分后确认重试，仍用相同 ID，仅成功后清空。检查仅一条 final receipt/成功日志、原 receipt 元数据、新余额和版本保持。 | 已覆盖 |
| 直接相关身份边界 | 原 dispatcher 将未知非空 token 的空 role 当作 anonymous，匿名登录动作可返回 OK；新增有限真实 TCP 回归并统一修为 AUTH_REQUIRED。未知 token 的 health 仍成功，正常匿名登录保留。 | RED/GREEN 已验证 |

## 准入设计与未关闭边界

AuthService 仍只属于 DB worker。它把进程内实际会话的 token→role 转成值副本，DatabaseWorker 在登录 action 完成后、completed 信号之前发布 tokenRolesChanged。由同一 sender 发往 AppContext 的排队信号保持顺序，因此前台发出登录 ACK 前已经取得会话副本。固定 simulator/ml token 的角色解析共享一个纯函数。快照不含 SQL 句柄、服务指针或业务状态，也不被输出到日志/文档；前台不会直接调用 worker 拥有的 AuthService。

AppContext 依次执行原 envelope/version、未知 action、缓存 health、身份与基础参数检查、原 256 容量判定。准入后仍由原 worker dispatcher 重验身份/基础参数，执行幂等与业务 SQL。没有新增验证命令队列、额外业务 writer 或跨线程服务调用。内部 AdminView 也先做相同管理员身份检查。连接上限 16 与待完成业务命令上限 256 的原实现保留。

共享基础校验覆盖 action 的登录字段类型、手机号、非空文本、正 safe integer、坐标、分页、状态枚举、设备 Timestamp、demo confirmation 等。forecast 只共享基础结构与 JSON 类型/safe-integer 检查；`FORECAST_INVALID` 的元数据关系、整批完整性、物理边界、站点集合及 run/hash 语义继续由原 worker 服务判定。本轮没有把这些语义统改为 INVALID_REQUEST，也没有更改合法 mutation 的原 ACK 字节重放路径。

冻结合同 §5 明确业务错误先于 infrastructure。当前结构中，worker 可能正被 SQLite 锁阻塞，256 个业务请求也已占满；入口没有权威业务状态。要判定依赖当前实体/订单/余额/设备游标的错误，就需要等待 worker 或维护额外一致性状态。额外无界验证队列、前台 SQL 和直接访问 worker 服务均违反已给约束；完整一致性内存业务模型会扩大本轮架构范围。Controller 因此明确选择先部分修复并保留 Important，不允许用陈旧业务快照冒充权威检查。

具体剩余例子由当前控制流可直接推出，未被改成“正确预期”：

- 有效 user token、`station.detail`、`{stationId:999999}`，临时运行库没有该站且队列已满：基础参数通过后返回 SERVER_BUSY，未到 worker 的 ENTITY_NOT_FOUND。
- 有效 admin token、`admin.charger_restart`、已知非 fault 桩：满队列时 SERVER_BUSY 先于 ORDER_STATE_CONFLICT。
- 有效 ml token、类型正确但整批不完整的 forecast payload：满队列仍可能先返回 SERVER_BUSY，尚未执行 worker 的 FORECAST_INVALID 判定。其中部分整批语义是纯计算，但本波次仅共享基础结构，不声称完成全部纯 forecast 语义迁移。

本轮没有添加“满队列允许跳过业务检查”的合同例外；也没有为这些剩余偏差添加接受 SERVER_BUSY 的永久测试期望。它们必须由 controller/用户另行裁决。服务端 README 明确保留这一限制。

## 真实 RED 与定向 GREEN

工作目录均为 `/mnt/hgfs/Desktop/SummerTermProject/worktrees/core-integration`；构建目录固定为 `/home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0`。

### I1 的测试先行 RED

在生产代码修改前运行：

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --target tst_server_threads -j4
QT_QPA_PLATFORM=offscreen /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/tests/admin-server/tst_server_threads queueCapacityDoesNotHideUnknownActionsOrCachedHealth
```

构建退出 0；测试退出 1。聚合错误码列表第 0 项实际 `SERVER_BUSY`，期望 `AUTH_REQUIRED`，`Totals: 2 passed, 1 failed`，80ms。该确定性测试在主事件循环交付任何完成通知前连续提交 256 个请求，再提交 10 个错误请求；覆盖匿名、错误角色、三种 confirmation、无效 token、金额、ID 和手机号，并保留未知 action、合法读取忙及 health 断言。RED 来自真实响应，不是拼写或编译错误。

### 未知非空 token 的 RED

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --target tst_server_threads tst_admin_window_refresh tst_database_worker -j4
QT_QPA_PLATFORM=offscreen /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/tests/admin-server/tst_server_threads unknownNonemptyTokenCannotUseAnonymousLogin
```

构建退出 0；测试退出 1。真实 auth.user_login 返回 `OK`，期望 `AUTH_REQUIRED`；`Totals: 2 passed, 1 failed`，32ms。新增测试同时在 GREEN 阶段验证 admin.login 与未知 token health。

### 初版修复的限定验证

以下命令均真实运行并退出 0（QtTest 计数包含 init/cleanup）：

```bash
QT_QPA_PLATFORM=offscreen /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/tests/admin-server/tst_server_threads queueCapacityDoesNotHideUnknownActionsOrCachedHealth unknownNonemptyTokenCannotUseAnonymousLogin restartAckThenShutdownCompletesExactlyOnce failedFirstRestartAckNeverSchedulesCompletion
# Totals: 6 passed, 0 failed；3433ms

QT_QPA_PLATFORM=offscreen /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/tests/admin-server/tst_server_threads purePayloadErrorsMatchBeforeAndAfterCapacityAdmission
# Totals: 3 passed, 0 failed；461ms

QT_QPA_PLATFORM=offscreen /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/tests/admin-server/tst_database_worker
# Totals: 3 passed, 0 failed；928ms

QT_QPA_PLATFORM=offscreen /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/tests/admin-gui/tst_admin_window_refresh failedResetAckKeepsRequestIdForConfirmedRetry
# Totals: 3 passed, 0 failed；383ms；offscreen propagateSizeHints 告警保留

QT_QPA_PLATFORM=offscreen python3 tests/admin-server/test_tcp_demo_reset.py /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/apps/admin-server/ev_admin_server DemoResetTcp.test_old_pending_across_new_reset_business_and_process_restart
# Ran 1 test in 0.162s；OK
```

第二个参数测试以 21 个独立无效基础参数，比较同一 AppContext 满队列本地入口和排空后的真实 TCP 入口，均为 INVALID_REQUEST。M2/M3 是覆盖既有正常实现，初次新增就通过；没有为宣称 RED 而故意破坏生产实现。两种队列测试都是有限容量断言，不做压力、race detector 或高频循环。

## 一次全量回归中的失败，以及修正后的限定复验

初版共享 preflight 构建成功后运行：

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
python3 -m pytest database/tests -q
```

- 构建退出 0。CMake 仍输出找不到 XKB/Cups 的可选组件告警，没有安装或屏蔽。
- CTest 退出 8，**27/29**，38.52s。失败只有 `server_tcp_p0_UTC` 和 `server_tcp_p0_Asia_Shanghai`，均为原用例 `test_cursor_preserves_arbitrary_fraction_across_telemetry_fault_and_restart` 在 `fraction-fault` 得到 INVALID_REQUEST；其合法 Timestamp 为 `2026-09-06T10:00:00.0002000000000000000000000000001+08:00`。
- 数据库 pytest 退出 0，**15/15**，1.18s。

根因是新 preflight 的 QDateTime 解析误拒绝了合同允许的任意长度小数。原 TelemetryService 已用 regex + QDate/QTime 校验整数日期时间，保留字符串小数。修正将该原算法移到 BusinessTime::timestampKey，服务和 preflight 共用；没有修改任何原测试向量、Timestamp 权威比较或 cursor SQL，也没有截断精度。

修正后运行并退出 0：

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -R 'server_tcp_p0_(UTC|Asia_Shanghai)$' --output-on-failure -j2
```

结果：构建退出 0；UTC 3.76s、Asia/Shanghai 3.77s；**2/2**、总计 3.77s。Controller 明确安排由其在最终提交上执行完整构建/CTest，本代理不重复整套。上面的 27 项和 pytest 是修正前源码证据，不能称作修正后 fresh 全量 29/29。

`git diff --check` 对本代理作用域无输出，退出 0。测试前后 `sha256sum runtime/golden/core.db` 均为原批准值 `5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7`。

## 限定复审清单与交接

1. 检查 token 角色快照只传值、登录 ACK 前发布顺序，以及正常入口/满队列对未知非空 token 的同一分类；确认不持有/跨线程调用 SQL 服务。
2. 检查 preflight 的基础校验与 forecast 业务语义错误域边界，未声明字段仍忽略；确认原 mutation/reset ACK 字节重放不变。
3. 检查 BusinessTime::timestampKey 与原 TelemetryService 算法等价，任意长度小数和 +08:00 不退化。
4. 检查 M1 局部 timer 上下文、M2 两个单次重启结果以及 M3 TCP/GUI 状态转换测试；原 reset 两事务、版本重试和 generation 防旧任务实现未修改。
5. 保留 I1 未关闭的权威业务错误/容量优先级冲突，以及尚未迁出的 forecast 整批语义。缺少用户授权时不得改合同或宣布可合并。
6. Controller 在最终源码上执行约定的唯一完整复验，并记录真实结果。

没有推送、合并、腾讯调用、凭据输出、客户代码修改或工作簿访问；没有接续此前被工具限制中止的诊断。原附件、黄金原件、队友分支及 root README/docs 状态文件未编辑/暂存；仅本报告位于许可的 .superpowers 报告目录。GUI offscreen、XKB/Cups 告警作为事实保留。Core 不要求 Web/ML 在线；正式联调、换机和两次人工彩排不属于本波次完成声明。
