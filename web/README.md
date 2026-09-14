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
| `/user` | 用户视角 | 骨架 + 待接接口 |
| `/station` | 充电站视角 | 骨架 + 待接接口 |
| `/enterprise` | 企业视角 | 骨架 + 待接接口 |

按老师口述的「3 个界面来回切换」实现；文档里的政府视角列为加分项，暂未建页。

## 待办

1. 北京 GeoJSON 底图（本地文件，不走 CDN）——主页站点图现在是经纬度散点。
2. 与 #2 冻结 `src/api/endpoints.js` 的字段清单。
3. 三个视角页的图表实现（复用 `models.js` 的构造风格，新增 builder 先写测试）。
