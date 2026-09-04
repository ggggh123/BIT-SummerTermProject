# 核心交付范围重置与可选成果保留设计

> 状态：已确认
> 决策日期：2026-09-04
> 决策人：项目组
> 最终截止：2026-09-10
> 被替代基线：`docs/plans/2026-09-01-ev-charging-platform-design.md` 中关于“五系统全部作为验收硬门槛”的部分

## 1. 背景与决策

项目最初按 Qt 用户端、Qt 管理/服务端、SQLite 与模拟器、Web ECharts 大屏、ML 智能分析五条交付线推进。2026-09-04 项目组重新评估截止时间、当前集成状态与答辩收益后，确认：

- Web 大屏不再属于 9 月 10 日核心验收要求；
- ML 训练、预测和在线发布不再属于 9 月 10 日核心验收要求；
- 已完成的 Web、ML 代码、测试、数据、文档与提交必须保留，不删除、不伪装为未发生；
- 团队资源集中到用户端、管理/服务端、数据库/模拟器组成的可运行核心闭环；
- 原始需求附件保持只读，不因内部范围调整而篡改。

本设计采用“核心交付 + 可选成果保留”的双层结构。范围调整是发布门槛和答辩主线的变化，不是对历史工作的否定，也不是一次破坏性协议清理。

## 2. 目标与非目标

### 2.1 目标

1. 建立一份自 2026-09-04 起生效的当前范围基线。
2. 让 README、项目计划、风险表、接口合同和发布闸门使用同一口径；需求矩阵由队员人工维护并自行保证其口径。
3. 默认构建、环境检查、冒烟和彩排只依赖核心系统。
4. 保留 Web/ML 已完成成果及其独立验证能力。
5. 保留原五系统设计和 HTML 架构图作为历史基线，并新增当前核心架构图。
6. 统一云端协作路径为 `feat/* -> dev -> main`。
7. 用独立、可审查的 PR 完成范围重置和可选成果归档，不直接修改共享分支。

### 2.2 非目标

- 不删除 `dashboard/`、`ml/`、预测表、预测协议 action、fixture、历史计划或远端功能分支。
- 不修改 `references/` 中的原始 `.doc/.docx` 文件。
- 不在范围整理 PR 中补写服务端业务、修复模拟器或改动用户端业务逻辑。
- 不把尚未完成的真实联调写成“已完成”。
- 不要求 9 月 10 日现场训练模型、发布预测或启动 Web 静态服务器。
- 不将静态封存预测表述成实时机器学习结果。

## 3. 当前交付架构

### 3.1 核心默认层

```text
Qt 用户端（含腾讯地图 Web API 导航）
        |
        | 长度前缀 JSON/TCP
        v
Qt 管理/服务端（唯一运行时 SQLite writer） -- 串行事务 --> SQLite / 黄金库
        ^
        | 状态/遥测/故障
        |
Qt 设备模拟器
```

核心验收链路为：

```text
登录/自动注册
  -> 查站与查桩
  -> 腾讯地图驾车/步行导航
  -> 预约
  -> 开始充电
  -> 模拟器遥测或故障事件
  -> 停止
  -> 结算
  -> 用户余额、订单和管理统计一致
```

核心 Definition of Done：从受校验的黄金数据开始，核心链路在同一提交上连续两次通过；无人工直接修改运行时数据库；无崩溃或无法解释的数据矛盾；开放 P0 缺陷为零。

### 3.2 可选参考层

```text
Optional Web
  dashboard/ 静态 ECharts 页面、fixture、fallback、轮询和独立 Node 测试

Optional ML
  ml/ 离线训练/预测管线、模型与指标工件、发布客户端和独立 pytest
```

可选层遵循以下原则：

- 不加入顶层 CMake 默认构建图；
- 不进入核心环境检查、核心 smoke、核心彩排和核心 release gate；
- 可保留独立测试与手动演示说明；
- 缺少 active forecast 是合法核心运行状态；
- 缺少 Web snapshot writer 或静态 HTTP 服务不导致核心发布失败；
- 将来启用时必须使用显式 optional profile，而不是悄悄恢复为硬依赖。

## 4. 兼容性边界

### 4.1 必须保留

- `forecast.publish`、`forecast.latest` action 与角色权限常量；
- `forecast_enabled`、`station_hourly_history`、`forecast_runs`、`forecasts` 和 `snapshot_meta`；
- 用户端对 `forecastRun: null`、`records: []` 的合法降级处理；
- 已封存黄金库中的 144 条预测及 manifest/hash；
- Web 快照合同和预测合同的历史/可选扩展说明；
- Web/ML 实施计划、测试记录和功能分支提交。

这些内容属于向后兼容能力和已有成果。取消默认验收不能通过删除字段、表或 action 实现。

### 4.2 核心运行允许的降级

- 服务端可返回无 active forecast 的合法响应；
- 用户端显示“暂无预测”，但找站、导航和充电不受影响；
- 核心黄金库可以保留静态 last-good 预测，也可以在新的基础 profile 中没有 active forecast；
- Web snapshot 和在线 `forecast.publish` 均不属于核心启动的前置条件。

## 5. 文档治理

### 5.1 当前规范性文件

以下文件必须更新为当前口径：

- `README.md`
- `docs/management/scope-baseline.md`
- `docs/management/project-plan.md`
- `docs/management/risk-register.md`
- `docs/management/daily-log.md`
- `docs/management/environment-matrix.md`
- `docs/design/interface-contract.md`
- `docs/superpowers/plans/README.md`
- `docs/superpowers/plans/2026-09-01-integration-demo-release.md`
- `database/README.md`

当前需求矩阵工作簿不属于本 PR 的规范性修改对象。其内容、状态、样式、验收签字和后续维护均由队员人工负责。

README 必须清楚区分“当前核心交付”“可选成果”“历史基线”，并给出当前架构与范围变更记录入口。

### 5.2 历史文件

以下内容不重写历史过程，只在文件头或入口增加醒目的状态说明：

- `docs/plans/2026-09-01-ev-charging-platform-design.md`
- `docs/plans/2026-09-03-optional-features-proposal.md`
- `docs/superpowers/plans/2026-09-01-web-dashboard.md`
- `docs/superpowers/plans/2026-09-01-ml-forecasting.md`
- `docs/design/five-system-architecture.html`

原五系统 HTML 必须保留原路径，作为 2026-09-01 历史基线。另新增当前核心架构 HTML，README 默认指向新页面，同时保留旧页面入口。

### 5.3 审计与决策证据

新增：

- 2026-09-04 范围变更记录；
- 当前分支/模块进展审计报告；
- 当前三核心系统架构图；
- 核心验收清单或集成闸门说明。

审计报告必须区分：已提交、已构建、已测试、已独立运行、已真实端到端联调，不以代码量或提交数代替完成度。

## 6. 需求矩阵：已移交队员人工维护

Task 1 已将 `origin/main@fc7c67e` 的 `需求矩阵-第1组-王浩恩.xlsx` 同步到本整理基线；该工作簿只作为已完成上游同步的历史输入保留。自 2026-09-04 的用户覆盖决策起，工作簿的内容、状态、样式、验收签字及后续维护完全由队员人工负责。

原自动编辑提交 `3f1c2c5 docs(requirements): align matrix with core acceptance` 已由恢复提交 `ef9fb99 Revert "docs(requirements): align matrix with core acceptance"` 完整恢复。本 PR 不再自动读取、检查、编辑、导出、渲染或验证该工作簿，也不就其行号、公式、状态或版式作出自动化承诺。任何后续矩阵调整均应由队员在人工维护流程中完成。

## 7. 环境与发布门槛

默认 core profile 仅检查核心所需工具：Git、CMake、C++、Qt Core/Network/Widgets/WebEngine/Charts/Test、Python 及 pytest 等核心数据测试依赖。

- Node/npm 移入 Web 可选检查；
- NumPy、pandas、scikit-learn、joblib 移入 ML 可选检查；
- `check_env.sh` 提供显式 `--with-web`、`--with-ml` 或同等清晰接口；
- `bootstrap.sh` 保留当前一键全量安装能力，同时提供不会把 Web/ML 描述成核心必需的分组说明；
- 核心 release gate 不检查 Web HTTP、snapshot、新鲜预测、ML metrics、last-good 或四方 run ID；
- optional profile 可以继续执行其独立测试和一致性检查。

## 8. 人员与责任口径

五名学生的正式角色不因范围收缩而删除：

| 编号 | 正式角色 | 当前主线职责 | 历史/可选成果 |
|---|---|---|---|
| #1 | PM | 范围、排期、文档、答辩组织、验收材料 | Web 大屏成果维护 |
| #2 | TL | 管理/服务端、核心协议、端到端集成 | 可选快照/预测接口 |
| #3 | PRL | 用户端、腾讯地图、同行评审、缺陷把关 | 用户端预测展示兼容能力 |
| #4 | SCML | SQLite、模拟器、黄金库、版本与发布 | 历史/预测数据资产 |
| #5 | PE | 核心联调测试、服务端或演示补位 | ML 管线成果维护 |

文档只记录可验证工作。#5 的具体补位任务应由项目计划逐项登记，不反向伪造已完成记录。

## 9. 云端仓库整理

### 9.1 第一批：范围重置 PR

- 基于最新 `origin/dev` 创建隔离分支；
- Task 1 已完成 `origin/main@fc7c67e` 工作簿的文件名和版本同步；后续工作不再处理该工作簿；
- 只修改文档、HTML 以及范围相关的环境脚本；
- 不修改业务实现；
- 通过检查后推送并创建目标为 `dev` 的 PR。

### 9.2 第二批：可选成果归档 PR

- 保留 `feat/web@1d07976` 与 `feat/ml@2b1563d` 作为来源证据；
- 不直接 merge 两个落后分支；
- 选择性引入已验证的 `dashboard/` 与 `ml/` 产品代码、测试和 README；
- 以当前 `dev` 中已有的数据库、fixture、vendor 和合同为准，不回退或复制它们；
- 不夹带旧需求矩阵、旧 WBS、个人工具技能目录或无关 `.gitignore` 变更；
- 作为独立 PR 审查，可选模块测试不进入 core gate。

不删除 `feat/web`、`feat/ml`、`feat/data`、`feat/server` 或 `feat/user`。服务端必须先同步最新 `dev` 并完成核心 P0 后，才能进入独立功能 PR。

## 10. 验证要求

### 10.1 范围重置 PR

- Markdown 链接和关键口径扫描；
- HTML 可本地打开并人工检查；
- `scripts/check_env.sh` core、Web、ML 组合测试；
- shell 语法检查；
- 数据库 pytest；
- 在 Linux 原生构建目录执行完整 CMake build 与 CTest；
- `git diff --check`；
- 确认 `references/` 和业务实现未被修改。

### 10.2 可选成果归档 PR

- Web：全部 Node 测试通过，静态入口和 fallback 返回 HTTP 200；
- ML：全部测试无失败，离线 12,960 行历史到 144 条预测链路可复现；
- 核心 CMake build、CTest 与数据库测试不回退；
- 顶层 CMake 不新增 Web/ML 硬依赖；
- core 环境检查在没有 Node/ML 依赖的语义下仍成立。

由于 VMware 共享目录可能产生时钟偏差和瞬态对象文件损坏，正式 C++ 构建证据使用 `/tmp` 或其他 Linux 原生文件系统中的独立 build 目录。

## 11. 风险与回退

| 风险 | 控制措施 |
|---|---|
| 新口径与历史文档混淆 | README 默认指向当前基线；历史文件加状态横幅但不改写正文 |
| 可选化被误解为删除 | 保留代码、表、action、测试、分支和精确来源 SHA |
| 旧分支合并带回过期文档 | 选择性归档，不直接 merge Web/ML 分支 |
| 需求矩阵与当前范围口径不一致 | 工作簿由队员人工维护、人工验收；本 PR 不自动处理该文件 |
| 环境检查仍暗中依赖 ML/Web | core 与 optional 参数分别验证 |
| 范围调整掩盖服务端 P0 | 审计报告继续将服务端核心闭环列为最高优先级 |

任一整理 PR 均可通过关闭 PR 或回退其独立提交恢复；不会重写共享分支历史，也不会删除远端成果。
