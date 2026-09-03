# Web ECharts 运营大屏

电动汽车充电平台的只读可视化大屏。单页、零外部依赖（ECharts 5.6.0 已本地化）、不登录、不写任何业务数据。

## 运行边界（硬约束）

- **只读**：大屏从不打开 SQLite、从不发送业务写操作；唯一数据源是服务端原子生成的 JSON 快照。
- **快照属主**：`dashboard/runtime/dashboard_snapshot.json` 由 Qt 服务端 `SnapshotWriter`（#2）在每次成功业务事务后临时文件写完原子替换；`runtime/` 已被 gitignore，页面视为不可信输入。
- **契约**：页面在渲染前用 `assets/contracts.js#validateSnapshot` 做 v1 全量校验（19 项规则）；校验失败的数据永远不上屏。
- **禁止**：登录、业务写入、多页面后台、真实 GIS 底图、外部 CDN。

## 本地启动

```bash
# Windows / 任意有 Python 的环境（端口可换；8080 常被占用）
python -m http.server 8177 --directory dashboard
# 浏览器打开
http://127.0.0.1:8177/
```

无服务端时的演示数据：把 `dashboard/tests/fixtures/valid_snapshot.json` 复制为 `dashboard/runtime/dashboard_snapshot.json`。

## 数据获取与轮询（assets/poller.js）

- 页面以 **2000ms** 同源轮询 `runtime/dashboard_snapshot.json`（`?ts=` 时间戳 + `cache: 'no-store'` 穿透缓存）。
- **先验证后改状态**：HTTP 错误、畸形 JSON、契约校验失败都只更新状态，不触碰图表。
- `onData` 仅在 `snapshotVersion` 变化时触发（同一秒的新版本也会重绘恰好一次）；同版本新 `generatedAt` 只走心跳刷新状态与时间，不重绘。
- 连接状态由 `lastSuccessfulFetchAt` 判定：超过 `10000ms` 未成功获取有效快照 → `stale`（保留旧数据，轮询不停止）。
- 四态标签（右上角）：`live` 已连接（实时快照）｜`stale` 连接超时 · 显示最后成功数据｜`cached` 正在展示缓存快照（非实时）｜`error` 快照加载失败 · 保留旧数据。

## 缓存降级（fallback/）

- 仅当**没有任何历史快照**且 live 获取失败时，加载 `fallback/dashboard_snapshot.json`，来源标记 `cached`。
- fallback 永不冒充实时数据——`cached` 标签由 poller 提供并始终可见。
- 发布期流程：由 #4/#5 用批准的 golden run **重新生成** fallback 快照（内容可为演示数据，但必须带同一预测 run/payload），页面依旧显示 `cached`。

## 页面结构（index.html）

| 区域 | 说明 |
|---|---|
| 顶栏 | 标题、`#connection-state` 状态徽章、`#generated-at` 快照生成时间 |
| 4 张 KPI 卡 | 今日营收 / 今日充电量 / 今日订单 / 在线率·空闲桩 |
| 近 7 日营收 | `#revenue-chart` 柱状图（元） |
| 桩状态分布 | `#status-chart` 环图，固定顺序 空闲→已预约→充电中→故障→重启中 |
| 负荷与预测 | `#load-chart` 过去 24h 实际 + 未来 24h 预测双曲线（kW），含高峰标记与站点选择器 `#station-select` |
| 利用率排行 | `#ranking-chart` 横向条形（%），降序 |
| 站点分布 | `#station-map` 经纬度散点，悬停 tooltip（名称/空闲/总数/营收/是否参与预测），点击联动详情 |
| 站点详情 | `#station-detail` 当前选中站点的关键字段 |
| 事件流 | `#event-list` 开始充电/完成/故障/恢复/重启/预测发布，文本节点渲染 |

单页无跳转：没有登录页、没有二级页面、没有隐藏导航（设计 §4.4 明确不做）。

## 响应式

- ≥1100px：宽屏运营布局（8/4 分栏）；≤1100px：全部单列堆叠；≤800px：KPI 折半、图表降高。
- 状态不仅靠颜色（徽章有文字），焦点可见，图表有最小高度。

## 测试

```bash
node --test dashboard/tests/contracts.test.mjs dashboard/tests/models.test.mjs dashboard/tests/poller.test.mjs
```

共 36 项（契约 19 / 模型 8 / 轮询 9）。轮询测试通过注入 `fetchImpl` 覆盖：立即首载、心跳不重绘、同秒新版本恰好重绘一次、HTTP/畸形 JSON/非法契约保留旧数据、fallback cached、过期阈值。

## 修改 UI 的注意点

1. **DOM id 是 JS 契约**：`connection-state`、`generated-at`、`kpi-revenue/energy/orders/online/idle`、`revenue-chart`、`load-chart`、`load-note`、`station-select`、`status-chart`、`ranking-chart`、`station-map`、`station-detail`、`event-list`——删改任意一个，对应功能静默失效。
2. **图表容器必须有高度**（`.chart` 类或自定义 height），否则 ECharts 初始化为 0 尺寸；不要用 `display:none` 隐藏容器（初始化会拿不到宽高），改用布局调整并在窗口 resize 外的布局变化后手动触发 `chart.resize()`。
3. **脚本顺序不可变**：`vendor/echarts.min.js`（普通 script，暴露 `window.echarts`）必须先于 `assets/dashboard.js`（`type="module"`）加载。
4. **事件流只能用 `textContent` 构建节点**，禁止把快照内容拼进 `innerHTML`。
5. 状态徽章依赖 `styles.css` 的四个类：`pill-live / pill-stale / pill-cached / pill-error`，改主题请保留。
6. 主题色集中在 `styles.css :root` 变量，改配色优先改变量。
7. 新增图表 = `models.js` 加 builder（先写测试）+ `dashboard.js` `initChart`/渲染 + HTML 容器，遵循 TDD。

## 与其他子系统的关系

- 服务端（#2）每次成功业务事务后原子替换快照；每次成功事务同步递增 `snapshot_meta.version`，页面以该版本驱动重绘。
- 交叉契约验证（`tests/server-fixture.test.mjs`，CTest 包装 Node）在服务端 `SnapshotWriter` 可用后执行（计划 Task 6 Step 1–2），断言真实服务端快照与本目录 fixture 同为 camelCase v1 契约。
