# 01 · Web 可视化大屏设计说明书（#1 PM）

> 承担人：#1（PM，项目经理）
> 任务性质：**第二阶段主要任务（必做）**
> 管理职责：范围、排期、风险、答辩组织
> 上游输入：ADS 层结果表（#4）、Flask REST API（#2）、预测接口（#5，选做）

---

## 1. 模块目标

基于 Flask + Vue + ECharts 技术栈，构建大数据可视化大屏 Web 应用，将 Spark 数仓 ADS 层的分析结果以「1 个综合主页 + 4 个视角子页」的形式呈现，覆盖**用户、充电站、企业、政府**四类使用者的决策需求，作为第二阶段的主要交付与答辩演示入口。

## 2. 技术栈与工程结构

| 项 | 选型 | 说明 |
|---|---|---|
| 前端框架 | Vue 3（Composition API）+ Vite | 单页应用，路由切换主页与子页 |
| 图表库 | ECharts 5 | 折线/柱状/饼环/散点/热力/地图散点 |
| HTTP | axios | 轮询 + 手动刷新 |
| 后端 | Flask 3.x（#2 负责，本设计只约定接口契约） | REST API，JSON |
| 运行环境 | Node 18+，Ubuntu 22.04 / 25.04 | 与团队基线一致 |

```text
web/
├── index.html
├── package.json
├── vite.config.js
└── src/
    ├── main.js
    ├── router/index.js          # 主页 + 4 子页路由
    ├── api/client.js            # axios 封装，统一 baseURL 与错误处理
    ├── components/              # KpiCard / TrendChart / RankBar / StatusDonut ...
    ├── views/
    │   ├── HomeView.vue         # 综合运营大屏（主页）
    │   ├── UserView.vue         # 用户视角
    │   ├── StationView.vue      # 充电站视角
    │   ├── EnterpriseView.vue   # 企业视角
    │   └── GovView.vue          # 政府视角
    ├── styles/                  # 暗色大屏主题、自适应缩放
    └── mock/                    # 与真实接口同构的 mock 数据（离线开发/演示）
```

## 3. 页面设计

### 3.1 主页 · 综合运营大屏（答辩主入口）

全屏暗色大屏，16:9 自适应缩放，顶部标题栏显示平台名、当前时间、数据生成时间与「项目生成的模拟数据」标注。

| 区域 | 图表 | 数据接口 |
|---|---|---|
| 顶部 KPI 条 | 总营收（元）、总充电量（kWh）、总订单数、桩在线率、当前空闲桩 | `GET /api/overview/kpis` |
| 中央 | **北京市**站点分布散点图（北京 GeoJSON 底图；经纬度，点大小=桩数，颜色=利用率），点击跳转充电站视角 | `GET /api/overview/stations` |
| 左中 | 近 30 日营收趋势折线 | `GET /api/enterprise/revenue-trend?days=30` |
| 左下 | 充电桩状态环图（idle/reserved/charging/fault/restarting） | `GET /api/overview/charger-status` |
| 右中 | 过去 24h 实际负荷曲线（选做：叠加未来 24h 预测曲线） | `GET /api/overview/load-24h`、`GET /api/forecast/24h` |
| 右下 | 站点利用率排行横向柱状图 | `GET /api/enterprise/station-ranking` |
| 底部 | 业务事件滚动流（订单完成、故障、恢复等） | `GET /api/overview/events` |
| 底部右侧 | **数据质量面板**：各表清洗前/后行数、10 类问题检出/注入对账、清洗保留率 | `GET /api/quality/summary` |

### 3.2 用户视角页 ——「去哪充电划算、不排队」

| 图表 | 说明 | 接口 |
|---|---|---|
| 站点价格对比柱状图 | 各站元/度对比，标注全市均价 | `GET /api/user/price-compare` |
| 距离-价格散点图 | 以北京市中心（天安门 39.9087, 116.3975）为参考点，找「近且便宜」的站 | `GET /api/user/price-distance` |
| 各站当前空闲桩排行 | 实时空闲数量与占比 | `GET /api/user/idle-ranking` |
| 低拥堵推荐榜（选做） | 结合 ML 未来 1h 预测空闲桩与拥堵等级推荐 | `GET /api/forecast/recommend` |
| 充电时段价格热力图 | 站点 × 24h 的繁忙程度，引导错峰 | `GET /api/user/peak-heatmap` |

### 3.3 充电站视角页 ——「站点分布与配置是否合理」

| 图表 | 说明 | 接口 |
|---|---|---|
| 站点选择器 + 单站利用率时段曲线 | 选中站点 90 天利用率与日内峰谷 | `GET /api/station/{id}/utilization` |
| 快慢充结构饼图 | 各站快充/慢充占比与功率结构 | `GET /api/station/{id}/mix` |
| 设备健康面板 | 故障率、累计充电次数/时长 Top 桩、远程运维建议 | `GET /api/station/{id}/health` |
| 区域覆盖散点 + 服务半径 | 站点分布密度，识别覆盖盲区（选址建议） | `GET /api/station/coverage` |

### 3.4 企业视角页 ——「营收最大化」

| 图表 | 说明 | 接口 |
|---|---|---|
| 营收/订单/电量三指标趋势 | 7/30/90 日切换 | `GET /api/enterprise/revenue-trend` |
| 站点营收排行 | 营收、订单、客单价 Top N | `GET /api/enterprise/station-ranking` |
| 用户增长与活跃曲线 | 新增用户、日活充电用户 | `GET /api/enterprise/user-growth` |
| 用户 RFM 分层 | 重要价值/保持/挽留用户分布 | `GET /api/enterprise/user-rfm` |
| 月度经营汇总表 | 月度营收、电量、订单、单桩日均收益 | `GET /api/enterprise/monthly` |

### 3.5 政府视角页 ——「设施是否服务好民生」

| 图表 | 说明 | 接口 |
|---|---|---|
| 行政区覆盖密度 | 各区站点数、桩数、每万人桩数 | `GET /api/gov/coverage` |
| 区域服务指标表 | 各区订单量、服务用户数、平均等待/排队 | `GET /api/gov/service-stats` |
| 充电量与等效碳减排 | 总电量换算减排量（给出换算系数说明） | `GET /api/gov/carbon` |
| 全城高峰负荷曲线 | 24h 全城负荷，辅助电网调度 | `GET /api/gov/peak-load` |
| 设施利用率公平性 | 各区利用率对比，识别「建而不用」 | `GET /api/gov/utilization` |

## 4. 接口契约（与 #2 共同冻结）

- 统一返回结构：`{ "code": 0, "message": "ok", "data": ..., "generatedAt": "..." }`。
- 金额字段后端返回整数分，前端统一除 100 展示为元；时间字段为 `+08:00` ISO 8601。
- 所有接口只读；大屏不做任何业务写入。
- 接口字段清单在 D2 前与 #2 共同冻结，之后只允许向后兼容新增。

## 5. 前端工程要点

1. **自适应**：主页按 1920×1080 设计，`transform: scale` 适配；子页响应式栅格。
2. **刷新策略**：KPI/事件流 5s 轮询，趋势类图表手动 + 60s 自动刷新；接口失败保留上次成功数据并显示「数据已过期」。
3. **降级**：预测接口无数据时显示「暂无预测」，不伪造曲线。
4. **mock 契约**：D1 期前端先用 mock 渲染，mock 文件置于 `src/mock/`，**结构必须与真实接口完全同构**（同为 `{code, message, data, generatedAt}`），接口就绪后只切 baseURL、不改组件代码。
5. **可演示性**：提供 `npm run dev` 启动与 `npm run build` 静态打包两种演示方式；打包产物可由 Flask 直接托管，答辩单进程起全栈；**北京地图 GeoJSON 与 ECharts 需离线内置**（虚拟机可能无外网）。

## 6. 排期（3 天，对应总体排期 D1–D3）

| 日 | 任务 |
|---|---|
| D1 | 搭建 Vue3 工程、大屏主题与布局骨架；用同构 mock 渲染主页（含数据质量面板） |
| D2 | 主页全部图表接入 mock；完成企业、用户子页原型；对接 Flask 真实 API（主页 + 企业/用户） |
| D3 | 完成充电站、政府子页；数据质量面板与预测模块接真实数据；全链路联调；晚间截图、答辩脚本与彩排 |

## 7. 验收标准

1. 主页 + 4 个子页全部可访问、全部图表真实渲染自 ADS 数据；主页「数据质量」面板显示清洗前后与检出对账。
2. 每个页面显示数据生成时间与「模拟数据」标注。
3. 大屏数值与 ADS 表抽验一致（与 #3 共同抽查至少 10 个指标）。
4. 接口中断时保留旧数据并提示过期，不白屏。
5. 答辩可从黄金数据复位后一键启动全栈并完整演示。

## 8. 管理职责交付物（PM）

- 第二阶段项目计划与 WBS、每日站会记录、风险登记表。
- 接口冻结会议纪要（与 #2）。
- 答辩脚本与全流程录屏（备用材料）。
