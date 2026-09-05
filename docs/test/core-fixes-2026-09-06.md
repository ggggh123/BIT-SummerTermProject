# 核心阻塞修复与同版本复验（2026-09-06）

> 这是修复后的工程记录，与同日的[修前诊断基线](core-integration-baseline-2026-09-06.md)分开保存。当前为本地集成候选，不代表已经推送、合入 dev/main 或完成答辩验收。

## 结论与版本

本机获得直接介入服务端、数据库和模拟器的授权后，已补交三个原始 P0，以及模拟器同步、黄金库工具和管理端运行问题。各域均经历独立评审、修复和定点复审；组合后的生产代码版本为 **`2318d149c519f15fe478a1e700ad1ad60a2c9421`**。

2026-09-06 04:32（+08:00）前，同一组合已通过全新构建、24 项 CTest，以及真实 Qt UserApi 的四组完整业务诊断。随后新增实际 SimulatorWindow 与真实服务端的组合回归，源提交 `99bcecd+c6dda3d` 已通过独立评审；本地引入为 `1e349a6+9faad1f`。两类诊断分别记录，不能把手工遥测注入写成模拟器引擎输出。

| 修复域 | 已审查源提交 | 本地集成提交 |
|---|---|---|
| 服务端时间、计费、事务、持久化幂等 | `cfa00e7` + `dcd7d5a` | `79477a0` + `783a2d9` |
| 模拟器线上时间、同步、重发、黄金库与运行副本工具 | `6e44c18` + `6a69d0b` | `caebf29` + `4e58cff` |
| 管理端同库启动、统计、刷新及选择身份 | `9fc9919` + `237e2b0` | `9da1272` + `2318d14` |
| 实际 Qt 核心业务回归与生产周期同步覆盖 | `99bcecd` + `c6dda3d` | `1e349a6` + `9faad1f` |

初始组合来源：`origin/dev@e21c73a`、服务端 `661c91e`、数据 `10034fd`、本机 UI `48ee40c`。诊断快照 `4c0f845`，探针安全补丁 `1173984`。来源目录按明确路径引入；没有将整个功能分支的祖先关系 merge 到共享分支。正式 PR 仍需保留这些来源说明。

## 已修复的问题

### 服务端三个 P0

1. **时间戳导致严格客户端拒收。** 新写入的业务时间显式为 `+08:00`；不放宽 UserApi 的 Timestamp 规则。设备游标使用无损小数比较，不把 `.0001` 与 `.0002` 压成同一毫秒；历史最大值也不能依赖 SQLite 浮点时间排序。
2. **停止充电覆盖真实量价。** 停止保留遥测累计电量、按站点价格计算的整数分金额，时长取实际开始/结束时间；结算按同一订单金额扣款。故障/重启后的未结算订单继续占用，不能只因桩显示 idle 就重复预约。
3. **同一请求重复执行、SQL 失败留下半笔业务。** 业务变更和持久化 ACK 共用事务，重放先于业务及设备游标检查；相同请求返回首次响应字节，身份和 payload 不同不得冒用旧 ACK。失败/提交失败回滚，不把 SQL 文本当合同响应。

审查修正也有明确 RED→GREEN：原设备时间比较会拒绝 `.0001→.0002`；修复后通过 UTC/上海两组真实 TCP，覆盖无损精度、重复请求、进程重启重放、SQL/ACK/提交失败和回滚。

### 模拟器与数据库工具

- 遥测、故障、恢复的线上 JSON 保留毫秒；待 ACK 队列保留原 action、payload、requestId，重连后按顺序重发。
- 周期获取权威桩状态，区分 TCP 已连接与业务鉴权成功；不同模拟器实例不会因相同起点生成相同 status requestId 而误取旧 ACK。
- 默认生产模拟时钟使用当前 `+08:00` 时间；暂停一段时间再启动、故障/恢复也保持事件先后。显式固定起点仍可用于可重复测试。修复前固定 9 月 1 日起点可能早于 9 月 6 日订单开始时间；这是业务时间倒序风险，不声称原 UserApi 会执行它并不存在的跨字段检查。
- core 构建输出独立的 `core.manifest.json`/checksum，拒绝覆盖现有工件，不覆盖保留的 demo 清单。
- `database/create_runtime_copy.py` 校验 hash、完整性、外键及源/目标 sidecar，排他创建全新运行副本。源含 WAL/SHM/journal 时先拒绝，不能只复制主文件而漏掉已提交数据。它是停服冷启动工具，**不是在线 `demo.reset`**。

### 管理端

- `--db` 不再意外切换到 headless；仅 `--server`/`--no-gui` 选择无界面模式。GUI 仍经过管理员登录，窗口和唯一 listener 使用同一个 AppContext 和指定运行库。
- 月营收按自然月及已完成订单统计；预测告警读取保留，但 core 无 active forecast 时合法返回空告警，不重新引入 ML 门槛。
- 当前页自动刷新、切页刷新；保留筛选、分页和仍存在的稳定 ID。旧 ID 消失时清空 selection/current，防止“冻结/重启”误作用到原行的新 ID。Qt 按钮回归已复现旧版误冻结替代用户，修复后替代用户保持 active。

## 同版本验证

源码工作区：`/mnt/hgfs/Desktop/SummerTermProject/worktrees/core-integration`。

**最新组合复验：** 加入经审查的实际三端测试后，在代码/测试 HEAD **`9faad1feed137d0610e88c88d0de0d0d2dc09a9a`** 上再次 `cmake --build ... --clean-first -j4`，180/180 成功；全量 **CTest 25/25，39.04 秒**（2026-09-06 **04:58 +08:00**），地图离线 **15/15，237.44 ms**，数据库 **15/15，1.15 秒**。`core_workflow` 自身 4.04 秒，原始输出保存在 [`evidence/core-fixes-2026-09-06/ctest-with-core-workflow.log`](evidence/core-fixes-2026-09-06/ctest-with-core-workflow.log)。本段时间依据原始日志的 `Start testing`/`End testing` 记录；此前出现的 05:01 是模拟 `faultAt` 业务事件时间，不是测试执行时间。本段之后的文档提交不改变该已测生产/测试源码树。

本轮最终综合审查在完成前中断，未形成全分支 review clean 或合并放行结论。未完成范围、未验证的活动 `SimulatorClient` 早退析构候选，以及“test stop guard 仅缓解测试退出、不是生产析构修复”的边界，见[最终综合审查归档](../review/core-fixes-review-2026-09-06.md)。模拟 `faultAt` 仍是业务事件，不改写原始日志或业务值。

下表和第一份日志另保留生产组合 `2318d14` 的 24 项初次验证，不能把两轮计数相加当作 49 个独立测试。

全新原生构建目录：`/home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0`；不是旧 `ev-core-integration-build-0R6scP` 修前产物。

```bash
cmake -S . -B /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest \
  --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --output-on-failure -j4
node --test apps/user-client/tests/test_navigation_html.mjs
python3 -m pytest database/tests -q
bash scripts/check_env.sh
bash tests/scripts/test_check_env.sh
python3 -m pytest docs/test/evidence/core-integration-2026-09-06/test_probe_server.py -q
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1.5 \
  /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/apps/user-client/tst_user_mobileui
```

| 检查 | 结果 |
|---|---|
| 全新 configure/build | 成功，175/175 构建步骤 |
| CTest | **24/24**，38.73 秒，含 UTC/上海真实 TCP、管理启动和刷新 |
| 地图 HTML 离线 Node | **15/15**，350.26 ms，不访问腾讯 |
| 数据库 | **15/15**，1.43 秒 |
| core 环境检查 | 退出码 0 |
| 环境脚本回归 | **13/13** |
| 历史诊断探针安全回归 | **3/3**，0.17 秒 |
| 用户端 1.5 倍 DPI QtTest | **16 passed / 0 failed / 0 skipped**，1061 ms |

配置仍有环境缺少可选 XKB/CUPS 的提示；offscreen Qt 存在 `propagateSizeHints` 提示。未为消除这些非阻塞提示修改系统或降低测试断言。

### 真实 Qt UserApi 四组业务诊断

控制器诊断使用生产 UserApi/TcpJsonClient，链接上述同版本构建的静态库；分别启动四份全新黄金库运行副本、四个独立真实服务端。设备遥测由诊断中的生产 TcpJsonClient 发送，**不是实际模拟器引擎**。

路径：`/home/hushengyuan/.cache/ev-core-userapi-probe-Sbc81d/`（本机诊断缓存，不是跨机器脚本入口）。`probe_qt_flow.cpp` 与 `run_probe.py` 保留在此；执行：

```bash
python3 /home/hushengyuan/.cache/ev-core-userapi-probe-Sbc81d/run_probe.py \
  /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/apps/admin-server/ev_admin_server full
```

每组：登录 → 充值 1000 分 → 查站/详情 → 预约/开始 → 遥测 0.25+0.50 kWh → 停止 → 结算 → 历史和当前订单检查。

| 服务端进程 TZ | 账户 | 停止电量/金额 | 结算余额 | 结果 |
|---|---|---|---|---|
| UTC | 黄金库原用户 | 0.75 kWh / 113 分 | 50887 分 | PASS |
| UTC | 自动注册新用户 | 0.75 kWh / 113 分 | 887 分 | PASS |
| Asia/Shanghai | 黄金库原用户 | 0.75 kWh / 113 分 | 50887 分 | PASS |
| Asia/Shanghai | 自动注册新用户 | 0.75 kWh / 113 分 | 887 分 | PASS |

四份运行库各自产生订单 432；独立副本相同 ID 是预期，不是对同一库重复造单。四组完整断言通过、退出码 0，每个自建服务进程均已停止。对修前服务执行同一严格客户端诊断会出现 `INVALID_RESPONSE uncertain` 并失败，没有通过测试专用宽松解码绕过原缺陷。

黄金封存原件 SHA-256 仍为：

```text
5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7
```

封存原件没有作为服务端运行库。服务端只迁移/写入全新副本；运行副本启动后的 hash 变化是正常现象。

### 实际 SimulatorWindow / UserApi / 服务端组合回归

新增 `core_workflow` QtTest 在外部启动真实服务进程，在测试进程中构造生产 UserApi、SimulatorClient、TelemetryEngine 和 SimulatorWindow，调用实际窗口槽并观察真实回包。两个用例顺序使用一份全新的 core 运行副本，不执行 SQL；结尾检查自建服务退出和临时目录清理。跨机器运行入口见 [`tests/integration/README.md`](../../tests/integration/README.md)。

- **正向**：新用户登录/充值/查站/预约/开始，原已连接模拟器自动从 idle 同步 charging；相同模拟起点的第二实例仍取得新快照。窗口产生两次有效遥测，暂停后停止/结算，历史 completed、current=null、桩 idle、管理营收一致。
- **故障**：黄金老用户开始充电并产生遥测，实际窗口注入故障/快速恢复，事件时间相差 1 ms；结束时间不早于开始时间，量价冻结，恢复意图不直接解除桩故障。真实管理员 TCP 重启后，生产模拟器周期器自动同步 idle；旧订单未结算时他人仍不能预约，最后结算正确。
- 为压缩运行时间，测试明确使用 60 秒业务模拟步长、1 秒状态刷新配置；没有修改生产默认配置。自动同步依赖生产 timer，不再由测试额外 timer 或手动 refresh 替代。

源提交 `c6dda3d` 定点复验为 `core_workflow` **4 passed / 0 failed**（含 QtTest 初始化/清理），相邻 `core_workflow + simulator_client` **2/2**。记录的真实业务值：

| 链路 | 订单 / 桩 | 电量 | 金额 | 余额 | 补充核对 |
|---|---|---|---|---|---|
| 新用户正向 | 432 / 1 | 2.066604 kWh | 310 分 | 50000→49690 | 今日营收 +310 分 |
| 黄金用户故障 | 433 / 2 | 1.047189 kWh | 157 分 | 50000→49843 | restart→idle 后未结算占用仍有效 |

首次同码测试连接旧修前服务时，新用户登录被真实 UserApi 拒绝为 `INVALID_RESPONSE`，退出失败；修后通过。审查还发现测试侧主动刷新无法证明生产周期器工作，因此补交自动同步覆盖并定点复审通过。

这些是**程序化 Qt 核心联调**证据，不是人工连续操作三窗口、腾讯在线导航或两轮答辩彩排。随机端口先探测再绑定有低概率竞争风险，当前以明确失败和超时呈现，未静默跳过。

## 尚未关闭的门槛

- **DatabaseWorker 线程架构仍未完成。** 当前补丁修正了事务与幂等，但现有 ApiServer/service/重启 timer 仍在主线程；合同第 8 节要求的专属串行 DB worker 不能据此宣称实现。
- **在线 `demo.reset` 未实现。** 合同第 27 条已经定义，不是不存在的接口。冷启动新副本不能替代其事务、receipt 与 snapshot 规则。
- 实际模拟器两条程序化链路已补齐；全套异常链路与最终宽范围评审须分别记录，自动化计数不能代替所有 action/权限的全面验收。
- 腾讯在线地址解析/驾车/步行、Linux 人工三窗口连续操作、第二批充电/导航/历史/账户 UI 收口，以及同一候选 SHA 两轮完整答辩彩排尚未完成。
- 没有推送、创建 PR 或合并共享 dev/main；没有代表队友认领/签署验收。需求矩阵继续由人工全权维护，本轮不操作。

因此本轮结果是**三个原始 P0 的已审查修复与集成回归通过**，不是项目整体 GO 或全部合同完成。

## 实施过程中的裁定与成本

以下按发生顺序保留，便于团队审查本机代为作出的取舍：

1. 最初只执行已批准的 UI 保护/诊断交接，不自动接管队友生产代码；代价是要等待明确扩权。这项边界随后由用户本轮授权覆盖。
2. 新建同级集成 worktree，保留 UI linked worktree 与主 checkout；代价是多保留本地目录，但不破坏原有工作。
3. 按明确核心目录导入固定源快照，避免混入人工需求矩阵及跨分支旧文档；代价是不能宣称源功能分支祖先已经 merge，正式 PR 必须核对来源 SHA。
4. 仅把 UI 长内容测试的固定等待改成有超时的条件等待，保留原断言；若判断错误可能掩盖布局问题，因此要求独立评审、默认/高 DPI 反复验证。最初拟“纠正缺站截图说明”的视觉判断后经原图复核撤回，截图未替换，不能把误判当修复成果。
5. 按用户明确扩权执行服务端/数据修复和真实链路验证；代价是扩大本地改动面，但不免除测试，也不自动获得共享分支发布/改写授权。
6. 将强耦合的服务端计费/事务/幂等交给同一实施者，模拟器/构建器另域并行；代价是必须额外做同版本组合验证，单域通过不能关闭整体门槛。
7. 更正旧报告：`demo.reset` 已冻结但未实现；保留修前“未支持”观察，先关闭核心计费/充值 P0。代价是在线 reset 缺口仍明确开放，冷启动工具不能代替它。
8. 仅在文件、索引和工作区均隔离的独立域使用并行实施，覆盖开发流程默认串行策略；若接口独立性判断错误会增加集成返工，因此保留各域评审与最终跨模块审查。

采用的子代理开发、测试先行和独立评审流程，实际促成了设备任意小数精度、源 WAL 漏拷、模拟器状态请求碰撞、管理行选择身份等补充修正；它没有代替队员签署验收或发布。
