# 电动汽车充电平台实施计划索引

> **当前口径：** 2026-09-04 起，默认交付采用 core profile。当前范围依据为 [核心交付范围重置设计](../specs/2026-09-04-core-scope-rebaseline-design.md) 与 [范围重置实施计划](2026-09-04-core-scope-rebaseline.md)。Web 和 ML 成果、测试和原计划均保留，但不属于默认验收或发布闸门。

## 当前核心执行计划

以下计划构成当前默认主线；目标是 Qt 用户端、Qt 管理/服务端、SQLite/设备模拟器的可运行闭环。核心发布入口是 2026-09-04 计划，而不是旧五系统集成计划。

1. [`2026-09-01-platform-foundation.md`](2026-09-01-platform-foundation.md) — Foundation：环境、公共状态和 TCP 契约。
2. [`2026-09-01-data-simulator.md`](2026-09-01-data-simulator.md) — Data/Simulator：SQLite schema、seed、黄金库与设备模拟器。
3. [`2026-09-01-admin-server.md`](2026-09-01-admin-server.md) — Admin/Server：Qt 管理/服务端、串行事务和唯一运行期 SQLite writer。
4. [`2026-09-01-user-client.md`](2026-09-01-user-client.md) — User：Qt 用户端、腾讯地图导航、预约、充电与结算。
5. [`2026-09-04-core-integration-demo-release.md`](2026-09-04-core-integration-demo-release.md) — Core Integration：核心 reset/start/smoke/rehearsal/release 目标与待实现的运维入口。
6. [`2026-09-06-core-integration-next.md`](2026-09-06-core-integration-next.md) — 已执行修复与真实 Qt 组合联调的证据、尚未完成的人工/地图/UI/彩排边界。
7. [`2026-09-06-server-delivery-closeout.md`](2026-09-06-server-delivery-closeout.md) — 基于 PR #10 合并后的 dev，实施线程隔离、管理/网络统一调度和合同在线复位。

## 可选参考计划

这些计划对应已保留的独立成果。仅在明确启用相应 optional profile 时执行其测试或演示；它们不阻塞 core acceptance、两次核心彩排或 core release。

1. [`2026-09-01-web-dashboard.md`](2026-09-01-web-dashboard.md) — Web dashboard、快照消费与独立 Node 测试。
2. [`2026-09-01-ml-forecasting.md`](2026-09-01-ml-forecasting.md) — ML 离线训练、预测发布与独立 pytest。

## 历史计划

- [`2026-09-01-integration-demo-release.md`](2026-09-01-integration-demo-release.md) — 原五系统集成与发布计划；仅保留为历史实施证据。
- [`2026-09-01-project-kickoff.md`](2026-09-01-project-kickoff.md) — 原项目启动计划。
- [`../../plans/2026-09-01-ev-charging-platform-design.md`](../../plans/2026-09-01-ev-charging-platform-design.md) — 2026-09-01 五系统 v1 设计基线。
- [`../../plans/2026-09-03-optional-features-proposal.md`](../../plans/2026-09-03-optional-features-proposal.md) — 已终止的 P1 提案，保留 F1–F8 决策证据。

## 当前依赖与默认发布路径

```text
Foundation（冻结 v1 兼容合同）
├── Data/Simulator（核心黄金数据、遥测/故障）
│   └── Admin/Server（唯一运行期 SQLite writer）
│       └── User（登录、地图、预约→充电→结算）
└── Core Integration（核心 smoke → 同提交彩排 ×2 → Core Release Gate）

Web Dashboard ─────── optional profile：独立快照演示/Node 测试（不指向默认 Release Gate）
ML Forecasting ────── optional profile：离线训练/发布/pytest（不指向默认 Release Gate）
```

- core 不要求在线 ML 生产者、Web HTTP 服务、Web snapshot 或 active forecast；无 active forecast 是合法降级状态。
- 如果启用 optional profile，仍须遵守冻结的 [v1 接口合同](../../design/interface-contract.md)，并按各自计划执行独立验证；通过 optional 验证不替代 core 集成验收。
- 现有旧计划中的 checkbox、命令与测试记录是历史或可选证据，不得据此推断尚未实现的运维脚本已经可用。当前真实实现/联调状态查阅 [2026-09-06 团队交接状态](../../management/core-integration-handoff-2026-09-06.md)；[2026-09-04 仓库进展审计](../../review/repository-progress-audit-2026-09-04.md)保留为历史检查。
