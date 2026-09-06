# 核心三端运行入口实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把已确认的手动三端冷启动流程落实为可重复的 reset/start/smoke/stop 四个入口。

**Architecture:** POSIX shell 仅定位真实脚本路径并转发参数；Python 标准库负责 run 状态、受控进程和 TCP 基础冒烟。模拟器以可选本地原子状态文件暴露自身鉴权/同步结果，不改 TCP 协议。黄金副本继续复用现有 Python 校验复制工具，服务器仍是唯一运行期 SQLite writer。

**Tech Stack:** Ubuntu Linux、Qt 6.2+ / C++17、Python 3 标准库及既有 pytest、POSIX sh、CMake/Ninja。

**Spec:** 已批准 [core 集成与发布目标](2026-09-04-core-integration-demo-release.md) §§1、3–6 与 [手动交付基准](../../release/core-demo-runbook.md) §§1–3。用户在“下一批优先启动、停止、复位和冒烟”建议后回复“继续推进”。本计划仅细化这批，不承担完整业务/腾讯在线彩排或 release GO。

## Global Constraints

- 核心仅 Qt 用户端、Qt 管理/服务端、SQLite/Qt 模拟器；Web/ML 保留但不是本批启动或冒烟门槛。无 active forecast 且 health=degraded/ok=true 合法。
- 不读取、检查、编辑、渲染或验证任何 xlsx；原需求矩阵由队员人工负责。旧中止诊断不续跑。
- 不调用腾讯在线服务，不读出密钥实值，不隐式安装依赖；使用现有全局环境。实际 Qt GUI 自动化可用 offscreen，但不能冒充人工或地图验收。
- 不改原协议、业务状态机、计费、ACK、黄金原件/checksum 和 optional demo.db；只在全新运行副本上运行，绝不在线覆盖数据库。
- 使用独立 worktree /mnt/hgfs/Desktop/SummerTermProject/worktrees/core-integration；本批分支 feat/runtime-delivery-20260906 从 f0b8d4a 开始，原 core-delivery 分支保留。不得推送、创建 PR 或合并；不得动其他工作树。
- 新文档尽量中文，源码用 apply_patch，精确 stage 本任务文件。子代理不再派子代理，主控统一评审。除本计划 scratch 外，不接触旧 SDD workspace。
- 不 kill 按名称匹配的进程，不覆盖旧 run；任何 TERM/KILL 必须校验该 run 的 PID + Linux boot ID + /proc starttime + canonical exe。信号用 pidfd 锁定目标身份，系统不支持 pidfd 时明确不支持而非不安全退化。
- 每个 run 的状态/日志/DB 固定为本仓库 runtime/demo-runs/<run-id>/；run-id 是 1–64 位 ASCII 字母数字开头，后续仅字母数字点下划线横线，禁止 . 和 ..。拒绝目录/文件符号链接逃逸与已有目标；保留失败数据库和日志。
- JSON/日志/报告不保存腾讯 Key、模拟器 token、用户或管理员会话 token；不打印完整命令或环境。记录 sourceCommit、sourceDirty、源码/build/run 实际路径及三二进制 SHA-256。指纹与源码 SHA 分别记录，不自动宣称证明二进制与 SHA 完全一致。

## 接口与取舍

四个 shell 入口从任意 CWD 或符号链接调用均按 readlink -f 定位实际源码根，exec python3 scripts/demo_cli.py <subcommand> "$@"。

```sh
scripts/reset_demo.sh --run-id meeting-01
EV_SIMULATOR_TOKEN=<local-token> scripts/start_demo.sh --run-id meeting-01 --build-dir /path/to/native-build --port 9100
scripts/smoke_test.sh --run-id meeting-01
scripts/stop_demo.sh --run-id meeting-01
```

默认三 GUI；start 的 --headless 只令服务端使用 --server（测试另外设置 QT_QPA_PLATFORM=offscreen）。模拟器初始暂停不变。host 固定本机127.0.0.1；支持 --port 1..65535、--seed 0..4294967295、--interval-ms 1000..10000、--timeout-seconds 1..60，默认9100/20260901/3000/15。stop 超时默认10秒，强制 KILL 仅 --force 明确选择时执行；启动失败也默认只 TERM，有未退出进程则保留其记录供人工处理。

配置：start 必须显式获得非空 EV_SIMULATOR_TOKEN；Qt 模拟器新支持该环境变量，显式 --token 仍优先且兼容旧行为。腾讯 Key 解析复用用户端环境优先、源码根 config.local.ini 其次，不输出内容；missing/blank 在任何进程启动前失败。测试只用无效占位 Key 且不访问地图。所有子进程以源码根为 CWD，明确覆盖 EV_SERVER_HOST/PORT，不因调用者位置漂移。

### Task 1: 模拟器本地就绪观测与环境配置

**Files:** Create simulator/src/app/RuntimeStatusWriter.h/.cpp、simulator/tests/tst_runtimestatus.cpp；Modify simulator/src/main.cpp、simulator/src/app/SimulatorConfig.cpp、simulator/CMakeLists.txt、simulator/README.md。

**Interfaces:** 生产 main 若 EV_SIMULATOR_STATUS_FILE 非空则启用 RuntimeStatusWriter，原子覆盖启动器指定的本地文件。输出 `{"schemaVersion":1,"pid":123,"sessionState":"ready","updatedAt":"2026-09-06T10:00:00.000Z"}`。状态集合 starting / waiting_auth / ready / auth_failed / disconnected / stopped，只有 ISimulatorClient::sessionReady（已收到真实成功状态回包并更新 charger snapshot）才能 ready；每次 sessionReady 更新时刻。连接不是ready。文件无 token。写失败返回失败信息并使启用观测的进程启动/运行明确非零失败，不能留下看似新的 ready 证据。

- [ ] **Step 1: 先补真实写入/状态转移测试，运行 RED。** 使用 QTemporaryDir 与 QObject 生命周期绑定，真实 RuntimeStatusWriter 消费客户端事件；测试初始状态、connected≠ready、ready→disconnected、auth_failed、stopped、合法UTC时戳/当前PID、失败路径、未配置不写文件。优先用现有真实 SimulatorClient + 回环 TCP 测试辅助，不重复协议解析；必要的信号源替身只替外部网络，断言真实写出文件。新环境token验证CLI优先、env回退、都空原行为。

```cpp
// 所需消费者边界：读实际生成的 JSON，不检查源码里有没有某字符串。
QCOMPARE(state.value("sessionState").toString(), QStringLiteral("waiting_auth"));
QVERIFY(state.value("pid").toInteger() == QCoreApplication::applicationPid());
// 对真实成功状态回包等待，禁止用 connected 作为期望ready的证据。
QTRY_COMPARE(readStatus().value("sessionState").toString(), QStringLiteral("ready"));
```

- [ ] **Step 2: 实现最小接口并接生产 main。** Writer 使用 QSaveFile；connect 的上下文是生命周期确定的 QObject；先写 starting 再 start client；失败信号/返回值由 main 显式处理。token 解析形态为 `parser.isSet(tokenOpt) ? parser.value(tokenOpt) : qEnvironmentVariable("EV_SIMULATOR_TOKEN")`。main 不自动点击 Run，不改遥测或业务代码。
- [ ] **Step 3: 定向 GREEN，再全量构建/CTest、精确提交并写报告。**

```sh
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -R '^simulator_' --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
```

### Task 2: 四个运行入口、真实三端组合与交付说明

**Files:** Create scripts/reset_demo.sh、start_demo.sh、stop_demo.sh、smoke_test.sh、demo_cli.py、demo_runtime.py、demo_processes.py、demo_protocol.py、tests/scripts/test_demo_runtime.py、tests/scripts/test_demo_runtime_live.py、tests/scripts/CMakeLists.txt、docs/test/core-runtime-entrypoints-2026-09-06.md；Modify root CMakeLists.txt、docs/release/core-demo-runbook.md、README.md、docs/superpowers/plans/README.md、docs/superpowers/plans/2026-09-04-core-integration-demo-release.md。不改Task1源码；需要修正时先报告。

**Interfaces:** 消费Task1 `EV_SIMULATOR_STATUS_FILE`、`EV_SIMULATOR_TOKEN`及JSON字段。Python模块分工：demo_cli负责 argparse/中文摘要+JSON最后一行和退出码；demo_runtime负责新副本、manifest、flock串行四操作、启动阶段编排；demo_processes负责身份抓取、pidfd信号、存活/退出检测与反向回收；demo_protocol负责4字节大端JSON/TCP（上限1MiB、总超时、requestId匹配、v1响应shape）及只读业务冒烟。模块供CLI和测试消费，不增加生产test-only参数。

- [ ] **Step 1: 从外部行为写 RED。** 用 tmp_path 中的精确测试仓库副本和测试自己拥有的进程，实际运行CLI/函数，不在真实黄金上制造损坏。没有入口时command-not-found是初始RED，随后逐项添加测试覆盖边界；不能只断言文件存在或脚本文字。

```python
first = run_cli("reset", "--run-id", "round-01")
assert first.returncode == 0
digest = sha256(runtime_db.read_bytes()).hexdigest()
assert digest == approved_digest
again = run_cli("reset", "--run-id", "round-01")
assert again.returncode != 0
assert sha256(runtime_db.read_bytes()).hexdigest() == digest
assert unrelated_process.poll() is None  # 冲突/错误PID/回滚均不能杀测试的旁观进程
```

- [ ] **Step 2: 实现 reset/start/stop。** 所有入口 --help、未知参数非零；错误JSON `{ok:false,code,message}`。每个run manifest.json使用版本1、原子替换；状态 PREPARED/STARTING/RUNNING/FAILED/STOPPED；processes按server/simulator/client字典，记录pid/starttime/exe/bootId并逐个落盘。参数与path验证在创建进程之前，native CMakeCache.txt 的 CMAKE_HOME_DIRECTORY 必须与源码根相同，三个预期二进制可执行并记录hash；不隐式构建/安装，不支持裸二进制复制包。

reset仅接受新的run-id，调用现有create_runtime_copy（精确core.db+core.db.sha256），拒绝本仓库已记录的活跃/身份不明服务端；从不覆盖或清空旧run。start只启动PREPARED run，拒绝重复和已存在进程记录；全仓库flock防同仓库竞态，先bind探测端口，然后显式启动 server --db <run>/core-runtime.db --host 127.0.0.1 --port <port> --snapshot <run>/snapshot.json --golden <core> --golden-hash <approved>。默认GUI，--headless加--server；等待进程身份存活与严格health成功。snapshot作为隔离写出位置，不设任何Web门槛。

随后启动模拟器：env token/status-file，CLI host/port/seed/interval；等待同PID且fresh的ready（updatedAt不早于该次启动，当前不超过max(5秒,3×状态周期)，waiting/auth_failed/disconnected不是通过）。最后启动用户端，观察至少500ms内未退出；进程存活仅CLIENT_PROCESS_ALIVE，不称Qt登录/地图通过。每阶段有deadline，错误只反序TERM本次已保存身份的进程并保留日志/DB；无法确认或未退出的记录不得丢失。stop反序逐个核对pidfd身份再发信号；已退出幂等通过，不匹配输出WRONG_PID且不发信号，继续回收其他正确记录；失败非零且不称STOPPED。未启动PREPARED可停为STOPPED，但不能以此覆盖复用同run。

- [ ] **Step 3: 实现基础 smoke 与失败测试 GREEN。** smoke不读写活动SQLite；检查manifest归属/三个活进程身份、真实health、同PID且fresh的模拟器ready；通过真实TCP对既有黄金用户13800138000登录、user.get、station.list、station.detail、order.current做基本shape/归属校验，认证token仅存内存。查询无active forecast合法；不做充值、建单、遥测、故障、结算。输出BASIC_SMOKE_PASS及 `tencentNavigation/fullBusinessRehearsal/otherMachine: NOT_RUN_SEPARATE_GATE`，不能输出release GO。记录action/code/requestId摘要但不含payload/token；smoke失败写报告并非零，不伪造成功。

必须覆盖：任意CWD/路径含空格/脚本符号链接；重复run/start；缺key/token/非法port/seed/interval；坏hash/源sidecar/已有DB/目标symlink/越界；无关端口监听及启动竞态失败；server失败/健康超时/sim拒绝/client立即退；错误PID/starttime/bootId/exe不杀旁观进程；并发同仓库命令互斥；stop超时默认不KILL、--force仍身份校验；smoke错误id/坏envelope/失效ready/旧进程与非readyhealth明确失败。测试不得在无关真实进程上发破坏性信号。

- [ ] **Step 4: 真实三程序整合测试和记录。** `test_demo_runtime_live.py` 通过EV_DEMO_BUILD_DIR使用本工作树构建，从唯一新run开始，真实 reset→start --headless→smoke→stop；QT_QPA_PLATFORM=offscreen且占位Key，不运行地图，模拟器初始paused。再验证坏模拟器token时部分启动回滚以及旧run完整保留；只清理测试自己的已确认进程，运行副本与日志留存。停服后只读SQLite核对integrity/FK、6站48桩及真实simulator.status记录；黄金hash/optional原件不变。unit与live作为两个CTest目标（Python已有core依赖）接入，live超时有界且标记不等于人工GUI/换机/双彩排。

```sh
python3 -m pytest tests/scripts/test_demo_runtime.py -q
EV_DEMO_BUILD_DIR=/home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 QT_QPA_PLATFORM=offscreen python3 -m pytest tests/scripts/test_demo_runtime_live.py -q
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
python3 -m pytest database/tests -q
```

- [ ] **Step 5: 中文交付文档与精确提交。** 更新实际可用四入口/参数/报告路径/预备→启动→smoke→停止→下一新run流程，列明人工管理员登录/模拟器Run尚需操作。保留旧手动流程与历史事实；旧六入口计划区分四个已实现、rehearse/release两个仍未实现。写真实测试结果和失败过程，不假造换机、UI或腾讯证据，不动需求矩阵。自审后commit任务文件、完整报告写本计划scratch；主控另派独立评审。
