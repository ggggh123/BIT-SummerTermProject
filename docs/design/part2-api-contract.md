# 第二阶段大屏接口契约（#1 ↔ #2 冻结稿）

> 提出人：#1（PM / Web 大屏）｜状态：**待 #2 确认签字**｜日期：2026-09-14
> 上游依据：`Part2/01-PM-Web可视化大屏设计.md` §3–§4、`Part2/02-TL-Hadoop平台与Flask后端设计.md` §3.3
> 说明：前端已按本契约实现并接通同构 mock（`web/src/mock/`，23 个文件），Flask 只需按本表返回同样字段即可切换，**前端组件代码不改**。

## 1. 通用约定

| 项 | 约定 |
|---|---|
| 统一信封 | `{ "code": 0, "message": "ok", "data": ..., "generatedAt": "2026-09-14T10:05:00+08:00" }` |
| code | `0` = 成功；非 0 = 业务错误，前端显示错误并**保留上次成功数据** |
| 金额 | 一律整数**分**（`*Fen`），前端除 100 展示为元 |
| 时间 | 一律 `+08:00` ISO 8601 字符串 |
| 百分比 | 0–100 的数值（不是 0–1 小数） |
| 只读 | 全部接口只读，大屏不做任何写入 |
| 方法 | 全部 `GET`，查询参数见各表 |

心跳与刷新：主页 KPI/桩状态/事件流/站点 5s 轮询，趋势类 60s；其余页 60s。接口失败不得返回 200+空 data，应返回非 0 code。

## 2. 主页 `/api/overview`、`/api/quality`

| 端点 | 查询 | data 结构 |
|---|---|---|
| `/api/overview/kpis` | — | `{ totalRevenueFen, totalEnergyKwh, totalOrders, chargerCount, idleCount, onlineRate, windowDays }` |
| `/api/overview/stations` | — | `[{ stationId, name, district, longitude, latitude, chargerCount, idleCount, utilizationRate, revenueFen, priceFenPerKwh, orderCount, forecastEnabled }]` |
| `/api/overview/charger-status` | — | `{ idle, reserved, charging, fault, restarting }`（五者之和 = 总桩数） |
| `/api/overview/load-24h` | `stationId?` | `{ points: [{ stationId, observedAt, loadKw }] }`（**站点数 × 24 点**；当前官方批 7 站 = 168 点） |
| `/api/overview/events` | `limit?` | `[{ eventType, message, createdAt }]`（倒序） |
| `/api/quality/summary` | — | `{ runId, tables: [{ name, rowsBefore, rowsAfter }], issues: [{ rule, type, injected, detected, handled, recall }] }` |

主页营收趋势取 `/api/enterprise/revenue-trend?days=30` 的**最近 30 个点**。

## 3. 企业视角 `/api/enterprise`

| 端点 | 查询 | data 结构 |
|---|---|---|
| `/api/enterprise/revenue-trend` | `days=7\|30\|90` | `{ days, points: [{ date, revenueFen, orderCount, energyKwh }] }`（按 date 升序；**orderCount/energyKwh 必填**，缺失会让趋势图退化成零线） |
| `/api/enterprise/station-ranking` | `limit?` | `[{ stationId, name, utilizationRate, revenueFen, idleCount, chargerCount, orderCount }]` |
| `/api/enterprise/user-growth` | `days=30` | `{ days, points: [{ date, newUsers, activeUsers }] }` |
| `/api/enterprise/user-rfm` | — | `[{ segment, userCount, revenueFen }]`（8 个 RFM 分层） |
| `/api/enterprise/monthly` | — | `[{ month: "2026-06", revenueFen, energyKwh, orderCount, revenuePerChargerFen }]` |

口径自洽要求（前端已按此校验）：KPI 的总营收/总订单/总电量应等于 90 天趋势逐日汇总。

## 4. 用户视角 `/api/user`

| 端点 | data 结构 |
|---|---|
| `/api/user/price-compare` | `{ cityAvgFenPerKwh, stations: [{ stationId, name, priceFenPerKwh }] }` |
| `/api/user/price-distance` | `[{ stationId, name, distanceKm, priceFenPerKwh, idleCount }]`（距离以天安门 39.9087,116.3975 为参考点） |
| `/api/user/idle-ranking` | `[{ stationId, name, idleCount, chargerCount, idleRate }]` |
| `/api/user/peak-heatmap` | `{ hours: ["00:00"...23 个], names: [站名 8 个], values: [[hourIndex, stationIndex, 占用桩数]] }` |
| `/api/forecast/recommend`（选做） | `[{ stationId, name, predictedIdleCount, congestionLevel }]`；无预测数据时返回空数组，前端显示「暂无预测」 |

## 5. 充电站视角 `/api/station`

| 端点 | data 结构 |
|---|---|
| `/api/station/coverage` | `[{ stationId, name, district, longitude, latitude, chargerCount, serviceRadiusKm }]` |
| `/api/station/{id}/utilization` | `{ stationId, name, points: [{ observedAt, utilizationRate }] }`（日内 24 点，`observedAt` 降序或升序均可，前端会排序） |
| `/api/station/{id}/mix` | `{ stationId, name, fastCount, slowCount, fastPowerKw, slowPowerKw, fastOrderShare }`（快慢桩数之和 = 该站总桩数） |
| `/api/station/{id}/health` | `{ stationId, name, faultRate, faultCount, topChargers: [{ code, chargeCount, totalDurationSec, faultFlag }] }` |

`{id}` 为第一阶段 `stations.id`（1–8），与 `schema.sql` 一致。

## 6. 政府视角 `/api/gov`

| 端点 | data 结构 |
|---|---|
| `/api/gov/coverage` | `[{ district, stationCount, chargerCount, population, chargersPer10k }]` |
| `/api/gov/service-stats` | `[{ district, orderCount, servedUserCnt, avgWaitMin }]` |
| `/api/gov/carbon` | `{ totalEnergyKwh, co2SavedTon, factorTonPerMwh, factorNote, equivalentTrees }`（要求 `co2SavedTon = 电量/1000 × factorTonPerMwh`，前端会自校验） |
| `/api/gov/peak-load` | `{ points: [{ hour: "08:00", loadKw }] }`（24 点） |
| `/api/gov/utilization` | `[{ district, chargerCount, stationCount, utilizationRate }]` |

**注意**：`district`（行政区）在第一阶段 `database/schema.sql` 的 `stations` 表中**不存在**，属于第二阶段扩展列，见 `docs/management/part2-design-review.md` 的字段漂移清单。生成器与 ADS 必须显式提供该列。

## 7. 预测 `/api/forecast`（选做，按必做完成）

| 端点 | data 结构 |
|---|---|
| `/api/forecast/24h` | `{ runId, modelVersion, activatedAt, points: [{ stationId, forecastAt, horizonH, predictedLoadKw, predictedBusyCount, predictedIdleCount, congestionLevel, isPeak }] }` |
| `/api/forecast/metrics` | `{ horizons: [{ horizonH, mae, rmse, wape, baselineWape }] }` |

无有效预测批次时返回 `code != 0`（如 `code: 4041, message: "no active forecast"`），前端显示「暂无预测」而不伪造曲线。

## 8. 静态托管约定

- 前端构建产物 `web/dist`（`base: './'`、hash 路由）由 Flask 直接托管；ECharts 与北京 GeoJSON 均在产物内（离线可用）。
- 单进程起全栈：Flask 托管 `dist` + 提供 `/api/*`，无需 Node 运行时。
- 前端切换真实接口只需 `VITE_USE_MOCK=false VITE_API_BASE=...`，**组件代码不改**。

## 9. 冻结签字

| 角色 | 姓名 | 结论 | 日期 |
|---|---|---|---|
| #1 PM（前端） | 王浩恩 | 提出并已按此实现 | 2026-09-14 |
| #2 TL（Flask） | 杨佳车 | 待确认 | |
