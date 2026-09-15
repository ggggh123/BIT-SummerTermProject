# 第二阶段 Web 大屏（Vue 3 + ECharts + Flask API）

老师第二阶段要求：数据可视化：Flask + Vue + ECharts。本工程是其中的前端部分。

> **改这个工程前先读 [`docs/management/part2-web-change-declaration.md`](../docs/management/part2-web-change-declaration.md)**：#1 的变更声明，列了改过哪些组件、E2E/抽验依赖的 DOM 与路由硬契约、以及"不要动"的地方（例如 `EChart.vue` 暴露的 `__echarts` 钩子、`ScaleFrame` 的内容驱动高度）。

## 复用原则（不做重复工作）

第一阶段的 ECharts 逻辑**不重写、不复制**，通过 `src/lib/` 直接引用 `dashboard/assets/` 下的纯模块：

| 复用文件 | 内容 | 第一阶段测试 |
|---|---|---|
| `dashboard/assets/models.js` | 5 个图表 option 构造器（营收、负荷+预测、桩状态、利用率排行、站点散点） | `dashboard/tests/models.test.mjs` |
| `dashboard/assets/contracts.js` | 金额分→元、百分比、kWh 格式化 | `dashboard/tests/contracts.test.mjs` |
| `dashboard/assets/poller.js` | 轮询四态与 cached 降级（第一阶段页面使用，本工程暂未使用） | `dashboard/tests/poller.test.mjs` |

`src/lib/viewModel.js` 是关键一层：把 Flask REST 响应组装成 `models.js` 期望的字段名，因此 5 个图表构造器**一行都不用改**。

跑第一阶段测试（36 项）：

```bash
node --test dashboard/tests/contracts.test.mjs dashboard/tests/models.test.mjs dashboard/tests/poller.test.mjs
```

## 本地运行

```bash
cd web
npm install
npm run mock      # 确定性生成 mock 数据（可选，仓库已带生成结果）
npm run dev       # http://localhost:5173
npm run build     # 产物在 web/dist，可由 Flask 直接托管
```

## 数据源切换

默认走同构 mock（页面右上角显示「演示数据」）。Flask 接口就绪后：

```bash
VITE_USE_MOCK=false VITE_API_BASE=http://<flask-host>:5000/api npm run build
```

由 Flask 同源托管时，直接运行 `npm run build:live` 即可。`web/.env.production` 已把
发行构建锁定为 `VITE_USE_MOCK=false` 和相对地址 `/api`；开发模式仍默认使用 mock，
不会影响离线页面开发。

组件代码不需要任何改动。接口契约见 `src/api/endpoints.js`，与《02-TL-Hadoop平台与Flask后端设计》§3.3 一致；统一信封 `{code,message,data,generatedAt}`。

## 页面结构

| 路由 | 页面 | 状态 |
|---|---|---|
| `/` | 主页综合运营大屏（KPI + 5 图 + 数据质量面板 + 事件流） | 已用 mock 渲染 |
| `/user` | 用户视角（价格对比、距离-价格散点、空闲排行、时段热力图） | 已用 mock 渲染 |
| `/station` | 充电站视角（单站利用率、快慢充结构、设备健康、区域覆盖；站点可切换） | 已用 mock 渲染 |
| `/enterprise` | 企业视角（趋势、营收排行、RFM、月度汇总、用户增长） | 已用 mock 渲染 |
| `/gov` | 政府视角（行政区覆盖密度、区域服务指标表、碳减排、全城负荷、利用率公平性） | 已用 mock 渲染 |

按《01-PM-Web可视化大屏设计》§3 实现「1 主页 + 4 视角子页」。老师口述只提到 3 个界面，多做的政府视角成本很低，保留。

## 刷新与降级（《01》§5.2）

- 主页：KPI / 桩状态 / 事件流 / 站点 **5 秒**轮询，趋势 / 排行 / 负荷 / 预测 / 质量 **60 秒**轮询
- 其余页面：**60 秒**轮询（充电站页在切换站点时立即重新拉取）
- 接口失败时**保留上次成功数据**，顶部徽章切为「数据已过期 · 保留上次成功数据」；轮询不中断
- 调度器 `src/api/polling.js` 有 4 项单测（立即执行、按间隔重复、stop 生效、异常不中断）

## 测试

```bash
# 前端纯函数单测 36 项（视图模型/图表 option/轮询调度/地图与缩放）
node --test web/tests/*.test.mjs
# 第一阶段回归 36 项
node --test dashboard/tests/contracts.test.mjs dashboard/tests/models.test.mjs dashboard/tests/poller.test.mjs
```

### 页面级 E2E（Playwright，补纯函数测不到的三条）

`@playwright/test` 是 **devDependency，不参与 `npm run build`**；默认用系统已安装的 Chrome（`channel: 'chrome'`），不下载 Playwright 自带浏览器。

```bash
npm run test:e2e                                              # 默认打 http://192.168.88.131:5000（VM 演示栈需在跑）
PART2_BASE_URL=http://localhost:5000 npm run test:e2e         # 或指向本地 Flask / vite preview
```

覆盖 4 条（`web/e2e/dashboard.spec.mjs`）：主页 5 图渲染且控制台零报错；**点击地图站点 → 跳转 `/#/station?station=<id>` 且充电站视角选中同一站**；**5s 轮询真的重绘**（拦截接口改值后页面数值随之变化）；**接口失败保留旧数据**并显示「数据已过期 / 数据加载失败」，不白屏。

> 地图点击在测试里通过 ECharts 实例的事件通道触发（`inst.trigger('click', …)`），链路与真实点击一致（`chart.on('click')` → `emit` → `router.push`）；原因是主页地图面板偏扁、站点像素互相贴近，像素级点击结果不稳定，见 `docs/test/evidence/part2-2026-09-15/README.md` §5。

### DataV 大屏件（老师要求第 5 条）

依赖 **`@kjgl77/datav-vue3@1.7.4`**（DataV 的 Vue3 版），样式随包引入（`main.js` 里 `import '@kjgl77/datav-vue3/dist/style.css'`）。当前落在**主页大屏**：

| 位置 | DataV 件 |
|---|---|
| 每个图表面板（营收趋势/桩状态/24h 负荷/利用率排行/北京地图） | `dv-border-box-8` 边框（封装在 `src/components/DvFrame.vue`） |
| 面板标题右侧 | `dv-decoration-10` 装饰条 |
| KPI 行下方 | `dv-decoration-10` 分隔条 |
| 实时事件流 | **`dv-scroll-board`** 滚动榜单（替代原朴素列表） |

E2E 里有一条专门断言它们真的渲染出来且有非零尺寸（`DataV 大屏件已渲染`）。ECharts 图表类型保持 5 类（bar/line/scatter/pie/heatmap）不变，DataV 负责大屏骨架与装饰。

> 离线打包提醒：`node_modules` 现在多了 DataV（含 `@kjgl77/datav-vue3` 与其依赖），给离线机器准备环境时需整体重新打包；`dist` 已把 DataV 打进 bundle，演示机不需要 Node。

## 待办

1. 当前正式 ADS 已合并 Spark MLlib 批次 `ml-20260915-111604`（`isBaseline=false`）；若后续重新构建 ADS 而未传入 forecast handoff，数仓脚本仍会按设计回退到 seasonal-naive，并在接口中如实标注，不能把回退批次当作当前正式演示结果。
2. DataV 目前只用在主页大屏；4 个视角子页仍是普通面板，如需统一观感可把 `DvFrame` 铺到各子页（改动小，但要重跑 E2E 与截图）。
3. 主页在 1920×1080 下已"一屏放下"（三列栅格 + 地图 380px 主视区）；若后续往主页加面板，先看 E2E 第 6 条的 `.scale-inner` 内容高 ≤1080 断言，避免又回到需要滚动。
