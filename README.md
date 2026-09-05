# BIT-SummerTermProject

充电桩管理与演示平台小学期项目。

## 当前交付口径（2026-09-04 生效）

9 月 10 日的核心验收以可运行的三条交付线为准：Qt 用户端、Qt 管理/服务端、SQLite 与 Qt 模拟器。核心目标是从受校验的黄金数据开始，完成用户端—服务端—数据库/模拟器的闭环；它替代了 2026-09-01 历史基线中“五系统同时作为硬门槛”的表述。

## 三条核心交付线

1. **Qt 用户端（#3 PRL）**：11 位手机号登录/自动注册、查站查桩、腾讯地图 Web API 地址解析与 QWebEngineView 驾车/步行导航，以及预约到结算的用户流程。
2. **Qt 管理/服务端（#2 TL）**：长度前缀 JSON/TCP、业务状态机、唯一运行时 SQLite 写入、管理统计、故障重启与用户管理；服务端 P0 是当前 NO-GO。
3. **SQLite 与 Qt 模拟器（#4 SCML）**：版本化 Schema、受校验黄金库、模拟器状态/遥测/故障和数据一致性。

## Web 与 ML：保留的可选参考成果

`dashboard/` Web ECharts 大屏与 `ml/` 离线训练、预测和发布能力均保留代码、测试、数据、提交与独立演示价值，但退出核心验收、核心彩排和发布硬闸门。缺少 Web snapshot、静态 HTTP 服务、在线预测生产者或 active forecast 都不阻塞核心交付；用户端应将无预测显示为“暂无预测”，而不影响找站、导航或充电。

## 当前文档入口

- [当前核心交付架构](docs/design/core-system-architecture.html)
- [范围基线 v2](docs/management/scope-baseline.md) 与 [2026-09-04 范围变更记录](docs/management/scope-change-2026-09-04.md)
- [仓库进展审计（2026-09-04）](docs/review/repository-progress-audit-2026-09-04.md)
- [接口合同](docs/design/interface-contract.md) 与 [当前实施计划索引](docs/superpowers/plans/README.md)
- [核心验收清单](docs/release/core-acceptance-checklist.md)
- [2026-09-01 历史五系统架构图](docs/design/five-system-architecture.html)（历史基线，不是当前唯一架构）

## 正式角色与当前职责

| 编号 | 正式角色 | 当前主线职责 | 历史/可选成果 |
|---|---|---|---|
| #1 | PM | 范围、排期、文档、答辩组织、验收材料 | Web 大屏成果维护 |
| #2 | TL | 管理/服务端、核心协议、端到端集成 | 可选快照/预测接口 |
| #3 | PRL | 用户端、腾讯地图、同行评审、缺陷把关 | 用户端预测展示兼容能力 |
| #4 | SCML | SQLite、模拟器、黄金库、版本与发布 | 历史/预测数据资产 |
| #5 | PE | 核心联调测试、服务端或演示补位 | ML 管线成果维护 |

## 分支路径

日常协作路径统一为 `feat/* -> dev -> main`。功能分支先面向 `dev` 审查和集成；`main` 仅接收经审查的稳定里程碑。Web/ML 的既有功能分支保留为来源证据，不直接作为核心发布依赖。

## Qt 用户端运行入口

环境配置、构建测试、离线行为和答辩演示步骤见 [用户端 README](apps/user-client/README.md)。
