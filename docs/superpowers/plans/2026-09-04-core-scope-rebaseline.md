# 核心交付范围重置实施计划

> **执行方式：** 使用 `superpowers:subagent-driven-development` 逐任务实现、逐任务复核，并在全分支完成后进行整体代码审查。

**目标：** 在不删除 Web/ML 成果、不篡改原始需求附件的前提下，把仓库的现行范围、计划、需求矩阵、环境门槛和架构入口统一为“三条核心交付线 + 两项可选参考成果”。

**架构：** 默认 core profile 只包含 Qt 用户端、Qt 管理/服务端、SQLite/设备模拟器；Web 与 ML 保留为独立 optional profile。旧五系统设计与 HTML 作为历史证据保留，新文件承载当前架构和验收口径。范围重置通过独立 PR 进入 `dev`，不直接修改共享分支。

**技术栈：** Markdown、HTML/CSS/JavaScript、POSIX shell、CMake/CTest、Python/pytest、Node.js（仅可选 Web 检查）、`.xlsx` 与 `@oai/artifact-tool`。

**设计规范：** `docs/superpowers/specs/2026-09-04-core-scope-rebaseline-design.md`

## 全局约束

- 所有正文和新增管理材料优先使用中文。
- `references/` 中的原始 `.doc/.docx` 文件不得修改、移动或重命名。
- 不删除 `dashboard/`、预测数据库表、预测协议 action、历史计划或任何远端功能分支。
- 第一批 PR 不修改 `apps/`、`simulator/src/`、`database/*.py`、`database/schema.sql` 或 `shared/` 中的业务实现。
- 原 `docs/design/five-system-architecture.html` 保留原路径；只增加历史状态说明，不删除原图主体。
- 新的当前架构页面使用独立文件，不覆盖旧 HTML。
- 最新需求矩阵必须以 `origin/main@fc7c67e` 的 `需求矩阵-第1组-王浩恩.xlsx` 为输入，保持该文件名、两张工作表和现有样式。
- Web/ML 行保留但退出核心验收；不得把已经完成的成果写成“未发生”，也不得把未联调能力写成“已完成”。
- 默认 `scripts/check_env.sh` 不得因缺少 Node/npm、NumPy、pandas、scikit-learn 或 joblib 失败；可选参数分别启用 Web/ML 检查。
- 保留当前一键全量 APT 安装能力；全量安装是便利超集，不代表所有包都是 core 必需项。
- C++ 正式验证使用 `/tmp` 下的 Linux 原生 build 目录，避免 VMware 共享目录时钟偏差产生假失败。
- 不直接 push 到 `dev` 或 `main`；只推送当前整理分支并创建目标为 `dev` 的 PR。
- 每个提交前执行 `git diff --check`，不得夹带审计构建目录、缓存、临时脚本或用户其他改动。

## Task 1：吸收最新 main 需求矩阵并锁定整理基线

**文件：**

- 删除：`01需求矩阵-第1组-王浩恩.xlsx`（由上游提交的重命名产生）
- 新增：`需求矩阵-第1组-王浩恩.xlsx`（来自 `fc7c67e`，本任务不编辑内容）

**输入：**

- 当前整理分支基于 `origin/dev@04e45fc`；
- 已审计上游提交：`origin/main@fc7c67e`；
- `c909b67..fc7c67e` 只包含需求矩阵重命名与新版二进制内容。

- [ ] **Step 1：再次确认精确上游差异**

```bash
git fetch --prune origin
git diff --name-status c909b67ee66f203c82a1fca5d390fc0f1ef3f8ec fc7c67e
git log -1 --format='%H %an %s' fc7c67e
```

预期：只显示旧工作簿删除和新工作簿新增；提交作者为 SpiderBoy，主题为刷新需求矩阵。

- [ ] **Step 2：合并精确提交而非可移动分支名**

```bash
git merge --no-ff fc7c67e -m "merge: sync refreshed requirement matrix from main"
```

预期：不修改设计规范，不产生业务代码冲突。

- [ ] **Step 3：只读导入并渲染新版矩阵**

使用 `@oai/artifact-tool` 导入 `需求矩阵-第1组-王浩恩.xlsx`，检查：

- `需求进度管理表` 范围为 `A1:J28`；
- `示例` 范围为 `A1:L31`；
- 需求编号为 1–25；
- Web 为 22–23，ML 为 24–25；
- 没有公式错误；
- 渲染图片可读且版式完整。

本步骤只读，不运行 artifact edit operation marker，不导出覆盖工作簿。

- [ ] **Step 4：验证提交边界**

```bash
git status --short
git diff --check HEAD^ HEAD
```

预期：合并完成，工作区干净。

## Task 2：重写当前治理入口并新增范围/进展证据

**文件：**

- 修改：`README.md`
- 修改：`docs/management/scope-baseline.md`
- 修改：`docs/management/project-plan.md`
- 修改：`docs/management/risk-register.md`
- 修改：`docs/management/daily-log.md`
- 新增：`docs/management/scope-change-2026-09-04.md`
- 新增：`docs/review/repository-progress-audit-2026-09-04.md`
- 新增：`docs/release/core-acceptance-checklist.md`

- [ ] **Step 1：更新仓库 README 当前入口**

README 必须按以下顺序呈现：

1. 当前交付口径及 2026-09-04 生效日期；
2. 三条核心交付线；
3. Web/ML 可选成果保留说明；
4. 当前架构、范围基线、进展审计、接口合同、实施计划和旧五系统 HTML 链接；
5. 五人正式角色与新职责；
6. 分支路径 `feat/* -> dev -> main`；
7. 用户端运行入口。

不得再把“交互式五系统架构图”称为当前唯一架构，也不得声称五端必须同时通过验收。

- [ ] **Step 2：将范围基线升级为 v2**

`scope-baseline.md` 必须包含：

- 三个核心系统的必须完成与明确不做；
- 腾讯地图 Web API 驾车/步行导航仍为用户端核心要求；
- Web、ML 的状态为可选参考成果；
- 核心 Definition of Done；
- 2026-09-01 v1 和 2026-09-04 v2 的变更记录；
- 预测缺失时的合法降级语义。

- [ ] **Step 3：更新 WBS、风险和历史日志**

- `project-plan.md`：主路径聚焦服务端 P0、端到端闭环、两次彩排；Web/ML 工作包标 optional，不进入硬闸门。
- `risk-register.md`：将服务端协议/状态机/测试缺失列为最高风险；R5/R6 降为可选展示风险；保留腾讯地图 P0。
- `daily-log.md`：不改写 9 月 1 日历史，在末尾追加 2026-09-04 审计与范围决策。

- [ ] **Step 4：新增正式范围变更记录**

`scope-change-2026-09-04.md` 记录：

- 变更原因；
- 变更前后对照；
- 保留与取消的精确边界；
- 对角色、接口、数据库、答辩和分支的影响；
- 决策人和可回退方式。

- [ ] **Step 5：新增仓库进展审计报告**

报告以 2026-09-04 已验证证据为准，至少包含：

- `dev@04e45fc`、`main@fc7c67e` 和五个功能分支的拓扑；
- PR #4、#5 状态；
- #2 服务端只实现 5/27 action、根 CMake 未纳入 target、无专属测试、暂不可合入；
- #4 数据库 9/9、核心 CTest 13/13、黄金库完整性与两个模拟器风险；
- #1 Web 36/36、静态 HTTP 200、未真实快照联调；
- #5 ML 78 passed/3 skipped、12,960→144 离线管线、未真实服务端发布；
- 当前 P0/P1 列表与建议合并顺序；
- “提交/构建/测试/独立运行/真实联调”五层状态定义。

- [ ] **Step 6：新增核心验收清单**

清单只要求：

- 环境与构建；
- 黄金库校验；
- 用户登录、查站/查桩；
- 腾讯地址解析和 QWebEngineView 路线；
- 预约→充电→遥测→停止→结算；
- 管理端统计、故障重启和用户管理；
- 模拟器状态、遥测、故障；
- 数据一致性、断线降级和两次同提交彩排。

Web/ML 只列在“可选加分演示”，不影响 GO/NO-GO。

- [ ] **Step 7：检查并提交**

```bash
rg -n '五个系统全部|五系统连续|五系统 V1|五端按冻结接口并行实施' README.md docs/management docs/release
git diff --check
git add README.md docs/management docs/review docs/release
git commit -m "docs(scope): rebaseline governance around core delivery"
```

预期：关键词仅允许出现在明确标注的历史引用或变更前口径中。

## Task 3：调整合同、计划索引、发布计划和数据说明

**文件：**

- 修改：`docs/design/interface-contract.md`
- 修改：`docs/plans/2026-09-01-ev-charging-platform-design.md`
- 修改：`docs/plans/2026-09-03-optional-features-proposal.md`
- 修改：`docs/superpowers/plans/README.md`
- 修改：`docs/superpowers/plans/2026-09-01-web-dashboard.md`
- 修改：`docs/superpowers/plans/2026-09-01-ml-forecasting.md`
- 修改：`docs/superpowers/plans/2026-09-01-integration-demo-release.md`
- 新增：`docs/superpowers/plans/2026-09-04-core-integration-demo-release.md`
- 修改：`database/README.md`
- 修改：`docs/management/environment-matrix.md`

- [ ] **Step 1：保留冻结协议并增加 profile 说明**

`interface-contract.md` 不删除或重命名任何 v1 action、字段、状态或错误码。新增“2026-09-04 运行 profile”说明：

- core 不要求在线 ML 生产者或 Web 消费者；
- `forecast.publish` 为 optional producer extension；
- `forecast.latest`、ForecastRun/Record 和 snapshot contract 为兼容能力；
- 无 active forecast 合法；
- Web snapshot 不进入 core release gate；
- 若启用 optional 功能，原合同仍完整适用。

- [ ] **Step 2：将旧设计与旧提案标为历史**

- `2026-09-01-ev-charging-platform-design.md`：文件头增加“历史 v1 基线，五系统硬门槛已被 2026-09-04 设计替代”，正文保持历史事实。
- `2026-09-03-optional-features-proposal.md`：状态改为“已终止/被范围重置替代”，保留 F1–F8 原文作为决策证据。
- Web/ML 实施计划：文件头增加“可选参考计划，不纳入 core acceptance/release”，不删除 checkbox 或测试记录。

- [ ] **Step 3：重写计划索引**

计划索引分为：

- 当前核心执行计划；
- 可选参考计划；
- 历史计划。

新的依赖图只以 Foundation、Data/Simulator、Admin/Server、User、Core Integration 为主线；Web/ML 从旁路 optional 接入，不指向默认 Release Gate。

- [ ] **Step 4：保留旧集成计划并新增当前核心计划**

- 旧 `2026-09-01-integration-demo-release.md` 加历史状态横幅，不再作为默认执行入口。
- 新 `2026-09-04-core-integration-demo-release.md` 提供核心 reset/start/smoke/rehearsal/release 目标、进程顺序、检查项和显式可选附录。
- 不在本 PR 实现尚不存在的 ops 脚本；计划中须如实标记为待实现。

- [ ] **Step 5：拆分数据库 profile 和环境说明**

- `database/README.md`：区分基础黄金库（core）与带预测增强黄金库（optional）；现有已封存 `demo.db` 继续保留。
- `environment-matrix.md`：Qt、Python/pytest 属 core；Node/npm 和 ML 科学计算包标 optional profile；不篡改已观察到的安装事实。

- [ ] **Step 6：检查并提交**

```bash
rg -n '当前.*五系统|默认.*Web|默认.*ML|全部子系统.*Release' docs/design docs/plans docs/superpowers/plans database/README.md docs/management/environment-matrix.md
git diff --check
git add docs/design/interface-contract.md docs/plans docs/superpowers/plans database/README.md docs/management/environment-matrix.md
git commit -m "docs(plans): separate core and optional delivery profiles"
```

## Task 4：保留旧 HTML 并新增当前核心架构页面

**文件：**

- 修改：`docs/design/five-system-architecture.html`
- 新增：`docs/design/core-system-architecture.html`

- [ ] **Step 1：检查旧 HTML 结构和本地资源**

确认旧页面是否依赖外部 CDN、内联脚本或本地文件；记录标题、主图和交互入口。不得删除原有五系统节点、说明或交互。

- [ ] **Step 2：增加历史状态横幅**

旧页面顶部必须醒目标注：

- “2026-09-01 历史五系统基线”；
- “Web/ML 已于 2026-09-04 退出核心验收但成果保留”；
- 当前架构页面相对链接。

- [ ] **Step 3：创建当前核心架构页面**

新页面至少展示：

- Qt 用户端与腾讯地图；
- Qt 管理/服务端及唯一写库权威；
- SQLite/黄金库；
- Qt 模拟器；
- 核心主流程；
- Web/ML 作为不阻塞发布的可选侧车；
- 当前 P0 服务端提示；
- 当前范围、进展审计、接口合同、旧架构页面的相对链接。

页面必须在常见 1366×768 和移动宽度下可读，使用仓库内自包含 HTML/CSS/JS，不引入新的网络依赖。

- [ ] **Step 4：本地浏览器验证**

```bash
python3 -m http.server 8184 --bind 127.0.0.1 --directory .
```

使用应用内浏览器检查：

- 两个页面 HTTP 200；
- 默认首屏没有水平溢出或正文遮挡；
- 历史/当前状态醒目；
- 相互链接、当前文档链接可用；
- 页面中不存在真实腾讯 Key。

- [ ] **Step 5：检查并提交**

```bash
rg -n 'TENCENT_MAP_KEY|历史五系统基线|核心交付架构' docs/design/*.html
git diff --check
git add docs/design/five-system-architecture.html docs/design/core-system-architecture.html
git commit -m "docs(architecture): publish core profile and retain legacy view"
```

## Task 5：用测试拆分核心与可选环境检查

**文件：**

- 修改：`scripts/check_env.sh`
- 修改：`scripts/bootstrap.sh`
- 新增：`tests/scripts/test_check_env.sh`

**接口：**

```text
scripts/check_env.sh
scripts/check_env.sh --with-web
scripts/check_env.sh --with-ml
scripts/check_env.sh --with-web --with-ml
scripts/check_env.sh --help
```

默认模式只检查 core；未知参数输出用法并返回 2。所有模式保持 POSIX sh 兼容。

- [ ] **Step 1：先写失败的 shell 测试**

测试必须覆盖：

- `sh -n` 语法；
- `--help` 返回 0 并列出两个 optional flag；
- 未知参数返回 2；
- 默认真实环境检查通过；
- `--with-web` 通过；
- `--with-ml` 通过；
- 两参数组合通过；
- 用 `sh -x` 或等价执行证据证明默认路径未执行 node/npm 和 ML import；
- Web 路径执行 node/npm 检查；
- ML 路径执行 NumPy/pandas/sklearn/joblib import。

运行：

```bash
sh tests/scripts/test_check_env.sh
```

预期：至少一项失败，因为当前脚本无参数并始终检查 Web/ML。

- [ ] **Step 2：实现最小 profile 参数解析**

- core command：`git cmake make g++ qmake6 qtpaths6 python3 pkg-config`；
- core Qt：`Qt6Core Qt6Network Qt6Widgets Qt6WebEngineWidgets Qt6Charts Qt6Test`；
- core Python：`pytest`；
- Web optional：`node npm`；
- ML optional：`numpy pandas sklearn joblib`；
- 保留 `PATH=/usr/bin:/bin` 和清除 Python 虚拟环境变量的宿主工具链语义。

- [ ] **Step 3：重构 bootstrap 分组说明**

`bootstrap.sh` 的实际全量安装行为保持不变，但包列表清楚分成：

- core；
- optional Web；
- optional ML。

脚本输出明确说明“安装的是方便开发的全量超集”；不得声称 Node 或 sklearn 属核心验收前置条件。

- [ ] **Step 4：运行测试并提交**

```bash
sh tests/scripts/test_check_env.sh
sh -n scripts/check_env.sh
sh -n scripts/bootstrap.sh
scripts/check_env.sh
scripts/check_env.sh --with-web --with-ml
git diff --check
git add scripts/check_env.sh scripts/bootstrap.sh tests/scripts/test_check_env.sh
git commit -m "build(env): separate core and optional checks"
```

## Task 6：更新最新版需求矩阵并完成视觉验证

**文件：**

- 修改：`需求矩阵-第1组-王浩恩.xlsx`

**工具约束：** 必须使用 `@oai/artifact-tool`；不得用 openpyxl、LibreOffice 自动另存或手工 ZIP/XML 修改工作簿。

- [ ] **Step 1：在首次编辑命令前登记 artifact edit operation**

本任务只执行一次以下命令：

```bash
/home/hushengyuan/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node \
  /home/hushengyuan/.cache/codex-runtimes/codex-primary-runtime/plugins/openai-primary-runtime/plugins/spreadsheets/skills/spreadsheets/container_tools/mark_artifact_operation_started.mjs \
  --operation-kind edit --expected-output-count 1 --output-format xlsx
```

- [ ] **Step 2：先写转换断言并读取原始单元格**

编辑脚本在保存前必须断言输入为最新矩阵：

- 文件名为 `需求矩阵-第1组-王浩恩.xlsx`；
- `需求进度管理表!A1:J28`；
- NO. 1–25；
- Web 行为 NO.22–23；
- ML 行为 NO.24–25；
- `示例!A1:L31` 存在。

- [ ] **Step 3：实施精确单元格变更**

保持行数、工作表、合并单元格和版式，至少完成：

- 标题图例增加 `N/A：退出核心验收、成果保留`；
- 用户端已完成客户端实现但待真实服务端联调的行标为 `△`，地图地址解析/导航可按已有验证证据标为 `✓`；
- 服务端行统一反映“已有雏形、核心动作和真实联调未完成”，不得标 `✓`；
- 数据库设计行保留 `✓`；
- 通信/数据构建与模拟器根据尚待服务端联调及已知风险标 `△`；
- NO.22–25 状态改为 `N/A`，详细说明前缀为 `【可选成果】`；
- NO.22–23 困难/备注记录 `feat/web@1d07976`、36 项测试和未完成真实快照联调；
- NO.24–25 困难/备注记录 `feat/ml@2b1563d`、78 passed/3 skipped 和未完成真实服务端发布；
- 删除核心服务端/数据库行中“必须与 Web/ML 同源或必须回灌预测”的硬依赖措辞，同时保留 optional compatibility 说明；
- `示例` 工作表保持逐单元格不变。

- [ ] **Step 4：导出到新文件并原子替换仓库工作簿**

先导出到该任务的临时输出目录，重新导入验证后再替换工作树中的目标文件。不得直接覆盖输入流，也不得创建第二个最终工作簿。

- [ ] **Step 5：渲染与错误扫描**

必须验证：

- 两张工作表仍存在；
- `需求进度管理表` 仍为 `A1:J28`；
- `示例` 内容未改变；
- 公式错误扫描为 0；
- NO.22–25 均为 `N/A`；
- 关键核心行不再将 Web/ML 写作验收前置；
- 渲染图片中文字、状态列和备注列可读，没有截断、遮挡或样式破坏。

- [ ] **Step 6：提交**

```bash
git diff --check
git status --short
git add -- '需求矩阵-第1组-王浩恩.xlsx'
git commit -m "docs(requirements): align matrix with core acceptance"
```

## Task 7：全范围一致性验证和交付记录

**文件：**

- 新增：`docs/test/core-scope-rebaseline-verification.md`
- 可能修改：本计划范围内存在事实或链接错误的文件

- [ ] **Step 1：范围和敏感信息扫描**

```bash
rg -n '五个系统全部可运行|五系统连续两次|五系统 V1|默认.*Web|默认.*ML' README.md docs database/README.md scripts
git grep -n -E '[A-Z0-9]{5}(-[A-Z0-9]{5}){5}' -- ':!references/**'
```

每个旧口径命中必须属于明确的历史引用、变更前说明或已标记 historical 文件；真实腾讯 Key 命中必须为 0。

- [ ] **Step 2：链接和文件边界检查**

编写或使用一次性只读检查，验证仓库内 Markdown 相对链接和新旧 HTML 相互链接存在。确认：

```bash
git diff --name-only origin/dev...HEAD -- references apps shared simulator/src database/schema.sql 'database/*.py'
```

预期：无输出。

- [ ] **Step 3：运行文档、脚本和数据验证**

```bash
sh tests/scripts/test_check_env.sh
python3 -m pytest database/tests -v
node --test dashboard/tests/*.test.mjs apps/user-client/tests/test_navigation_html.mjs
```

- [ ] **Step 4：在原生文件系统 fresh build 与 CTest**

```bash
build_dir="$(mktemp -d /tmp/ev-core-scope-build-XXXXXX)"
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure
```

预期：完整构建成功，13/13 CTest 通过。构建目录不进入仓库。

- [ ] **Step 5：记录可重复验证证据**

`core-scope-rebaseline-verification.md` 记录精确提交、时间、命令、通过数、已知不在本 PR 修复的问题：

- 服务端 P0；
- 模拟器两个 P1；
- 用户端已登记但未在本 PR 修改的会话 guard 风险；
- Web/ML 尚未真实联调且不阻塞 core。

- [ ] **Step 6：最终差异检查并提交**

```bash
git diff --check
git status --short
git add docs/test/core-scope-rebaseline-verification.md
git commit -m "test(scope): record rebaseline verification evidence"
```

完成后进入全分支审查，基线为 `04e45fcb4eb8ab5f14f526588e01d28fb4ffffd5`。审查通过后推送 `docs/core-scope-rebaseline` 并创建目标为 `dev` 的 PR。
