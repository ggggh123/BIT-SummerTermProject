# BIT-SummerTermProject

充电桩管理与演示平台小学期项目。

## 当前团队入口（2026-09-08）

> 本地候选分支 `feat/energy-pulse-qt-ui` 已按选定方案 2 实现双端“能量脉冲”UI；效果、采样边界和本机验证见[实现记录](docs/design/energy-pulse-qt-2026-09-08/README.md)，视觉对照见 [design-qa.md](design-qa.md)。基于 `dev@7bbfc0e`，并不表示此候选已合入远端；旧发行包和启动入口尚未替换。

> 同日候选 UI 增量：模拟器已完成同主题控制台；电桩状态与系统健康已完成结构级改造，见[设备阵列与故障处置](docs/design/operations-qt-2026-09-08/README.md)。其余管理页保持主题统一后的既有布局，不宣称全部逐页重做。

**默认 Ubuntu 22.04 / Qt 6.2 / GCC 11 / CMake 3.22。** 本机 25.04 / Qt 6.8 不再作为默认安装教程。先阅读 [22.04 开发指南](docs/development/ubuntu22.md)，不要为编译项目升级整个系统。

`dev` 已于 2026-09-07 更新到快照 `6360bd1`，包含三端 UI、运行入口、管理端日志分页和健康分区。`fix/ubuntu22-team-baseline` 已整合该快照，通过 [PR #11](https://github.com/ggggh123/BIT-SummerTermProject/pull/11) 补齐 22.04 构建/CI、启动鉴权与便携发行修复，并修正新增页面的集成问题；不是把队友快照覆盖成旧版本。PR 是否合并以 GitHub 为准。旧报告及下方 9 月 6 日状态作为历史记录保留，不用来判断最新代码或人工验收状态。

在 Ubuntu 22.04 的独立 clone 根目录执行：

```bash
bash scripts/bootstrap.sh
bash scripts/check_env.sh --strict
cmake --preset ubuntu22
cmake --build --preset ubuntu22
```

默认只编译三个程序，Ninja 增量构建、最多并行 2。完整验证使用独立的 `ubuntu22-test` 预设；Qt Creator 配置、拉取本轮分支、内存不足和环境报错处理见[详细指南](docs/development/ubuntu22.md)。启动步骤见[演示操作手册](docs/release/core-demo-runbook.md)。

### 快速开始（新环境一键部署）

不想逐步执行上面流程的成员/新用户，用一键脚本（内部等价于 bootstrap → 环境检查 → `ubuntu22-test` 构建，可选 `--start` 拉起三端）：

```bash
python3 scripts/quickstart.py          # 依赖安装 → 环境检查 → 配置 → 全量编译
python3 scripts/quickstart.py --start  # 同上，完成后自动拉起三端演示
```

- 依赖安装按需触发（apt 系统包无法随文件夹分发，脚本自动检测缺失并调用 `scripts/bootstrap.sh` 安装）；非 22.04 的 Ubuntu（如 25.04）会自动透传 `--allow-other-ubuntu`，属尽力兼容而非验证基线；
- 自动处理 Windows 复制导致的 shell 脚本 CRLF 换行问题；
- `--start` 成功后应看到管理端、用户端、模拟器三个窗口；腾讯地图 Key 已内置，无需配置；
- 环境兼容性问题与解决方法详见 [环境兼容性汇总](docs/test/ubuntu22-qt62-compatibility-2026-09-07.md)。

### 历史集成记录（不是当前环境要求）

> **2026-09-06 共享集成基线：** [PR #10](https://github.com/ggggh123/BIT-SummerTermProject/pull/10) 已合入 `dev@97c6da1`。本机后续分支 `feat/core-delivery-20260906` 已实现 DB worker、在线 reset 和恢复加固；用户批准容量补充后，`6863b36` [限定评审](docs/review/server-delivery-review-2026-09-06.md)已关闭 I1/N1。`a867ca6` 继续落实[充电与结算竖屏 UI](docs/test/user-charge-ui-2026-09-06.md)，完整构建、CTest **29/29**、数据库 **15/15** 通过。这些后续提交仍是本机候选，尚未共享合入 dev。
>
> **本机运行交付增量（2026-09-06）：** 四个reset/start/smoke/stop入口已实现，真实三程序offscreen基础流程及坏模拟器token回滚已有[验证记录](docs/test/core-runtime-entrypoints-2026-09-06.md)。历史/账户/导航外观收尾、换机验证、人工三端/腾讯地图联调与同 SHA 双彩排仍待完成。按[交付路线](docs/management/project-plan.md)推进，尚未宣布 GO 或发布到 main；此前[综合审查中断记录](docs/review/core-fixes-review-2026-09-06.md)继续保留。

## 当前交付口径（2026-09-04 生效）

9 月 10 日的核心验收以可运行的三条交付线为准：Qt 用户端、Qt 管理/服务端、SQLite 与 Qt 模拟器。核心目标是从受校验的黄金数据开始，完成用户端—服务端—数据库/模拟器的闭环；它替代了 2026-09-01 历史基线中“五系统同时作为硬门槛”的表述。

## 三条核心交付线

1. **Qt 用户端（#3 PRL）**：11 位手机号登录/自动注册、查站查桩、腾讯地图 Web API 地址解析与 QWebEngineView 驾车/步行导航，以及预约到结算的用户流程。
2. **Qt 管理/服务端（#2 TL）**：长度前缀 JSON/TCP、业务状态机、唯一运行时 SQLite 写入、管理统计、故障重启与用户管理；本分支包含线程隔离、在线复位和桌面 UI 精修，远端集成状态以 PR 为准。
3. **SQLite 与 Qt 模拟器（#4 SCML）**：版本化 Schema、受校验黄金库、模拟器状态/遥测/故障和数据一致性。

## Web 与 ML：保留的可选参考成果

`dashboard/` Web ECharts 大屏与 `ml/` 离线训练、预测和发布能力均保留代码、测试、数据、提交与独立演示价值，但退出核心验收、核心彩排和发布硬闸门。缺少 Web snapshot、静态 HTTP 服务、在线预测生产者或 active forecast 都不阻塞核心交付；用户端应将无预测显示为“暂无预测”，而不影响找站、导航或充电。

## 当前文档入口

- [Ubuntu 22.04 团队开发指南](docs/development/ubuntu22.md) 与 [环境基线](docs/management/environment-matrix.md)
- [2026-09-08 dev 更新与 PR 冲突整合记录](docs/test/dev-pr-refresh-2026-09-08.md)
- [便携发行说明](docs/release/portable-release.md)（与源码编译分开；含私有配置的发行包不上传 Git）
- [当前核心交付架构](docs/design/core-system-architecture.html)
- [范围基线 v2](docs/management/scope-baseline.md) 与 [2026-09-04 范围变更记录](docs/management/scope-change-2026-09-04.md)
- [仓库进展审计（2026-09-04）](docs/review/repository-progress-audit-2026-09-04.md)
- [核心阻塞修复与同版本复验（2026-09-06）](docs/test/core-fixes-2026-09-06.md)、[最终综合审查归档（2026-09-06）](docs/review/core-fixes-review-2026-09-06.md) 与 [团队交接状态](docs/management/core-integration-handoff-2026-09-06.md)
- [本批服务端交付审查](docs/review/server-delivery-review-2026-09-06.md)（保留历史发现，新增用户批准后的修复与关闭证据）
- [充电/结算 UI 与实际 Qt 截图](docs/test/user-charge-ui-2026-09-06.md)（受控 UI 响应，不代替真实三端联调）
- [接口合同](docs/design/interface-contract.md) 与 [当前实施计划索引](docs/superpowers/plans/README.md)
- [核心验收清单](docs/release/core-acceptance-checklist.md)
- [核心演示与交付操作手册](docs/release/core-demo-runbook.md)（四运行入口、保留手动流程、八分钟主线、数值核对和双彩排要求）
- [四入口与真实三程序基础验证](docs/test/core-runtime-entrypoints-2026-09-06.md)（独立运行副本、身份回收、TCP只读业务冒烟；不代替腾讯/人工/换机门禁）
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
