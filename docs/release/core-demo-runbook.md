# 核心项目演示与交付操作手册

> 2026-09-06：本手册为整项目收尾中的操作基准，不是已完成的两次彩排报告。当前共享基线为 dev@97c6da1，后续候选必须记录新的准确提交。本文不操作人工维护的需求矩阵。

## 1. 演示边界

核心程序是 Qt 用户端、Qt 管理/服务端和 Qt 设备模拟器。SQLite 只由管理/服务端写入；业务历史与设备数据是项目内生成的模拟数据。腾讯地图地址解析和驾车/步行路线是外部在线能力，需要实际连通验证。

Web/ML 的代码和成果保留，默认演示不启动它们。不宣称真实支付、真实硬件、真实 GPS、实时训练或未实现的能力。

## 2. 每次候选先填写版本记录

| 项目 | 应记录的内容 |
|---|---|
| 代码 | 当前 Git 完整 SHA、分支、是否存在未提交生产代码 |
| 构建 | 原生 Linux build 绝对路径、Qt/编译器版本、构建与测试结果 |
| 数据 | core 黄金库路径、批准 SHA-256、独立运行副本路径 |
| 配置 | 服务器 host/port、地图 Key 已配置与否；不写 Key/token/密码实值 |
| 执行 | 操作人、开始/结束时间、演示机器、run ID |

`config.local.ini` 和环境变量只留本机，不提交仓库。换机验收必须在队友的独立 clone 上按文档复现，不以本机绝对源码路径可访问作为成功条件。

当前有依据的交付方式是“完整源码 clone + 已安装全局依赖 + 在该 clone 重新构建”。仓库尚未提供经验证的二进制安装布局或 Qt/WebEngine 运行库部署规则；仅拷走三个可执行文件不构成独立运行包，部分资源路径仍关联编译时的源码根。换机运行是否通过必须记录实际结果，不能由静态检查代签。

## 3. 当前可用的四个运行入口（2026-09-06）

`reset_demo.sh`、`start_demo.sh`、`smoke_test.sh`、`stop_demo.sh` 已实现。它们可从任意工作目录或脚本符号链接调用，始终使用脚本所属源码根；源码路径和build路径可含空格。环境前提仍是完整源码及已安装依赖，在该源码上生成的原生 CMake build，不能只复制三个裸二进制。入口不隐式构建或安装。

先按下节手动方法中的命令完成构建。以下示例将`/path/to/source`、`/path/to/native-build`替换为实际路径；run ID 每轮必须新建。地图 Key 由本地 `config.local.ini` 的 `[tencent] mapKey` 或 `EV_TENCENT_MAP_KEY` 注入；环境变量即使为空也优先，空值会在启动任何进程前失败。不要在报告中回显 Key。

```bash
# 既有服务端接受的开发演示值；不要把自定义环境token当作服务端配置入口。
export EV_SIMULATOR_TOKEN=demo-simulator-token
/path/to/source/scripts/reset_demo.sh --run-id demo-01
/path/to/source/scripts/start_demo.sh --run-id demo-01 --build-dir /path/to/native-build
/path/to/source/scripts/smoke_test.sh --run-id demo-01
# 人工演示完成后反序停止三端。
/path/to/source/scripts/stop_demo.sh --run-id demo-01
# 下一轮必须新run；旧目录、数据库和日志全部保留。
/path/to/source/scripts/reset_demo.sh --run-id demo-02
```

start 默认启动三个GUI；`--headless`仅使服务端增加`--server`。自动化测试另外设置`QT_QPA_PLATFORM=offscreen`，不是人工界面验收。服务端固定本机`127.0.0.1`，并覆盖三端继承的`EV_SERVER_HOST/PORT`。管理员仍需人工登录；模拟器初始暂停，到计量演示时人工按Run；用户端仍需人工登录及地图/业务操作。`CLIENT_PROCESS_ALIVE`只表示用户端在至少500ms观察期内存活，不证明登录或地图通过。

| 参数 | 适用入口 | 默认与允许值 |
|---|---|---|
| `--run-id` | 四入口 | 必填；1–64位ASCII，首位字母/数字，其余字母/数字/点/下划线/横线 |
| `--build-dir` | start | 必填；CMakeCache源码根必须匹配当前仓库，三二进制必须可执行 |
| `--port` | start | 9100；1–65535 |
| `--seed` | start | 20260901；0–4294967295 |
| `--interval-ms` | start | 3000；1000–10000 |
| `--timeout-seconds` | start/smoke/stop | start每阶段及smoke总TCP时限默认15秒，stop每进程默认10秒；允许1–60秒 |
| `--force` | stop | 默认不启用；TERM超时后再次核对身份，才对同一pidfd发KILL |

四入口都支持`--help`，未知或错误参数非零退出。最后一行固定为JSON；失败至少含`ok:false/code/message`。同仓库四操作经flock串行。reset仅创建`runtime/demo-runs/<run-id>/core-runtime.db`新副本；已存在run、符号链接、坏hash/黄金sidecar、活跃或身份不明服务端记录均拒绝。脚本只能识别本仓库清单中的进程；启动脚本外的手动服务端须由操作人先确认停止。

每轮`manifest.json`原子记录版本1、PREPARED→STARTING→RUNNING，以及失败FAILED和停止STOPPED；记录实际源码/build/run路径、源码提交、非矩阵工作树脏状态、三二进制SHA-256及PID/starttime/bootId/exe。源码SHA与二进制指纹分别记录，不自动证明二者完全对应。每端输出保存在`server.log`、`simulator.log`、`client.log`，模拟器状态在`simulator-status.json`，本轮隔离快照在`snapshot.json`。自动结果不记录Key/token。

start先确认本次服务端的绑定成功输出及严格TCP health，再等模拟器同PID且fresh的ready，最后观察用户端。失败反序TERM本轮已记录且匹配的进程，保留运行库及日志；无法确认或未退出的记录保留。stop对每端核对Linux PID、starttime、boot ID和canonical exe，经pidfd发信号，不按名字杀进程。不匹配报告`WRONG_PID`并继续处理其他端；超时默认不KILL，失败不标STOPPED。系统不支持pidfd会明确失败。

smoke只通过TCP查询既有黄金用户`13800138000`的登录、user.get、station.list、station.detail、order.current，不直连活动SQLite、不充值/建单/遥测/故障/结算。`smoke-report.json`记录动作/code/requestId摘要，不保存payload或会话token；失败也写报告（run不存在、路径不安全或文件系统不可写时只能返回失败JSON）。无active forecast且health=degraded/ok=true合法。`BASIC_SMOKE_PASS`同时保留腾讯导航、完整业务彩排、换机三个`NOT_RUN_SEPARATE_GATE`，不等于发布GO。测试记录见[四入口验证记录](../test/core-runtime-entrypoints-2026-09-06.md)。

### 保留：原有手动冷启动方法

下列是四入口之外仍可使用的原程序操作方法；保留作为历史和故障定位参考。

先关闭本次三端进程，确认原管理/服务端已退出。保留旧运行库用于排查；不覆盖仍被 SQLite 打开的文件，也不直接运行封存原件。

在仓库根目录构建（`/path/to/native-build` 需替换为 Linux 原生磁盘的实际路径）：

```bash
cmake -S . -B /path/to/native-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /path/to/native-build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /path/to/native-build --output-on-failure -j4
python3 -m pytest database/tests -q
```

为这一轮选择尚不存在的新运行副本，例如 `runtime/rehearsal-01/core.db`。已有目录不得当作下一轮清空覆盖：

```bash
python3 database/create_runtime_copy.py \
  --golden-db runtime/golden/core.db \
  --checksum runtime/golden/core.db.sha256 \
  --output runtime/rehearsal-01/core.db
```

在三个终端中依次启动，命令中的 build 路径相同，工作目录均为仓库根目录：

```bash
# 终端 1：不加 --server/--no-gui，管理窗口和 TCP 服务共用运行库。
/path/to/native-build/apps/admin-server/ev_admin_server \
  --db runtime/rehearsal-01/core.db --host 127.0.0.1 --port 9100
```

等待服务正常监听，再启动模拟器和用户端。模拟器服务 token 使用本地演示配置，不将其输出到演示证据；用户端地图 Key 沿用本地配置：

```bash
# 终端 2：EV_SIMULATOR_TOKEN 由本地配置提供。
/path/to/native-build/simulator/ev_charger_simulator \
  --host 127.0.0.1 --port 9100 --token "$EV_SIMULATOR_TOKEN"

# 终端 3：地图 Key 来自 config.local.ini 或 EV_TENCENT_MAP_KEY。
EV_SERVER_HOST=127.0.0.1 EV_SERVER_PORT=9100 \
  /path/to/native-build/apps/user-client/ev_user_client
```

管理窗口完成管理员登录；模拟器显示“已接入”才表示鉴权和业务同步成功，“已连接/等待鉴权”不等于接入成功。模拟器初始暂停，演示到计量环节时再开始运行。生产默认每 3 秒提交遥测；程序化测试的加速参数不冒充生产默认速度。

停止时按用户端 → 模拟器 → 管理/服务端的顺序退出本次程序，确认实际进程结束。不得使用按程序名广泛杀进程的命令，也不得直接复制文件来实施运行中的 `demo.reset`。

### 在线复位与冷复位的区别

在线 `demo.reset` 在本机后续分支实现，使用前以[本批服务端最终审查记录](../test/server-delivery-closeout-2026-09-06.md)确认候选状态，不能假定旧 dev 二进制已有此入口。管理“系统健康”页提供明确确认，取消不变更数据；确认会丢失当前业务演示状态，恢复批准黄金内容。

在线复位由数据库工作线程执行事务，不替换运行库文件。相同请求重试只恢复未完阶段或重放原结果，不再次清库；界面提示快照写入警告时，数据库复位已经完成，不能讲成“全部回滚”。默认使用仓库封存 core 黄金库，自定义 `--golden` 与 `--golden-hash` 必须成对提供。

正式两次彩排仍各用全新运行副本，便于独立留存日志与数据。不要在充电主线中途临时复位；若展示复位能力，将它放在业务核对完成之后，随后重新开始客户端会话并重新同步模拟器，再做新的业务操作。在线复位不会替代换机启动和人工彩排证据。

## 4. 建议现场主线（约 8 分钟，按实际答辩时长调整）

| 顺序 | 屏幕与操作 | 讲清楚什么 | 当场检查 |
|---|---|---|---|
| 1 | 展示当前核心架构 | Linux Qt 模拟手机交互，服务端权威写库，设备是可控模拟源 | 三端来自同一候选版本 |
| 2 | 手机号登录、地址输入、附近站点 | 真实自动注册/已有账户，地址由腾讯解析，距离与站点数据有来源 | 昵称/余额正确，站点和地址可读 |
| 3 | 打开应用内导航，切换驾车/步行 | Qt QWebEngineView 显示腾讯实际路线 | 两种方式均成功；旧路线/失败不会伪装成本次成功 |
| 4 | 站点详情选空闲桩、预约、开始充电 | 选桩不等于预约；服务端校验状态和订单归属 | 当前订单、桩号、站名一致 |
| 5 | 模拟器开始运行，展示遥测与三端状态 | 模拟设备产生增量电量，服务端累计计费 | 至少两个有效样本，电量/费用连续变化 |
| 6 | 停止并结算 | 停止冻结费用，结算扣款；订单完成才释放相应占用 | 余额差=订单金额；历史增加完成订单；管理营收对应增加 |
| 7 | 单独演示一个可控故障及管理员重启 | 故障/恢复意图与管理员重启分开，不能跳过状态机 | 运行中订单保留可结算状态，不重复扣款或抢占 |
| 8 | 展示测试、版本、分工与复位方法 | 技术与过程证据对应真实成果 | 不用未通过功能或 optional 成果代替核心验收 |

故障演示选择已彩排的独立步骤，不在金额核对尚未完成时临场发散。若演示充电中故障，必须同时说明：订单停止计费但仍待结算；管理员重启到 idle 也不能让别人占用尚未结算的订单桩。

## 5. 每轮必须核对的业务数值

- 用同一个 orderId 串联客户端、服务端、模拟器相关证据。
- 记录站点单价（分/kWh）、开始前余额、停止前后电量与金额、结束后余额。
- 金额核对使用服务端累计电量与站点单价，金额单位为整数分；不要从界面显示的小数截断值反推误差。
- 停止前后电量/金额不被固定值覆盖，结算余额差等于订单金额，管理今日营收增加相同金额。
- 故障或 restarting 桩结算后保留设备状态，不要求无条件变成 idle。
- 不通过手工 SQL 修复现场数据。发生不一致时保留该轮日志和运行库，记录失败并修复后重新彩排。

## 6. 外部地图失败和备用材料

在线失败时展示明确错误及重试；若显示历史成功路线，标注它属于上次成功结果，不能当作本次在线成功。备用截图或录像应提前制作并明确标注“备用材料/录制时间/版本”，不能冒充现场实时调用。

备用材料用于解释已验证能力，不能据此关闭当前腾讯在线验收缺口。需要网络的两种路线应在实际演示机器和环境下提前验证。

## 7. 两次正式彩排与交付

1. 冻结同一候选提交、同一黄金库哈希。
2. 两轮使用不同 run ID，各自从全新受校验运行副本开始。
3. 每轮保留实际步骤、订单、数值、地图结果、截图、开始/结束时间、失败与否、是否手工改库。
4. 两轮均通过且开放 P0 为零，再由实际负责人审核交付；自动化工具不代签。
5. 合并或后续修改改变生产源码时，对新候选重新取得回归和彩排证据。

交付清单：代码/构建说明、受校验黄金库、运行配置示例、启动/复位说明、测试与彩排记录、用户/管理操作说明、实际分工和答辩材料。可选 Web/ML 单列附录，不作为核心程序的启动依赖。
