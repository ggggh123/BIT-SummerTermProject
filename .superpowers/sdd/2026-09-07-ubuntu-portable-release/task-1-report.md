# Task 1：发行运行适配器 RED／GREEN／自审报告

日期：2026-09-07

实现提交：`78ec355 feat(release): add portable runtime launcher`

范围：`scripts/release/portable_runtime.py`、`scripts/release/portable_launcher.py`、`tests/release/test_portable_runtime.py`。

共享文件 `scripts/demo_runtime.py` 未修改。

## 1. 实现结果

新增 `PortableRuntime(bundle: Path, data_home: Path | None = None)`，以继承方式复用既有 `Runtime` 的独占锁、运行数据库复制、进程身份登记、健康协议、启动顺序、失败回收和精确停止逻辑。发行适配层只替换下列边界：

- 从 `release.json` 读取 `schemaVersion`、`releaseId`、`sourceCommit`、目标平台及三端相对路径／SHA-256；不读取 Git 或 `CMakeCache.txt`。
- 仅接受计划约定的 `bin/ev_admin_server`、`bin/ev_charger_simulator`、`bin/ev_user_client`，启动前重新计算指纹，内容不符返回 `BINARY_MISMATCH`。
- 可写状态位于 `<data_home>/evcharging/<releaseId>/`，轮次位于其 `runs/`；默认 `data_home` 遵循 `XDG_DATA_HOME`，否则使用 `~/.local/share`。
- 用户配置 `<data_home>/evcharging/<releaseId>/config.local.ini` 覆盖包内 `config.local.ini`；显式 `EV_TENCENT_MAP_KEY` 优先级最高。
- 不使用外部 `EV_SIMULATOR_TOKEN`；每次 `start` 用 `secrets.token_urlsafe(32)` 生成本轮共享认证值，仅经父 Runtime 的子进程环境传递，不写入 manifest、日志或状态输出。
- `start` 在创建运行副本前检查 x86_64、桌面会话、配置、程序指纹及端口；创建新的独立轮次并写入 0600 原子 `current.json`。
- `stop` 沿用父 Runtime 的 PID／启动时间／可执行路径／boot ID 核验及反序 TERM/KILL 流程。
- `status` 分开报告三端进程、服务健康、模拟器鉴权和遥测提示；不读取或输出地图 Key、模拟器 token。由于现有公开状态文件不包含 Run/Pause 字段，遥测项明确报告“需在模拟器界面确认；默认暂停”，不虚构已上报结论。
- 已停止历史按 `releaseId` 和用户数据实际路径加载，不再要求旧安装绝对路径等于当前包路径；停止后移动包可继续读取历史并开始新轮次。若包在进程存活时被移动，新启动返回 `BUNDLE_MOVED_RUNNING`；停止操作仍按原进程身份记录安全回收。
- CLI 提供 `start`、`stop`、`status`，默认端口 `9100`、seed `20260901`、遥测间隔 `3000ms`；支持 `--software-rendering`，只向本轮子进程加入软件渲染环境。

## 2. TDD 证据

测试遵循“先命名会导致失败的生产回退，再观察失败”的原则；测试使用真实临时目录、真实可执行测试程序、真实 SQLite 黄金副本、真实本地 TCP 健康协议和真实子进程。没有通过源码字符串断言行为，也没有调用腾讯在线服务。

### RED 1：无构建缓存的发行程序发现

目标回退：若发行运行时不存在，或仍依赖开发构建目录，测试应失败。

命令：

```text
python3 -m pytest tests/release/test_portable_runtime.py -q
```

观察结果：收集失败，`ModuleNotFoundError: No module named 'portable_runtime'`。失败原因是生产模块尚不存在，不是测试拼写或夹具错误。

最小 GREEN：实现发行清单加载、用户数据根和 `build_info()`；结果 `1 passed`。

### RED 2：程序被篡改必须拒绝

目标回退：如果实现只返回当前文件摘要、却不与发行清单摘要比较，替换用户端程序后不应被误认为可启动。

观察结果：`test_tampered_binary_is_rejected` 失败，`Failed: DID NOT RAISE DemoError`，当时结果为 `1 failed, 1 passed`。

最小 GREEN：增加实际 SHA-256 与清单 SHA-256 的恒时无关等值检查和 `BINARY_MISMATCH`；结果 `2 passed`。

### RED 3：私有状态、迁移、端口与早退生命周期

目标回退：若继续依赖父 Runtime 的安装目录状态和“先 reset、后启动”接口，移动历史、自动新轮次、端口预检、失败回收均无法满足发行契约。

观察结果：新增真实生命周期测试后为 `3 failed, 5 passed`：

- 用户 `runs/` 尚未创建时，父 `refuse_active_server()` 触发 `FileNotFoundError`；
- `PortableRuntime.start()` 仍要求事先存在 PREPARED manifest，端口冲突和早退测试均在加载 manifest 时失败。

修正夹具完整性后（补齐数据库模块的真实依赖 `seed_demo.py`），最小 GREEN 包括：发行 manifest 加载规则、停止记录跳过、迁移识别、启动前置检查、自动 reset 和父启动流程调用。结果 `8 passed`。客户端 500ms 内早退时，测试确认 server、simulator、client 三条已登记记录均为 `EXITED`，manifest 状态为 `FAILED`。

### RED 4：最终 support 布局的 CLI

目标回退：若缺少发行 CLI，最终包布局不能执行 `start/status/stop`。

观察结果：`materialize_support()` 复制入口时因 `portable_launcher.py` 不存在失败，当时结果为 `1 failed, 10 passed`。

最小 GREEN：新增中文 CLI、参数边界、当前轮次指针和默认值；在临时包 `support/` 布局、`PATH` 为空的环境中真实执行 `start → status → stop`，结果 `11 passed`。

后续自审补充配置错误、无桌面、运行中迁移、软件渲染子进程环境和安装目录只读行为，均保持 GREEN。

## 3. 最终验证

最终验证命令：

```text
python3 -m py_compile scripts/release/portable_runtime.py scripts/release/portable_launcher.py tests/release/test_portable_runtime.py
git -c safe.directory=/mnt/hgfs/Desktop/SummerTermProject/worktrees/core-integration diff --check
python3 -m pytest tests/release/test_portable_runtime.py tests/scripts/test_demo_runtime.py -q
```

结果：三个命令均返回 0；pytest 输出：

```text
135 passed in 114.56s (0:01:54)
```

`tests/release/test_portable_runtime.py` 覆盖：

- 无 Git/CMake 缓存的三端发现与清单指纹；
- 程序篡改在创建轮次前被拒绝；
- 包内配置、用户覆盖、环境变量优先级与内部随机认证值；
- 缺失、损坏、空配置的稳定中文错误码；
- XDG 用户数据与安装目录隔离；
- 停止后移动包读取历史，并创建新轮次；
- 端口冲突不创建轮次、不启动进程；
- 客户端早退后的父 Runtime 回收；
- 无桌面明确失败；
- 运行中移动包明确失败，且仍可精确停止；
- 最终 `support/` 布局下的 CLI 默认值、健康状态、输出脱敏和 stop；
- 软件渲染变量实际进入测试子进程，发行目录未生成 `demo-runs`。

`tests/scripts/test_demo_runtime.py` 全量回归通过，证明本任务未改变既有开发运行入口的状态机和业务协议行为。

## 4. 自审与变异检查

逐项考虑的生产代码变异及对应保护测试：

| 变异 | 应失败的测试行为 |
|---|---|
| 恢复读取 `CMakeCache.txt` 或使用开发构建路径 | 临时发行包没有缓存且 `PATH` 为空，程序发现／CLI 启动失败 |
| 忽略 SHA-256 不一致 | 篡改 `ev_user_client` 后启动不再抛出 `BINARY_MISMATCH` |
| 把 runs 写回 bundle | 数据根断言或发行目录无 `runtime/demo-runs` 断言失败 |
| 忽略用户覆盖或环境优先级 | 手工固定的 `user-fixture-key`／`environment-fixture-key` 断言失败 |
| 重新采用外部模拟器 token | `external-token-must-not-be-used` 断言失败 |
| 用旧 `sourceRoot` 绑定已停止历史 | 移动发行包后的 status/reset 失败 |
| 不跳过 STOPPED 旧记录 | 移动后的 `new-round` 不能创建 |
| 端口检查放到进程启动之后 | 端口冲突会生成轮次目录或登记进程 |
| 绕开父 Runtime 回收 | 早退后的任一真实子进程不是 `EXITED` |
| status 合并所有健康层次 | server/simulator/client 三个独立中文字段断言失败 |
| 把 Key/token 写入 CLI 或 manifest | 固定假值的全文脱敏断言失败 |
| 软件渲染修改宿主全局环境或不传给子进程 | `/proc/<pid>/environ` 断言或上下文恢复逻辑失败 |

自审未发现需要修改 `scripts/demo_runtime.py` 的理由；发行适配只通过继承和小范围覆盖完成，没有复制启动／协议状态机。

## 5. 已知边界与后续关注

- 本轮按用户更新后的范围只完成本机自动测试；没有下载完整 Ubuntu ISO，也不宣称独立 Ubuntu 22.04 系统验收通过。
- 自动测试使用自包含的真实本地测试程序验证进程和协议生命周期，不调用真实 Qt GUI 或腾讯服务。最终 Jammy 原生程序、Qt/WebEngine 私有依赖、ABI 和可视界面仍需由打包及最终验收任务验证。
- `status` 可以从现有状态文件可靠确认模拟器进程和鉴权，但该文件不公开 Run/Pause。为避免持久化认证 token 或做虚假判断，遥测字段提示用户在模拟器界面确认。若未来业务侧增加不含凭据的 Run/Pause 状态字段，可在保持协议状态机不变的前提下进一步精确显示。
- 打包任务必须保持计划中的 `support/` 同目录复制关系、`database/` 资源以及 `release.json` 三端固定相对路径，否则发行清单验证会明确拒绝。
