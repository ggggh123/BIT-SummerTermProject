# #1 变更声明（写给其他 AI / Agent 阅读）

> 作者：#1 王浩恩（前端大屏 / PM）｜日期：2026-09-15
> 分支：`feat/part2-web-layout`（主页版面重构）；此前 `feat/part2-web-closeout`（PR #15，已合入 `dev`，`fdb5228`）、`feat/part2-integration`（PR #14，已合入 `dev`，`befbdf9`）
> 读者：在本仓库继续改代码或文档的其他 AI。**动手前先读第 4 节「硬契约」**，那里列了改坏就会挂测试/挂演示的东西。
> 这份声明只描述 **#1 写入范围内**的改动；队友目录的变化不在本文，除注明外我没有修改过他们的文件。

## 1. 我的写入范围

`web/**`、`docs/**`、`scripts/part2/**`（后者的 10/11/40/env-check 是更早一轮的产物）。

| 目录 | 归属 | 说明 |
|---|---|---|
| `web/**` | **#1** | Vue3 前端大屏、E2E、前端文档 |
| `docs/**` | **#1** | 契约、审查、抽验表、分工、证据 |
| `scripts/part2/**` | **#1** | 环境脚本与可移植一键启停（`part2/scripts/**` 是 #2 的，注意区分两个字面量相似的路径） |
| `server/**`、`part2/scml/**` | #4 | 我没有修改 |
| `part2/ml/**`、`part2/sql/**`、`part2/tmp/**` | #5 | 我没有修改 |
| `part2/plans/**`、`part2/scripts/**` | #2 | 我没有修改（只做过分支合并） |

我合并过队友分支（`feat/part2-02`、`feat/part2_SCML`、`feat/part2-ml`）到集成分支，**未改动其文件内容**。

## 2. 代码改动（`web/`）

| 文件 | 状态 | 改了什么 | 谁依赖它 |
|---|---|---|---|
| `web/src/components/ScaleFrame.vue` | 改 | ① 宿主高度由 `baseHeight × scale` 改为 **`contentHeight × scale`**（`contentHeight = max(1080, 内容 scrollHeight)`）+ `ResizeObserver`；② 预留高度由固定 120px 改为**按真实占位**（宿主顶部 + `.footnote` 高度 + 12），否则整页会比视口高 ~55px | 主页/子页布局；E2E 的面板可见性与"一屏放下"断言 |
| `web/src/styles/theme.css` | 改 | `.scale-host` 的 `overflow` **hidden → visible**；新增 `.panel--dv`、`.home-deco`、`.panel-heading`、`.panel-heading-extra`、`.event-board`（220px）；新增主页三列栅格 `.grid.home-main`、`.grid.home-bottom`、`.dv-col`、`.quality-list` 与固定图表高度 `.chart.h150/.h220/.h380` | DataV 边框、滚动榜单、面板标题、主页版面 |
| `web/src/components/EChart.vue` | 改 | onMounted 增加 **`el.value.__echarts = chart`**（页面里没有全局 echarts），onBeforeUnmount `delete el.value?.__echarts` | **E2E 取实例用**（`convertToPixel` / `trigger('click')`） |
| `web/src/lib/charts/beijingStation.js` | 改 | `geo` 增加 `layoutCenter:['50%','52%']`、`layoutSize:'94%'`、`scaleLimit`；`symbolSize` 由 `clamp(π/1.2, 12, 34)` 改为 **`clamp(chargerCount/2.2, 10, 16)`** | 主页地图；`web/tests/mapScale.test.mjs` |
| `web/src/views/HomeView.vue` | 改 | ① 面板内容包进 `<DvFrame>`、加 `<Decoration10 class="home-deco">`、事件流换 **`ScrollBoard`**、标题改 `.panel-heading`；② **版面重排为三列大屏栅格**（左：营收趋势+桩状态｜中：北京地图 `h380`｜右：利用率排行+数据质量；底行：24h 负荷 `h220` + 事件流），使内容 ≤1080 设计 px | 主页 E2E、截图、抽验 |
| `web/src/components/DvFrame.vue` | **新增** | DataV 边框封装：`BorderBox8` + 可选 `title` + `Decoration10`；props `{ title?: string }`，内容走默认 slot | `HomeView.vue` |
| `web/src/main.js` | 改 | `import '@kjgl77/datav-vue3/dist/style.css'` | 全部页面（DataV 样式） |
| `web/package.json` | 改 | deps 增 `@kjgl77/datav-vue3@^1.7.4`；devDeps 增 `@playwright/test@^1.63.0`；增 `engines.node >= 23`；增 script `test:e2e` | 构建与测试 |
| `web/playwright.config.mjs` | **新增** | `testDir: ./e2e`；`baseURL = PART2_BASE_URL ?? http://192.168.88.131:5000`；`channel = PART2_CHANNEL ?? 'chrome'`（用系统 Chrome，不下载浏览器）；viewport 1920×1080；`workers: 1` | E2E |
| `web/e2e/dashboard.spec.mjs` | **新增** | **6 条**页面级用例（见 §3） | CI/本地验收 |
| `web/README.md` | 改 | 重写测试段（36 项单测命令）、新增 Playwright E2E 段、新增 DataV 段、待办更新 | 人/AI 读文档 |

**不属于本次改动、但同属 `web/`（早前一轮已完成，改动前请先读）**：`src/App.vue`（顶栏与 `.pill-error` 状态）、`src/api/{client,state,polling,endpoints}.js`（`USE_MOCK`/`VITE_API_BASE` 开关、轮询、四态）、`src/views/{UserView,StationView,EnterpriseView,GovView}.vue`、`src/lib/charts/*`、`src/mock/*`、`public/geo/beijing.json`、`scripts/gen-mock.mjs`、`scripts/use-ads-json.mjs`。

## 3. 测试与验证方式

```bash
# 纯函数单测（36 项）：视图模型 / 图表 option / 轮询调度 / 地图与缩放
node --test web/tests/*.test.mjs
# 第一阶段回归（36 项）：不应被前端改动影响
node --test dashboard/tests/*.test.mjs
# 页面级 E2E（6 项）：默认打 VM 演示栈，也可本地
npm --prefix web run test:e2e
PART2_BASE_URL=http://localhost:5000 npm --prefix web run test:e2e
# 后端三条自检（联调用）
python server/tools/selftest_contract.py && python server/tools/selftest_frontend_shape.py && python server/tools/selftest_static_dist.py
```

E2E 6 条与它们证明的事：

| 用例 | 断言 | 依赖的契约 |
|---|---|---|
| 主页渲染 | `.kpi-card` 4 个、`.chart canvas` 5 个、KPI 文本非 `—`、控制台零报错 | `.kpi-card`/`.kpi-value`/`.chart` |
| 地图点击跳转 | 触发 ECharts click → URL 变 `/#/station?station=<id>` → 充电站页 `select` 值 = 该 id、详情行含该站名 | `EChart.__echarts`、路由 query、`select` |
| 5s 轮询重绘 | 拦截 `/api/overview/kpis` 第二次返回 999999 → 页面数字随之变化 | 5s 轮询 + 响应式视图模型 |
| 接口失败保留旧数据 | 拦截 `/api/**` 全部失败 → `.pill-error` 可见、页内提示出现、KPI 文本不变、图表数量不变 | `state.js` 语义 |
| DataV 渲染 | `.dv-border-box-8` ≥5 且尺寸 >200×100；`.dv-scroll-board` 存在且高度 >100px | `DvFrame`/`ScrollBoard`/`.event-board` 高度 |
| 主页版面 | `.scale-inner` 内容高 **≤1080 设计 px**；地图图表高 **≥380**；整页 `scrollHeight ≤ 视口+32` | 三列栅格、`.chart.h150/.h220/.h380`、`ScaleFrame` 预留高度 |

当前结果：单测 36/36、回归 36/36、E2E **6/6**、后端 55/55 + 46 项 0 不兼容 + 静态托管 OK。

## 4. 硬契约（改代码前必须知道；破坏会导致测试/抽验/演示失败）

1. **DOM 与类名**：`.kpi-card`（4 个）、`.kpi-value`（文本即抽验值）、`.panel`（E2E 用 `hasText` 定位，如 `北京市站点分布`）、充电站页的 `select`（value = stationId）、`.dv-border-box-8`、`.dv-scroll-board`（父容器 `.event-board` 必须给高度）。改模板请保留这些钩子，或同步改 `web/e2e/dashboard.spec.mjs`。
2. **`EChart.vue` 的 `__echarts` 钩子**：E2E 依赖。若要移除，请先改测试。
3. **`ScaleFrame` 的"内容驱动高度"**：这是修复主页静默裁切的关键（旧实现 `1080×scale` + `overflow:hidden` 会裁掉约 491px，含数据质量面板）。**不要改回固定高度**。
4. **路由**：主页地图点击 `router.push({ path: '/station', query: { station: <id> } })`；`StationView` 读 `route.query.station` 并校验存在性（`src/views/StationView.vue:26-28`）。hash 路由（`createWebHashHistory`）。
5. **接口**：真源 `docs/design/part2-api-contract.md`（27 条路由，信封 `{code,message,data,generatedAt}`）。前端**只读**，不做业务写入；接口失败必须保留旧数据 + 显示过期提示，禁止空白。
6. **数据口径**：金额整数分（前端 ÷100）、时间 `+08:00` ISO 8601、百分比 0–100、`stationId` 需数字转型（后端可能返回字符串）。
7. **图表 option 契约**：`buildBeijingStationOption(view)` 返回 `{tooltip, geo, series, meta}`，每个点位含 `stationId/name/value=[lng,lat]/chargerCount/idleCount/utilizationRate/revenueFen`（`web/tests/mapScale.test.mjs` 断言）。
8. **DataV 用法**：组件按需从 `@kjgl77/datav-vue3` 具名导入（`BorderBox8`/`Decoration10`/`ScrollBoard`…）；样式由 `main.js` 引入一次。新增 DataV 组件后请确认 `npm run build` 通过，并在 E2E 里补一条"确实渲染且有尺寸"的断言（DataV 的边框/榜单在无高度容器里会静默 0 高）。

## 5. 文档与脚本改动

| 文件 | 状态 | 内容 / 用途 |
|---|---|---|
| `docs/management/part2-division-revision.md` | 新增 | 第二阶段「编号↔姓名↔模块↔分支」对照表（#2/#4 编号互换的坑），`Part2/00` §8.1 的姓名列落地件 |
| `docs/management/part2-metric-checklist.md` | 改 | 数值抽验表：数据源标注 `runId=ads-20260915101224`（正式规模），15 行中 9 行已与页面逐位一致、6 行待看截图；3 条如实说明（电量舍入差、在线率偏理想、R02/R04 检出 0） |
| `docs/management/part2-analysis-dimensions.md` | 新增 | 对老师第 1/2/3/4 条的应答：**15 个分析维度 + 5 组维度对比**（表→接口→页面），附 HDFS 分层现状与版本基线 |
| `docs/test/evidence/part2-2026-09-14/windows-local-repro.md` | 新增 | 无 VM 全链路复现（生成器 18s → `build_local.py` 17s → 三条自检 + 五页真接口渲染） |
| `docs/test/evidence/part2-2026-09-15/vm-integration-run.md` | 新增 | VM 上实测：`part2/scripts/start_part2.sh` 的三处跨机断点（sudo -u hadoop / 缺 curl / 系统 python3 无 flask）＋等价可移植启动方式＋给 #2 的补丁建议 |
| `docs/test/evidence/part2-2026-09-15/regression-after-merge.md` | 新增 | 合并 #4 `13b18f0` + #5 `53b2dd4` 后的 9 项回归结果；含 `test_delivery_check.py` 的 Windows 编码缺陷定位 |
| `docs/test/evidence/part2-2026-09-15/README.md` | 新增/改 | 抽验真值表、五页截图索引、§4 桩状态修复复测、§5 两个版面缺陷（裁切/地图）、§6 对老师 6 条要求的核对与补齐 |
| `docs/test/evidence/part2-2026-09-15/screenshots/*.png` | 新增/改 | 五页 1920×1080 截图（DataV 接入后已刷新），答辩材料 |
| `scripts/part2/30-start-demo.sh` | 新增 | **可移植一键起全栈**：自动定位仓库根、自动挑能 `import flask` 的 Python（本机命中 `~/venvs/part2/bin/python` 3.11）、健康检查 curl→wget→urllib 回退、Hadoop 未运行才启动 |
| `scripts/part2/31-stop-demo.sh` | 新增 | 一键停（默认只停 Flask；`--all` 反向停 YARN→HDFS） |
| `scripts/part2/README.md` | 改 | 登记 30/31 并说明与 `part2/scripts/start_part2.sh`（#2 版）的分工与差异 |

## 6. 已知限制（不要误当 bug 修）

1. **主页版面已重构**（`feat/part2-web-layout`）：内容压到 1080 设计高度以内、1920×1080 一屏放下，地图升为 380px 主视区（E2E 第 6 条守护）。**往主页加内容前先看这条断言**。
2. **DataV 只在主页**：4 个视角子页仍是普通面板（可后续用 `DvFrame` 统一）。
3. **预测仍是基线**：`ads_forecast_batch.is_baseline=1`（seasonal-naive），等 #5 的 `handoff/forecast` 批次数据用 `--forecast-handoff` 接入。
4. **HDFS 上 `/dwd`、`/ads` 仍是旧数据**（`/ods` 104.7MB、360 分区 与 `/dws` 3.9MB 已是正式规模）：等 #3 的 `handoff/dwd`，再跑 `run_dws_ads.sh` 出官方 SparkSQL on YARN 链路与 YARN 记录。
5. **KPI 在线率 99.3%** 偏理想：故障桩只有 2 个（状态分布已按利用率生成，非缺陷）。
6. **电量 0.4 kWh 差异**：`ads_daily` 逐日四舍五入后汇总所致，金额为整数分无差异。
7. **ADS 质量表 R02/R04 检出 0**：注入各 11,200 条、检出 0，属 #3 的检测口径问题。
8. **老师"Qt 端加大屏入口"这条要求不存在**：2026-09-15 澄清为口误，主机浏览器直接开大屏即可；`docs/management/part2-design-review.md` 的 R3 已作废，**不要再往 Qt 端加入口**。

## 7. 环境与依赖影响（会影响其他人的构建）

- 前端构建需要 **Node ≥ 23**（实测 v24.15.0）；`web/package.json` 已声明 `engines`。
- **`node_modules` 变大**（新增 DataV 与 Playwright）：给离线机器准备环境时必须**重新整体打包** `node_modules`，不能沿用旧包。
- Playwright 是 **devDependency**，`npm run build` 不依赖它；E2E 默认用**系统 Chrome**（`channel: 'chrome'`），不下载浏览器。
- `dist` 已把 DataV 打进 bundle → **演示机仍然只需要 Python + Flask**，不需要 Node。
- 与 #2 的启停脚本差异见 `scripts/part2/README.md`（他的写死 `/home/bit/part2` + `sudo -u hadoop` + `curl` + 系统 `python3`，在本机三处断点）。

## 8. 落点（提交级）

| 提交 | 内容 | 文件 |
|---|---|---|
| `c1af94c` | 分工编号↔姓名对照 | `docs/management/part2-division-revision.md` |
| `41621d2` | 无 VM 全链路复现证据 | `docs/test/evidence/part2-2026-09-14/windows-local-repro.md` |
| `53d4631` | VM 全栈实测 + 脚本跨机缺陷 | `docs/test/evidence/part2-2026-09-15/vm-integration-run.md` |
| `eced236` | 跨机截图 + 抽验真值 | 同上 `README.md` + `screenshots/*` |
| `8303752` | 可移植一键启停 | `scripts/part2/{30-start-demo.sh,31-stop-demo.sh,README.md}` |
| `77e1959` | 合并后回归记录 | `docs/test/evidence/part2-2026-09-15/regression-after-merge.md` |
| `6337671` | 桩状态修复复测 + 截图刷新 | 证据 `README.md`、`screenshots/*` |
| `029ce66` | 修主页裁切 + 地图点大小/布局 | `web/src/components/ScaleFrame.vue`、`web/src/lib/charts/beijingStation.js`、`web/src/styles/theme.css` |
| `ec8b6d6` | 页面级 E2E 4 条 + 钩子 | `web/e2e/dashboard.spec.mjs`、`web/playwright.config.mjs`、`web/package.json`、`web/src/components/EChart.vue` |
| `42bb820` | DataV 接入 | `web/src/components/DvFrame.vue`、`web/src/views/HomeView.vue`、`web/src/main.js`、`web/src/styles/theme.css`、`web/package.json` |
| `1b5e849` | 抽验表定稿 + 版面缺陷记录 | `docs/management/part2-metric-checklist.md`、证据 `README.md` |
| `2c4590f` | 维度对照表 + 截图刷新 + `engines` | `docs/management/part2-analysis-dimensions.md`、`web/README.md`、`web/package.json`、`screenshots/01-home.png` |

## 9. 其他 AI 接手时的建议顺序

1. 先跑 `node --test web/tests/*.test.mjs` 与 `npm --prefix web run test:e2e`，确认基线是绿的。
2. 改 `web/src/**` 前过一遍 §4 的硬契约；改完必须重跑单测 + E2E，并（若动了布局）刷新 `docs/test/evidence/part2-2026-09-15/screenshots/`。
3. 改 `server/**`、`part2/**` 属队友范围：接口字段以 `docs/design/part2-api-contract.md` 为真源，**字段变更先改契约再改实现**，前端会自动跟着契约走。
4. 数据相关结论（真值、规模、口径）以 `docs/test/evidence/part2-2026-09-15/README.md` 与 `docs/management/part2-analysis-dimensions.md` 为准，不要凭旧文档或聊天记录里的数字。
