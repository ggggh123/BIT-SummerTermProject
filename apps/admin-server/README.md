# Qt 管理／服务端

本模块是运行期 SQLite 的唯一业务写入端；用户端和模拟器通过冻结的 v1 TCP 协议调用。默认 core profile 不要求 ML 或 Web 在线，没有活动预测时 `system.health` 返回 `ok=true`、`status=degraded`，仍可完成登录、充值、预约、充电和结算。

## 启动

在仓库根目录完成 CMake 构建后运行：

```bash
cmake --build /path/to/build -j4
/path/to/build/apps/admin-server/ev_admin_server --server \
  --host 127.0.0.1 --port 9100 \
  --db /path/to/runtime/core.db \
  --snapshot /path/to/runtime/dashboard_snapshot.json
```

`--no-gui` 与 `--server` 等价。省略二者会显示管理员登录窗口；成功登录得到的真实管理员 token 会传给主窗口。未提供 `--db` 时使用 Qt 应用数据目录；未提供 `--snapshot` 时使用仓库的 `dashboard/runtime/dashboard_snapshot.json`。启动输出包含监听地址、端口和数据库路径。

## 线程与调度

```text
主线程：登录／管理窗口 + AppContext + TCP listener
       │ executeLocal / 已校验请求（排队）
       ▼
DatabaseWorker 所属 QThread
       ├─ 唯一命名 SQLite 连接
       ├─ RequestDispatcher → 原 Auth/Admin/User/Telemetry/Forecast 等服务
       └─ 原业务事务 + request_log ACK

每个已接纳连接的 QThread：ConnectionWorker → 线程内创建 QTcpSocket
       └─ 收帧／拆粘包／envelope 校验 ⇄ 排队请求／响应
```

最多同时服务 16 个 TCP 连接，额外连接收到 `SERVER_BUSY` 后关闭。已接纳的数据库请求串行执行，本地和 TCP 共用 256 个待完成请求的容量；容量耗尽返回 `SERVER_BUSY`。未知 action 仍优先返回 `INVALID_REQUEST`；权限判断消费共享 Actions／Permissions，缺少有效身份为 `AUTH_REQUIRED`，有效身份无相应权限为 `FORBIDDEN`。

运行期 GUI 通过 `executeLocal(RequestEnvelope, QObject *receiver, callback)` 异步调用。登录、创建站点、冻结／解冻和重启复用 TCP 的调度器与幂等日志。执行中的按钮禁用；失败显示响应码及可解释信息。内部聚合表和日志使用 `AdminView` 闭合枚举及管理员校验，未新增 TCP action，也没有公开数据库或服务指针。

健康对象由 DB worker 在初始化、命令完成和定期队列任务中刷新，其他线程只接收值副本。`system.health` 直接读取缓存并生成当前 `serverTime`，不为每次健康请求执行 SQL。因此数据库等待锁时，主界面事件循环和独立健康请求仍可响应。

SQLite 的创建、迁移、种子、服务创建、SQL 和销毁均在 DB worker 内完成；启用外键、WAL 和 3000 ms busy timeout。每个 AppContext 使用独立的命名连接，测试中的两个上下文互不覆盖。

## 重启与关闭

故障桩重启的首次事务提交且 ACK 成功保存后，worker 才安排 1500 ms 精确定时任务。计时器只向 DB worker 排队，`finishRestart` 在 worker 内执行确定性事务；重放保留首次 ACK，已恢复为空闲的旧请求不再次启动任务。进程重新启动时会继续完成数据库中已有的 `restarting` 状态。

关闭管理窗口、Qt 正常退出，以及 Unix 上的 SIGINT／SIGTERM 都触发相同关闭顺序：

1. 停止接纳请求并关闭 listener。
2. 在各连接所属线程停止 socket，退出并等待连接线程。
3. 排空已接纳的数据库命令，等待已经安排的重启完成任务。
4. 在 DB worker 中先销毁服务，再关闭并移除命名连接，最后退出数据库线程。

关闭后不再交付回调；receiver 被销毁时，待返回的数据会自动丢弃。断开连接不会撤销已经接纳的业务命令，重连后可使用相同 requestId 获取原始 ACK。代码不使用 `QThread::terminate`。等待中的 SQLite 命令仍受 busy timeout 约束，正常退出可能等待在途命令和重启任务完成。

## 验证

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir /path/to/build \
  -R 'database_worker|server_threads|admin_window_refresh|core_workflow|server_tcp_p0' \
  --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir /path/to/build --output-on-failure -j4
```

`database_worker` 用临时库锁复现原主线程阻塞，并核对健康响应、计时器、充值一次和逐字节重放；`server_threads` 覆盖帧、容量、权限、独立上下文、重启、断线重放和正常关闭；GUI 测试实际点击登录及管理按钮并检查管理员日志。原 UTC／上海 TCP P0 与程序化 `core_workflow` 继续作为兼容门禁。

## 在线演示复位

管理窗口“系统健康”页提供“恢复演示黄金数据”按钮。确认框明确提示将丢失当前演示业务状态；默认选择取消。确认后异步调用同一管理员 `demo.reset` action，按钮在请求期间禁用，完成后刷新当前页。失败后再次确认会复用原 requestId，以便继续尚未完成的 ACK 保存阶段；成功或快照警告显示在状态栏。

TCP 使用管理员 token 和 payload `{"confirmation":"RESET_DEMO"}`。confirmation 必须是逐字匹配的 string；未知字段忽略。默认黄金输入是构建时仓库的 `runtime/golden/core.db`，批准 SHA-256 固定为 `5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7`。部署到其他位置时须显式配对指定：

```bash
/path/to/build/apps/admin-server/ev_admin_server --server \
  --db /path/to/runtime/core.db --snapshot /path/to/runtime/dashboard_snapshot.json \
  --golden /path/to/sealed/core.db \
  --golden-hash 5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7
```

运行库与黄金库同路径、符号链接到同文件或 Unix 硬链接到同 inode 时在打开数据库前拒绝。复位前还拒绝黄金输入旁的 WAL／SHM／journal 文件，并核验哈希、SQLite integrity、外键、schema 版本、业务表和索引结构。黄金输入仅以只读 immutable ATTACH 访问；运行中的 SQLite 文件不会被替换。

核心事务只恢复 `admins, users, stations, chargers, orders, telemetry, station_hourly_history, forecast_runs, forecasts, events`，清理旧 request_log，并在运行库原版本上递增 snapshotVersion。schema_version 仅校验。事务同步保存内部 `demo_reset_receipts` 的 pending 凭证；后续复位保留所有旧凭证。actor 和有效 payload 身份必须一致，final 回执按原 ACK 字节重放；pending 只续快照和 ACK，进程重启也不会重复清库。旧设备重启任务在核心提交时失效。

快照在核心提交后通过 QSaveFile 原子替换。文件失败仍返回 `ok=true,code=OK`，message 警告快照尚未刷新，旧文件保留；DB worker 每秒尝试仍对应运行库当前版本的未完成快照，旧版本不会覆盖新版本。最终 ACK、final 状态和本次 request_log 在第二笔事务中一起持久化。若第二笔事务失败，返回错误且凭证保持 pending；客户端应保留 requestId 重试。没有活动预测批次是正常 core 数据，复位仍成功。

新增验证：`ctest --test-dir /path/to/build -R 'demo_reset|admin_window_refresh' --output-on-failure`。TCP 测试在临时副本上注入坏哈希、坏 schema／外键、SQL trigger 和不可写快照位置，并实际停止／重启服务验证 pending／final 恢复；这些程序化结果不代表正式人工彩排或腾讯在线验证通过。
