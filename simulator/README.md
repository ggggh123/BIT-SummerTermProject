# simulator — EV charger device simulator

Responsible: #4 (SCML).

A visible Qt application that publishes deterministic charging telemetry and
fault events through the frozen v1 TCP protocol to the Qt admin/server.

The simulator is an in-memory state machine only. It **never** opens SQLite and
never submits authoritative order amounts or wallet balances.

## Build & test (on the Ubuntu host)

```bash
cmake --preset debug
cmake --build --preset debug --target ev_charger_simulator
ctest --preset debug -R "simulator_" --output-on-failure
```

## Run (offscreen smoke)

```bash
QT_QPA_PLATFORM=offscreen timeout 5s build/debug/simulator/ev_charger_simulator \
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
观测后若启动或运行期间写入失败，模拟器会明确以非零状态退出。

## 权威状态与断线语义

- 连接成功、Run/Pause 切换、手动刷新时都会发送 `simulator.status`；此外按遥测间隔在
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

## Controls

- Run/Pause — start or pause the 3-second telemetry loop.
- 故障注入 / 故障恢复 — inject/recover a fault on the selected charger.
- 刷新状态 — re-send `simulator.status` and sync the authoritative charger list.
- 准备复位 — 暂停遥测并提示执行受控冷启动复位；此按钮不会修改或替换 SQLite。
