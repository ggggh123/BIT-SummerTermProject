# 服务端交付收尾验证记录（2026-09-06）

## 范围与基线

- 共享基线：PR #10 已合入 `dev@97c6da13adb3b50503091dab9456fa704728771a`。
- 本批分支：`feat/core-delivery-20260906`；在独立集成 worktree 工作，不修改主目录、队友分支或黄金原件。
- 本批目标：[服务端线程归属与在线复位收尾](../superpowers/plans/2026-09-06-server-delivery-closeout.md)。客户端第二批 UI、启动脚本、换机运行、腾讯在线联调和正式双彩排不由本记录宣告完成。
- 需求矩阵由队员人工维护，本批不读取、检查、编辑或验证工作簿。

## 线程归属与统一调度

实现提交：`47bcdaee4e61fa4b852752acae64184e65c406b3`。

- 运行期数据库连接、业务服务、业务写入和 ACK 留痕由唯一 DatabaseWorker 线程持有；管理 UI 及网络连接通过异步队列调用。
- 每个连接工作线程创建和使用自己的 socket；最多 16 个连接、256 个待处理命令。健康检查读缓存，不逐次执行 SQL。
- 管理 UI 使用真实 admin token 和 requestId；回调绑定接收对象生命周期，避免窗口销毁后访问 UI。
- 重启定时器只排队，由 DB worker 完成事务；关闭时停止新请求、关闭连接、排空工作和延迟重启，再在所属线程关闭数据库。
- SIGINT/SIGTERM 转交 Qt 事件循环处理，无界面模式和管理登录窗口阶段均有正常关闭测试。

### RED / GREEN

| 定向测试 | 修改前可复现结果 | 修改后结果 |
|---|---|---|
| 测试连接持有 `BEGIN IMMEDIATE` 时发业务请求与 health | 30 ms UI 定时器实际延迟到 6037 ms，超过 500 ms 限制 | DB 等待期间 UI 定时器与缓存 health 正常响应 |
| 无界面服务端 SIGTERM | `CrashExit`，不符合 `NormalExit` 断言 | 正常退出 |
| 队列满时未知 action | 返回 `SERVER_BUSY` | 按合同先返回 `INVALID_REQUEST` |
| 管理登录对话框阶段 SIGTERM | `CrashExit` | 正常退出 |

构建命令：

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
```

本任务最终构建退出码 0，CTest **27/27**，38.98 秒。包含新 `database_worker`、`server_threads` 与既有 `core_workflow`、UTC/上海 TCP、管理 GUI 等测试。本文件后续列出组合到在线复位及最终修复后的新结果，不能将此历史计数当作最终计数。

独立任务评审：规格通过、质量 Approved，无 Critical/Important。两项 Minor 为测试提前失败时 singleShot 局部捕获的生命周期保护，以及额外的重启关闭/失败事务覆盖建议，交整分支评审统一处理。本段不是整个项目或历史全部 action 的重新认证。

### 兼容性裁决

1. 当前范围允许无 active forecast，健康状态可以是 `degraded` 且 `ok=true`；不沿用旧计划的 Web/ML、ready/144 预测门槛。若范围理解错误，会缺少可选展示内容，应通过重新确认范围处理，不应暗改核心闸门。
2. `admin_runtime` 原先以 health 请求写日志证明使用指定数据库；合同要求 health 不执行 SQL，故改为真实 `admin.login` 留痕，保留指定 DB、端口、窗口及日志计数断言。若替代断言不足会减少 DB 选择覆盖，独立评审已核对该替代与锁等待期间 health 的专门测试。

## 在线 demo.reset

初版实现提交：`876edaa73dfa889145567ec24f06f899567c8d25`，评审修复提交：`91e87be305a8b06d9ef325cc53bd9beb290a3680`。任务评审发现的默认数据库路径前置保护缺口已修复并通过限定复审；下方保留初版缺陷事实，不抹去发现与修复过程。

- 仅 admin、有效 `confirmation: "RESET_DEMO"` 可执行。黄金输入需通过 hash、sidecar、integrity、FK、schema/索引检查；业务显式表与 pending receipt 同事务复位，运行库快照版本递增。
- 第二笔事务持久化 final receipt、完整 ACK 和本次日志。pending 只继续提交后阶段，final 逐字节重放；后续不同 reset 保留旧 receipts。
- 快照失败不会伪装成核心事务失败：返回 OK 加中文警告、保留 last-good，并只对当前版本后台重试。已保存的原 ACK 不随后台恢复改写。
- 核心提交使旧重启任务 generation 失效；新旧重启交错已有真实 TCP 用例。管理“系统健康”页有明确数据丢失确认框，默认取消，确认后异步调用相同 action。
- 初版构建退出码 0，CTest **29/29**（38.62 秒）；新增 `demo_reset` 和 `server_tcp_demo_reset`，后者在 UTC 下运行 9 个真实 TCP 用例。数据库 pytest **15/15**（1.19 秒）。

新增测试先复现合法 admin reset 返回“不支持动作”，其后同文件、schema 索引、GUI 按钮和 UTC 快照时间均记录实际 RED。第一轮全量为 28/29，原因是新 GUI fixture 错误地固定期望版本 1，忽略此前四次变更；改为检查复位前版本 +1，未修改生产版本逻辑，第二轮为上述 29/29。

独立评审发现的 Important：省略或空白 `--db` 时，三路径互异检查使用原始参数，而实际默认路径在开库时才解析。修复必须把同一无副作用解析值同时交给前置检查与开库，并验证 golden/snapshot 指向默认库时不产生写入。这是初版真实缺陷，不以默认使用显式路径为由忽略。

修复新增 `DatabaseManager::resolvePath()`，统一解析后先检查、再开库。省略/空白 `--db` × golden/snapshot 冲突 × 直接路径/硬链接共 8 组用例先全部 RED，修复后全部通过，隔离 `XDG_DATA_HOME` 中原文件 hash 未变且无 sidecar。定向 CTest `server_tcp_demo_reset|database_worker|demo_reset|admin_runtime` **4/4**（6.01 秒）。独立限定复审：Important ADDRESSED，无新增破坏。

验证输出保留的环境告警：CMake 的可选 XKB/Cups 缺失提示和 offscreen Qt 的 `propagateSizeHints()`；本记录不宣称“无警告”。receipt 长期保留的清理策略不在本批实现。

## 修复后组合回归

在 `91e87be` 上重新执行本文件构建/CTest 及下节三条命令：构建退出码 0，CTest **29/29**（38.67 秒）、数据库 pytest **15/15**（1.16 秒）、地图 HTML 离线 **15/15**（342.68 ms）、环境脚本 **13/13**。以上是同一修复后源码的组合结果，不复用初版 29/29。整分支最终审查结果另行追加。

## 其他独立复验

### 最终审查修复中的回归记录

整分支审查发现的满队列校验优先级及有限测试修复以 `76ff1de` 提交，见[本批审查归档](../review/server-delivery-review-2026-09-06.md)。修复中的一次完整构建成功、数据库 15/15，CTest **27/29**：UTC/上海运行的同一高精度事件游标用例失败，新加入的 QDateTime 预检拒绝了原合法长小数 Timestamp。修正为服务和预检共用原有字符串小数算法，原测试向量未改；先取得 UTC/上海 **2/2**，再由本机主控执行下述最终完整验证。完整 RED、定向覆盖和修复过程见[归档报告](evidence/server-delivery-2026-09-06/final-fix-report.md)。

在最终实现 `76ff1de`（文档 HEAD 为 `bc436e9`）重新运行构建/全部 CTest/数据库 pytest：构建退出 0、CTest **29/29**（38.29 秒）、数据库 **15/15**（1.11 秒）；黄金 SHA-256 仍为批准值。这是时间戳修正后的实际全量结果，非拼接两次局部 GREEN。新测试包括满队列身份/基础参数、未知 token、重启关闭/首次 ACK 失败、跨新 reset 的旧 pending 和 GUI 失败复用请求 ID。

纯基础参数检查与身份副本解决了 I1 的已复现认证/格式分支；依赖权威数据库的业务检查和仍在 worker 内的 forecast 整批语义尚未获得满队列前判定，因此 **29/29 不代表该合同 Important 已关闭**。容量合同建议尚未获用户批准。

最后[限定复审](../review/server-delivery-final-rereview-2026-09-06.md)关闭 M1–M3，保留 I1，并新发现 N1：forecast 的 `horizonH=-1` 被预检改为 `INVALID_REQUEST`，而合同的有效整数业务越界应是 `FORECAST_INVALID`。现有 29 项测试没有覆盖该符号边界；N1 尚未修复，不能以全量 GREEN 或 Web/ML 可选掩盖该兼容回归。本批最终剩余为 I1、N1。

本批服务端修改之外，执行以下不调用腾讯网络服务的检查：

```bash
bash tests/scripts/test_check_env.sh
python3 -m pytest database/tests -q
node --test apps/user-client/tests/test_navigation_html.mjs
```

结果：环境脚本 **13/13**；数据库 **15/15**（1.11 秒）；地图 HTML 离线 **15/15**（233.60 ms）。这些结果不能替代真实服务端与腾讯导航共同运行的人工验收。

## 保留边界

- 原综合审查中止和未证实异常仍按[原审查归档](../review/core-fixes-review-2026-09-06.md)保留；本批普通功能测试不是接续被工具限制中止的诊断，也不证明旧风险已消失。
- 黄金 core 库批准 SHA-256：`5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7`。
- 不以自动回归成功替代真实人工操作、换机运行、PM 签字或同 SHA 两次彩排；本批没有腾讯调用，也没有推送或合并新的共享分支。
