# 第二阶段 Web 大屏（Vue 3 + ECharts + Flask API）

老师第二阶段要求：数据可视化：Flask + Vue + ECharts。本工程是其中的前端部分。

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
# 前端 28 项（复用边界 5 + 企业 6 + 用户/充电站 8 + 政府 5 + 轮询 4）
node --test web/tests/viewModel.test.mjs web/tests/enterpriseModels.test.mjs web/tests/userStationModels.test.mjs web/tests/govModels.test.mjs web/tests/polling.test.mjs
# 第一阶段回归 36 项
node --test dashboard/tests/contracts.test.mjs dashboard/tests/models.test.mjs dashboard/tests/poller.test.mjs
```

五页端到端渲染验收（Chrome headless）：主页 5 图 / 用户 4 图 / 充电站 4 图 / 企业 4 图 / 政府 3 图，控制台零报错。

## 待办

1. 北京 GeoJSON 底图（本地文件，不走 CDN）——主页站点图现在是经纬度散点。
2. 与 #2 冻结 `src/api/endpoints.js` 的字段清单。
3. 用户页「低拥堵推荐榜」等 #5 的 `ads_forecast_result` 就绪后接入（无结果显示「暂无预测」）。
4. Flask 静态托管联调（`dist` 交 Flask 托管，单进程起全栈）。
5. 主页 1920×1080 等比缩放（《01》§5.1 的 `transform: scale`；当前为响应式栅格）。
