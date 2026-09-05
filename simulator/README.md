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

## 权威状态与断线语义

- 连接成功、Run/Pause 切换、手动刷新时都会发送 `simulator.status`；此外按遥测间隔在
  1–10 秒范围内执行有界周期刷新。
- 状态刷新是单飞的：前一请求未完成时只合并一次后续刷新；每次新刷新使用新的
  `requestId`，避免服务端幂等重放旧快照。
- `telemetry.push` 或 `simulator.fault_set` 返回 `ORDER_STATE_CONFLICT` 后立即刷新完整
  权威桩快照。因此用户预约/开始/停止或管理员重启造成的外部状态变化无需依赖偶然旧快照。
- 遥测、故障和恢复事件在收到回包前保留在同一个有界队列中。断线重连先完成鉴权和
  权威快照，再按原顺序、原 `requestId` 重发；`.001/.002` 等毫秒以及 `+08:00` 均保留。

## Controls

- Run/Pause — start or pause the 3-second telemetry loop.
- 故障注入 / 故障恢复 — inject/recover a fault on the selected charger.
- 刷新状态 — re-send `simulator.status` and sync the authoritative charger list.
- 准备复位 — 暂停遥测并提示执行受控冷启动复位；此按钮不会修改或替换 SQLite。
