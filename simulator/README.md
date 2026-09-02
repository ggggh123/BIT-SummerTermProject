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
  --host 127.0.0.1 --port 9100 --seed 20260901 --interval-ms 3000
```

## Controls

- Run/Pause — start or pause the 3-second telemetry loop.
- 故障注入 / 故障恢复 — inject/recover a fault on the selected charger.
- 刷新状态 — re-send `simulator.status` and sync the authoritative charger list.
- 准备复位 — pause telemetry and prompt for admin-side reset.
