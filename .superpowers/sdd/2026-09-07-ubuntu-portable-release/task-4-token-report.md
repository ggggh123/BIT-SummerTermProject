# Task 4：发行随机模拟器认证服务端兼容报告

日期：2026-09-07

范围：服务端 `TokenRoles`／`AuthService`／`DatabaseWorker`／`AppContext` 的认证快照传递、对应 admin-server 测试，以及真实三程序测试的错误 token 注入夹具。没有修改协议、数据库结构、界面、发行脚本、真实配置或 Key，也没有读取或输出任何运行认证值。

## 1. 根因

发行运行适配器每轮生成一个随机模拟器认证值，并由 `scripts/demo_runtime.py` 的既有启动路径把同一个 `EV_SIMULATOR_TOKEN` 放入 server 与 simulator 子进程环境。模拟器会使用该值发出 `simulator.status`，但服务端 `AuthService::isSimulatorTokenValid()` 调用 `RequestPreflight::roleForToken({}, token)`；后者只识别 `sim-token`、`simulator-token`、`demo-simulator-token` 三个固定值，没有读取服务端已经收到的环境配置。因此随机值在模拟器一侧正确生效，却在服务端角色判定处被拒绝，最终形成 `SIMULATOR_AUTH_FAILED` 并触发启动回滚。

## 2. 最小修正与安全边界

`roleForToken()` 保持动态 `TokenRoles` 映射最高优先级，并增加以下唯一行为：

- `EV_SIMULATOR_TOKEN` 非空时，只把与环境原值完全相等的请求 token 判为 `simulator`；不做 trim，不接受前后空白，也不再接受三个旧固定模拟器 token。
- 环境变量未设置或值为空时，继续接受原有三个固定模拟器 token，保持开发和既有测试兼容。
- ML 固定 token 分支及其他角色映射顺序不变。

这使发行每轮随机认证值成为配置存在时的唯一模拟器凭据；错误随机值和旧演示值不会因固定回退而获得模拟器角色。

## 3. TDD RED／GREEN 证据

测试使用真实 `AuthService`，并按 `RequestDispatcher` 的角色选择方式把结果交给真实 `RequestPreflight::check()` 校验有效的 `simulator.status` 请求。随机值由 UUID 在测试进程内生成；断言和测试输出均不打印该值。

生产修正前执行：

```text
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --target tst_admin_auth -j2
ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -R '^admin_auth$'
```

观察到 `5 passed, 2 failed`：

- `configuredSimulatorTokenAuthorizesStatusPreflight` 因 `result.ok == false` 失败，证明随机配置值没有获得 simulator 角色。
- `configuredSimulatorTokenRejectsEveryOtherToken` 因旧固定值仍得到成功结果而失败，证明配置存在时仍有固定凭据旁路。

加入最小生产分支后，同一目标构建成功，同一 CTest 输出 `1/1` 通过。

最终单元边界覆盖：

- 每次生成的随机配置值通过 `AuthService → RequestPreflight::check` 的 `simulator.status` 校验；
- 另一个随机值、配置值前加空白、旧固定模拟器值均返回 `AUTH_REQUIRED`；
- 非空模拟器配置不改变现有 ML token 识别；
- 环境未设置和显式空值两种情况下，三个旧固定模拟器值均继续通过。

## 4. 真实服务端 TCP 证据

重新构建 `ev_admin_server` 后，在操作系统分配的临时回环端口启动真实服务端，使用临时 SQLite 数据库和进程环境中的随机配置值发送 4 字节大端帧协议的 `simulator.status` 请求。结果：

- 配置值返回 `OK`；
- 另一个随机值返回 `AUTH_REQUIRED`；
- `demo-simulator-token` 返回 `AUTH_REQUIRED`；
- 测试仅输出 `actual_tcp_configured_simulator_auth PASS`，不输出环境值；
- 测试进程结束后终止自己的临时服务端，没有连接或操作 PID 282316／端口 9100。

## 5. 兼容回归

在显式移除 `EV_SIMULATOR_TOKEN` 的测试环境中执行：

```text
env -u EV_SIMULATOR_TOKEN ctest \
  --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --output-on-failure -R '^(admin_auth|server_tcp_p0_)'
```

最终验证同时加入 server 线程／权限集成测试，并重新构建所有相关目标：

```text
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --target ev_admin_server tst_admin_auth tst_server_threads -j2
env -u EV_SIMULATOR_TOKEN ctest \
  --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --output-on-failure -R '^(admin_auth|server_threads|server_tcp_p0_)'
git -c safe.directory=/mnt/hgfs/Desktop/SummerTermProject/worktrees/core-integration diff --check -- <本任务四个文件>
```

构建及 diff 检查返回 0；CTest `4/4` 通过，包括 `server_threads`、`admin_auth`、`server_tcp_p0_UTC`、`server_tcp_p0_Asia_Shanghai`，总耗时 17.52 秒。两组既有真实 TCP P0 回归继续使用旧固定模拟器值，证明未配置环境时的兼容路径仍可工作。

## 6. 关注点

- 环境值由 server 启动前设置，服务运行期间不应修改；Qt 进程环境本身也不是用于并发热更新的配置接口。
- 精确匹配有意不对配置值或请求值做 trim。发行启动路径已经在创建子进程前验证并规范化非空配置值；服务端不应通过额外规范化扩大凭据接受集合。
- 本任务没有改变凭据存储或传输方式。认证值仍只存在于子进程环境和请求中，不进入 manifest、状态、报告或日志输出。

## 7. 审查修正第 1 轮：启动快照与真实错误 token 夹具

### 7.1 审查问题和根因补充

首轮实现虽然要求非空配置精确匹配，但 `roleForToken()` 在每次请求时重新调用 `qEnvironmentVariable()`。如果进程环境在启动后被修改，既有服务会立即轮换到新值；如果环境被清空，还会重新启用三个旧固定 token。这不满足“服务启动时绑定且生命周期内不变”的认证边界。

同时，原有 `test_demo_runtime_live.py` 的负向夹具把 `invalid-simulator-test` 设置为整个启动命令的 `EV_SIMULATOR_TOKEN`，因此 server 和 simulator 收到同一个值。服务端开始支持配置值后，这个组合已成为合法认证对，不再能制造 `SIMULATOR_AUTH_FAILED`。

### 7.2 TDD RED

先只修改 native 测试，然后执行：

```text
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --target tst_admin_auth tst_server_threads -j2
ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --output-on-failure -R '^(admin_auth|server_threads)$'
```

结果：`0/2` CTest 通过。

- `server_threads`：`16 passed, 1 failed`，真实 `AppContext` 回环 TCP 请求在环境变更后不能继续使用启动值。
- `admin_auth`：`8 passed, 6 failed`，分别证明启动配置值丢失、环境替换值被错误接受、清空环境后三个旧别名被重新启用，以及共享 `roleForToken` 调用形态没有保存快照。
- 同一轮中动态 admin/user 冲突优先级和正常 admin/user/ML 角色测试已经通过，作为必须保持的既有行为基线。

真实三程序负向夹具的既有失败由 Jammy 完整门禁记录为 `33/34`，本机复现命令：

```text
EV_DEMO_BUILD_DIR=/home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
QT_QPA_PLATFORM=offscreen python3 -m pytest tests/scripts/test_demo_runtime_live.py -q -s
```

结果：`1 failed, 1 passed in 13.22s`；失败项是 `bad-simulator-token-rollback` 启动返回码为 0，证明负向夹具不再制造凭据不一致。

### 7.3 最小实现

- `TokenRoles` 从普通 `QHash` 别名变成可复制的角色快照：内部保存动态角色映射和私有的启动 `configuredSimulatorToken`；只有 `fromEnvironment()` 会读取环境。
- `AppContext::initialize()` 调用一次 `fromEnvironment()`，将同一份值传给 `DatabaseWorker::start()` 和 `AuthService`；登录后发布的动态角色副本继续携带原启动值。
- `AuthService` 的 simulator/ML 判定与 `tokenRoles()` 都使用成员快照；`roleForToken()` 不再读取进程环境。
- 动态角色查询仍在模拟器配置之前，因此配置值与真实 admin/user 会话值碰撞时，动态角色保持最高优先级；ML 分支顺序和行为不变。
- 真实三程序负向测试通过临时 `sitecustomize.py` 只拦截启动器进程内针对 `ev_charger_simulator` 的 `subprocess.Popen`，复制其子进程环境并替换模拟器 token。server 仍得到合法启动配置，三个被测程序仍是原生真实 ELF；生产 `demo_runtime.py` 未加入测试钩子。

### 7.4 分项 GREEN

同一 native 构建及 CTest 命令修正后结果：`2/2` 通过，`server_threads` 7.59 秒、`admin_auth` 0.02 秒，总计 7.62 秒。

真实三程序测试先在仅修正夹具、尚未修改快照生产代码时验证夹具本身，结果 `2 passed in 12.77s`；随后用重建后的服务端再次执行，结果 `2 passed in 13.01s`。两轮均保留自己的运行目录、进程退出检查、黄金库 SHA-256 检查和 SQLite 完整性检查；负向分支仍要求 `SIMULATOR_AUTH_FAILED`、`FAILED` manifest、只登记 server/simulator，并确认两者均已退出。

### 7.5 最终相关门禁

```text
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --target ev_admin_server tst_database_worker tst_demo_reset tst_server_threads \
           tst_admin_auth tst_user_flow tst_admin_runtime -j2
python3 -m py_compile tests/scripts/test_demo_runtime_live.py
env -u EV_SIMULATOR_TOKEN ctest \
  --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --output-on-failure \
  -R '^(database_worker|demo_reset|server_threads|admin_auth|user_flow|admin_runtime|server_tcp_p0_.*|demo_runtime_live)$'
git -c safe.directory=/mnt/hgfs/Desktop/SummerTermProject/worktrees/core-integration \
  diff --check -- <本任务文件>
```

结果：最终一次构建只重新编译新增测试并成功链接，Python 编译检查和 diff 检查返回 0；CTest `9/9` 通过、0 失败，总耗时 31.84 秒。其中明确包含用户要求的 `demo_runtime_live`，该项使用真实三端程序并在 12.99 秒内通过；还包括两个时区的真实 TCP P0、线程／权限、数据库 worker、reset、AuthService、用户流和外部启动 server 的管理端运行测试。
