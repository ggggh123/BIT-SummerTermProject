# 2026-09-04 Core Integration、演示与发布计划

> **状态：当前核心执行计划；运维实现待完成。** 本计划定义 2026-09-04 起的 core acceptance/release 目标，替代旧 [`2026-09-01-integration-demo-release.md`](2026-09-01-integration-demo-release.md) 的五系统默认闸门。它不是已完成联调报告，也不创建任何 `ops/` 或 `scripts/reset_demo.sh`、`start_demo.sh`、`smoke_test.sh`、`rehearse_demo.sh`、`release_check.sh`；截至本计划写入时，这些运维入口均**待实现**。

**范围依据：** [核心交付范围重置设计](../specs/2026-09-04-core-scope-rebaseline-design.md)、[范围基线](../../management/scope-baseline.md)、[核心验收清单](../../release/core-acceptance-checklist.md)、[仓库进展审计](../../review/repository-progress-audit-2026-09-04.md) 和 [v1 接口合同](../../design/interface-contract.md)。

## 1. 目标与边界

核心演示和 release 的目标是从经校验的基础黄金数据出发，在同一提交上完成两次可重复的三系统闭环：

```text
Qt 用户端 → Qt 管理/服务端（唯一运行期 SQLite writer） ← Qt 设备模拟器
                      ↓
                 SQLite / core 黄金库
```

核心业务路径是：登录/自动注册 → 查站与查桩 → 腾讯地图地址解析与驾车/步行导航 → 预约 → 开始充电 → 模拟器遥测或故障事件 → 停止 → 结算 → 用户余额、订单与管理统计一致。

以下项目不是 core release gate：在线 ML 生产者、`forecast.publish`、active forecast、Web 静态 HTTP 服务、Web snapshot 和 Node/ML 依赖。缺少 active forecast 时，既有 `forecast.latest` 合同允许返回 `forecastRun: null`、`records: []`，不得把它判断为 core 失败。

## 2. 当前事实与前置条件

本计划只描述待达成的发布流程。以下工程事实于 2026-09-06 按本地集成候选更新；详细版本与证据见[核心修复复验记录](../../test/core-fixes-2026-09-06.md)。开始 reset/start/smoke 前，责任人仍须以当前提交的可执行证据确认条件，而不是引用旧计划的 checkbox：

- 根构建与服务端专属测试已接入，三个原始时间/计费/幂等 P0 已在本地候选修复并评审，组合 CTest 24/24。27 个 v1 action 与架构尚未全部验收，特别是专属 DatabaseWorker 和在线 `demo.reset` 仍有实现缺口；不能据此宣称最终彩排完成。
- 已跟踪的 `runtime/golden/demo.db` 是含 1 个预测批次、144 条预测的封存 optional 增强 demo 库；它保留但不是 core 基础库。
- 独立 `runtime/golden/core.db` 已由数据分支 `10034fd` 提供并引入本地候选，6 站/48 桩，配套 `core.manifest.json` 与 checksum。按 [`database/README.md`](../../../database/README.md) 校验并用 `database/create_runtime_copy.py` 创建全新运行副本；不得直接把封存原件交给会迁移/写库的服务端。
- 当前仓库没有 `ops/` 目录，也没有本计划所需的 reset/start/stop/smoke/rehearsal/release shell 入口。任何人不得把下文的目标接口当作可直接执行的现有命令。
- 正式 C++ 构建证据应在 `/tmp` 或其他 Linux 原生文件系统的独立 build 目录中产生，避免 VMware 共享目录的瞬态问题。

## 3. 待实现的 core 运维目标接口

后续单独实施时可使用以下稳定入口名称；在实现、测试和代码评审完成之前，它们全部处于**待实现**状态：

| 目标入口（待实现） | 目标职责 | core 约束 |
| --- | --- | --- |
| `scripts/reset_demo.sh` | 服务端停止后，校验并复制 core 黄金库到运行时数据库 | 仅处理工作区 `runtime/` 内已验证文件；不得手改 SQLite、不得替换 optional `demo.db` |
| `scripts/start_demo.sh` | 以已记录参数启动核心进程并保存受控 PID/日志 | 只启动管理/服务端、模拟器、用户端；不得启动 Web/ML 作为成功前置条件 |
| `scripts/stop_demo.sh` | 仅停止启动器记录且归属本工作区的进程 | 不杀死不相关 PID；失败时报告，不以广泛 kill 兜底 |
| `scripts/smoke_test.sh` | 输出机器可读/人可读的 core 冒烟结果 | 不检查 HTTP snapshot、新鲜预测、ML metrics 或 Node 资产 |
| `scripts/rehearse_demo.sh` | 从 offline reset 开始执行一次核心业务彩排并记录报告 | 每次彩排绑定当前 Git SHA、黄金库 hash、请求结果与数据一致性 |
| `scripts/release_check.sh` | 核对两次同提交的成功彩排和 release 前置条件 | 只对 core 条件给出 GO/NO-GO；optional 结果只能附录说明 |

## 4. 目标进程顺序

待实现的启动器必须使用以下顺序，并对每个等待条件给出明确超时与失败信息：

1. 确认没有已记录的核心服务端运行；执行离线 reset，验证 core 黄金库 hash 和 SQLite 完整性。
2. 启动 Qt 管理/服务端（唯一运行期 SQLite writer）；等待 `system.health` 返回可用状态和当前 schema 版本。
3. 启动 Qt 设备模拟器；等待其 `simulator.status` 心跳/状态被服务端接纳。
4. 启动 Qt 用户端；以用户界面或受控协议客户端完成登录和核心业务 smoke。
5. 停止时按用户端、模拟器、服务端的反向受控顺序记录退出结果；不得在服务端运行时以文件替换方式 reset 数据库。

Web HTTP 服务、Web snapshot writer、ML 训练与预测发布不在上述顺序中。若操作员显式选择 optional 附录演示，应在核心流程报告完成后独立启动和记录，不能污染核心通过结论。

## 5. 核心 reset 与 smoke 检查项

待实现的 `reset_demo.sh` 与 `smoke_test.sh` 至少应检查以下内容；每项必须由实际命令、测试或报告给出证据后才可标记通过：

| 分类 | 必须检查 | 不可接受的替代 |
| --- | --- | --- |
| 环境/构建 | core profile 工具链、Qt 构建与 CTest；数据库 pytest | 因缺少 Node/npm 或 ML 科学计算包而使 core 失败 |
| 数据 | core 黄金库 hash、`PRAGMA integrity_check`、运行时 DB 由服务端唯一写入 | 人工编辑运行时 SQLite 或用 optional `demo.db` 代替未校验 core 库 |
| 基础协议 | 帧/envelope、认证和 `system.health` 的已实现合同行为 | 为省事绕过 TCP 合同或直接调用数据库 |
| 用户闭环 | 用户登录、查站/查桩、腾讯地图地址解析和驾车/步行路线、预约、开始、停止、结算 | 把静态截图或未连接服务端的 UI 当作闭环证据 |
| 模拟器 | `simulator.status`、遥测与故障事件进入服务端的权威处理路径 | 模拟器/用户端直接写数据库 |
| 管理端 | 统计、用户管理、故障重启与用户/订单/余额的一致性 | 只看界面数字而不核对权威数据库状态 |
| 降级 | 断线和无 active forecast 的可解释行为，不崩溃、不伪造实时结果 | 因无预测、无 snapshot 或无 Web 服务将核心判定失败 |

## 6. 核心彩排与 release gate

核心彩排的目标操作顺序如下；实现前不应记录为“已通过”：

```text
停止受控进程
→ offline core reset
→ 启动服务端
→ 启动模拟器
→ 启动用户端
→ 登录/查站/地图导航
→ 预约/开始/遥测或故障/停止/结算
→ 管理端统计、用户与设备状态核对
→ 写入彩排报告
→ 受控停止
```

每次报告至少记录：Git commit、core 黄金库 hash、开始/结束时间、请求 ID 与响应、关键余额/订单/充电桩/统计前后值、失败原因、是否人工改库，以及 Web/ML 是否完全未参与 core 判定。两次正式报告必须来自相同 commit、不同 run ID、相同批准黄金库，并且均无开放 P0、无崩溃、无无法解释的数据矛盾。

`scripts/release_check.sh`（待实现）只能在下列条件同时为真时输出 core `GO`：

- 当前工作树/待发布内容与两次彩排记录中的 commit 一致；
- 两次 report 都通过且 run ID 不同；
- core 构建、CTest、数据库 pytest 和核心 smoke 有可审查的通过证据；
- 运行时数据库未被人工直接修改，数据完整性与核心业务结果一致；
- 腾讯地图导航 P0 有当前证据；
- 开放 P0 缺陷为零。

任一条件缺失即为 `NO-GO`。不得以已封存预测、Web 页面可打开、单独 ML 测试通过或旧五系统计划的预期输出替代上述证据。

## 7. 可选附录：Web 与 ML

完成 core 彩排和 GO/NO-GO 判定后，团队可显式选择下列加分展示。每项独立记录，失败只影响 optional 演示说明，不倒灌 core release：

- **Web optional profile：** 启动静态 HTTP 服务、执行 Node 测试、验证 snapshot 消费与 fallback。Web 不访问 SQLite，也不成为 core 进程。
- **ML optional profile：** 执行离线训练/pytest、准备并经 `forecast.publish` 发布 144 条预测；发布必须遵守冻结合同，不能直接写 SQLite。
- **兼容性：** 若 optional profile 产生 active forecast 或 Web snapshot，必须完整遵守 [`interface-contract.md`](../../design/interface-contract.md)；若未启用，core 合法返回空预测且不要求 snapshot。

附录结果必须表述为“独立验证/可选演示”或“待实现”，不得写成核心端到端联调已经完成。

## 8. 角色与证据

| 角色 | core 集成责任 | 发布证据 |
| --- | --- | --- |
| #1 PM | 范围、日程、GO/NO-GO 材料与答辩组织 | 本计划、范围基线、两次彩排汇总 |
| #2 TL | 服务端 P0、协议、事务和唯一写库 | 服务端构建/测试、协议与数据一致性证据 |
| #3 PRL | 用户端地图/业务流程、同行评审 | 用户闭环、地图和评审记录 |
| #4 SCML | core 黄金库、模拟器、配置/版本 | hash、SQLite 验证、模拟器状态/事件证据 |
| #5 PE | 核心联调补位与测试 | smoke/彩排执行证据；ML 成果另列 optional 附录 |

历史五系统计划、Web/ML 计划和封存 `demo.db` 都是保留成果；它们不能取代上述角色在核心闭环中应提供的当前证据。
