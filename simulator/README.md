# simulator — 充电桩设备模拟器

原责任角色：#4（SCML）。

可视化 Qt 桌面程序，通过已有 v1 TCP 协议向管理／服务端发送确定性充电遥测、故障与恢复事件。

模拟引擎只维护内存状态，不直接打开 SQLite，不提交权威订单金额或钱包余额。

候选分支新增「能量脉冲 · 遥测实验台」界面，风格与管理端／用户端同步。
详见[界面说明与留档](../docs/design/energy-pulse-qt-2026-09-08/simulator-ui.md)；这里的记录不表示已经合入远端或更新发行包。

## 构建与测试（默认 Ubuntu 22.04 / Qt 6.2）

依赖安装见[团队开发指南](../docs/development/ubuntu22.md)。日常版与完整测试版分开：

```bash
cmake --preset ubuntu22
cmake --build --preset ubuntu22 --target ev_charger_simulator
cmake --preset ubuntu22-test
cmake --build --preset ubuntu22-test
ctest --preset ubuntu22-test -R "simulator_" --output-on-failure
```

## 运行（离屏冒烟）

```bash
QT_QPA_PLATFORM=offscreen timeout 5s build/ubuntu22/simulator/ev_charger_simulator \
  --host 127.0.0.1 --port 9100 --seed 20260901 --interval-ms 3000 \
  --token sim-token
```

`--token` 必须使用当前服务端配置的 simulator token。默认空 token 只用于显式暴露
`AUTH_REQUIRED`：TCP 连通后窗口先显示“等待鉴权”，只有 `simulator.status` 成功并取得
完整权威桩快照后才显示“已接入”；鉴权失败会显示“鉴权失败”，不会把 socket 连接误报成
业务接入成功。

也可通过 `EV_SIMULATOR_TOKEN` 提供 token；显式传入 `--token`（包括显式空值）时始终
优先使用命令行值，未传该选项时才回退到环境变量。两者均为空时仍保持上述原有行为。

## 启动器运行状态文件

设置非空的 `EV_SIMULATOR_STATUS_FILE` 后，模拟器会用原子覆盖方式写入启动器指定的
本地 JSON 文件，例如：

```json
{"schemaVersion":1,"pid":123,"sessionState":"ready","updatedAt":"2026-09-06T10:00:00.000Z"}
```

`sessionState` 依次反映 `starting`、`waiting_auth`、`ready`、`auth_failed`、
`disconnected` 或 `stopped`。TCP 连接本身只会进入 `waiting_auth`；只有成功收到
`simulator.status` 回包并更新权威桩快照后才会写 `ready`，此后的每次成功状态刷新也会
更新时间。正常 Qt 退出会写 `stopped`；收到 `SIGTERM` 时最终文件可能停留在旧状态，
消费者还应独立核对 `pid` 对应的进程身份和存活状态。状态文件不会写入 token；启用状态
观测后若启动或运行期间写入失败，模拟器会明确以非零状态退出。运行期写失败时会尽力
删除本进程先前成功发布的状态文件；首次写入尚未成功时不会删除路径上已有的外部文件。
只读或故障文件系统也可能拒绝删除，此时错误会明确说明撤销失败，不能把仍在磁盘上的旧
快照解释为持续就绪。启动器必须联合校验同一活进程身份和新鲜时间，并在 start/smoke
返回前重新检查全部相关进程。

## 权威状态与断线语义

- 连接成功、启动／暂停切换、手动刷新时都会发送 `simulator.status`；此外按遥测间隔在
  1–10 秒范围内执行有界周期刷新。
- 状态刷新是单飞的：前一请求未完成时只合并一次后续刷新；每次新刷新使用新的
  `requestId`。ID 包含每个模拟器进程独有的 UUID 和进程内序号，默认时间相同的两次
  启动也不会命中服务端持久化的旧状态 ACK；未确认设备事件重发仍保留原 ID。
- `telemetry.push` 或 `simulator.fault_set` 返回 `ORDER_STATE_CONFLICT` 后立即刷新完整
  权威桩快照。因此用户预约/开始/停止或管理员重启造成的外部状态变化无需依赖偶然旧快照。
- 遥测、故障和恢复事件在收到回包前保留在同一个有界队列中。断线重连先完成鉴权和
  权威快照，再按原顺序、原 `requestId` 重发；`.001/.002` 等毫秒以及 `+08:00` 均保留。
- 生产启动的模拟时间默认锚定当前 `+08:00`，事件分配同时参考实时 clock。即使模拟器先
  启动并暂停、用户稍后才开始充电，随后的 fault/recovery 也不会沿用启动时旧时间；分配后
  仍执行严格毫秒递增。单元测试继续显式传固定初始时间且不注入实时 clock，结果保持确定。

## 界面与控制

- 启动模拟／暂停模拟：开始或暂停遥测循环（间隔由配置决定，默认 3 秒），并同步报告真实运行状态。旧版的 Run/Pause 对应这两个按钮。
- 设备表：明确区分额定功率和最新本地采样功率；未生成样本显示“—”，非充电设备实际生成的零功率显示“0.0”。点击一行查看该设备曲线与故障控制，“全部设备”返回合计。
- 故障注入／请求恢复：仅对选中且状态允许的设备可用。恢复事件仍走原协议，不代表管理员已重启设备，不在本地擅自改为 idle。
- 同步设备状态：重新发送 `simulator.status`，取得权威桩列表。TCP 连接、鉴权和运行状态分别展示。
- 准备复位 — 暂停遥测并提示执行受控冷启动复位；此按钮不会修改或替换 SQLite。
- 曲线：复用三端共享的原生 `PulseChart`；最近 600 批本地样本，支持拖动、左右键和 Home/End 回看。回看不会重发样本或修改订单。
- 事件回执：最新优先，保留 500 条。显示可读摘要，悬停可看完整原文；图上的本地生成条数不代表服务器成功入库条数。
- 默认 1360×860；1280×720 内容可滚动，左侧运行与故障控制固定可见。没有新增 Qt Charts、WebEngine 或 GPU 依赖。
