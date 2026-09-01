# 电动汽车充电平台实施计划索引

设计依据：[`docs/plans/2026-09-01-ev-charging-platform-design.md`](../../plans/2026-09-01-ev-charging-platform-design.md)

本项目包含五个可独立验收的子系统、一个公共基础层和一个集成发布层，因此拆成七份可并行执行的计划：

1. [`2026-09-01-platform-foundation.md`](2026-09-01-platform-foundation.md) — 环境、Monorepo、公共状态和 TCP 契约。
2. [`2026-09-01-admin-server.md`](2026-09-01-admin-server.md) — Qt 管理界面、QTcpServer、业务事务、唯一在线数据库写入与 Web 快照。
3. [`2026-09-01-user-client.md`](2026-09-01-user-client.md) — Qt 用户端、腾讯地图、预约充电和结算。
4. [`2026-09-01-data-simulator.md`](2026-09-01-data-simulator.md) — SQLite Schema/Seed/黄金库和可视化设备模拟器。
5. [`2026-09-01-web-dashboard.md`](2026-09-01-web-dashboard.md) — 单页 ECharts 大屏和快照降级。
6. [`2026-09-01-ml-forecasting.md`](2026-09-01-ml-forecasting.md) — 确定性 1h/6h/24h 预测、指标和发布。
7. [`2026-09-01-integration-demo-release.md`](2026-09-01-integration-demo-release.md) — 重置、启动、冒烟、彩排、角色证据和发布闸门。

## 执行波次

| 波次 | 日期 | 可并行任务 | 完成闸门 |
|---|---|---|---|
| 0 | 9/1 | Foundation Task 1–4；Data Task 1–2；同时申请腾讯 Key | 工具链通过、共享帧/报文测试通过、Schema/Seed 首版和地图最小加载成功 |
| 1 | 9/2 | Foundation Task 5；Admin Task 1–3；User Task 1–3；Data Task 3；Web Task 1–2；ML Task 1 | 18:00 契约、Schema、Fixture 冻结，登录端到端通过 |
| 2 | 9/3–4 | Admin Task 4–5；User Task 4–6；Data Task 4–6；Web Task 3–4；ML Task 2–3 | 一笔预约—充电—结算写入 SQLite；五个子系统可独立展示 |
| 3 | 9/5–6 | Admin Task 6；User Task 7；Web Task 5–6；Data Task 7A（最终历史）→ ML Task 4–6 → Data Task 7B（封装黄金库）→ Integration Task 1–2 | 最终黄金库含批准的 144 条预测，用户/管理/Web 同 run ID，五系统 V1 闭环 |
| 4 | 9/7–8 | 各模块最后任务；Integration Task 3–5 | P0 缺陷清理，9/8 12:00 功能冻结，首次完整彩排 |
| 5 | 9/9 | Integration Task 6 | 同一提交连续两次彩排通过，代码冻结，`v1.0-demo` |
| 6 | 9/10 | 仅运行 `reset_demo.sh`、`start_demo.sh`、`smoke_test.sh` 和答辩脚本 | 现场验收，不临时改代码或数据库 |

## 不可打乱的依赖

```text
Foundation 契约
├── Data Schema/Seed ──┬── Admin Database/Services ──┬── Web Snapshot
│                     │                             ├── User Integration
│                     └── ML History ── ML Publish ─┘
└── Shared Frame/Envelope ── User / Simulator / ML / Admin TCP

全部子系统 ── Integration Smoke ── Rehearsal ×2 ── Release Gate
```

- #2 是协议和在线写入权威；#4 是 Schema、Seed、配置和发布权威。六个种子站是本期固定 ML 范围；后台新增站点不进入本期预测，但不能破坏动态桩状态与营收快照。
- #3 在合并前执行同行评审；#1 维护范围、风险和答辩节奏；#5 维护模型与指标证据。
- 9 月 2 日后契约只允许兼容性新增；任何破坏性变更必须回退而不是要求其他四人同步重写。
- 任一闸门未通过时，先删除 P1 表现项，不移动 9 月 8–10 日的冻结和彩排时间。
