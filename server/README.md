# 第二阶段 Flask 后端（`/api/*`）

> 契约真源：[`docs/design/part2-api-contract.md`](../docs/design/part2-api-contract.md)（#1 王浩恩提出，23 个 mock JSON / 27 条路由）
> 分支：`feat/part2-web`　｜　实现：Flask + 标准库 `sqlite3`（**只读**）
> 依据：[`02-TL-Hadoop平台与Flask后端设计.md`](../docs/design/) §3.2 / §3.3

## 1. 定位

按契约 §2–§7 把 27 条 `/api/*` 端点全部实现，返回结构与前端同构 mock
（`web/src/mock/*.json`）**逐字段兼容**，前端只需改环境变量即可切换，
组件代码零改动。全部接口 `GET`、全部只读，大屏不做任何写入。

## 2. 起服务

```bash
# 1) 装依赖（只有 Flask 本体 + 跨域中间件）
python -m pip install -r server/requirements.txt

# 2) 确认 ADS 库存在（见 §5 数据链路）
ls handoff/ads/ads.db

# 3) 起服务
python server/app.py                  # 默认 http://0.0.0.0:5000
PORT=8000 python server/app.py        # 换端口
ADS_DB=/path/to/ads.db python server/app.py   # 换库位置
```

起好后先看自检端点：

```bash
curl -s http://127.0.0.1:5000/api/health | python -m json.tool
```

它会上报：库路径与存在性、`runId`、数据窗口、各表行数、已注册路由数 / 契约路由数。

## 3. 前端切换到真实接口

`web/src/api/client.js` 里 `USE_MOCK = (import.meta.env.VITE_USE_MOCK ?? 'true') !== 'false'`，
所以只需：

```bash
# 方式一：临时跑 dev server
cd web
VITE_USE_MOCK=false VITE_API_BASE=http://localhost:5000/api npm run dev

# 方式二：写进 web/.env.local（推荐，长期生效）
echo "VITE_USE_MOCK=false" >> web/.env.local
echo "VITE_API_BASE=http://localhost:5000/api" >> web/.env.local
```

生产形态（契约 §8）：`npm run build` 产出 `web/dist`，Flask 直接托管，
单进程同时给 `dist` 和 `/api/*`，集成机上不需要 Node 运行时。已开放
`/api/*` 的 CORS，前端 dev server 与 Flask 分端口也不会有跨域问题。

## 4. 端点清单

| 命名空间 | 路由数 | 端点 |
|---|---|---|
| `/api/overview` | 5 | `kpis`、`stations`、`charger-status`、`load-24h`、`events` |
| `/api/quality` | 1 | `summary` |
| `/api/enterprise` | 5 | `revenue-trend`、`station-ranking`、`user-growth`、`user-rfm`、`monthly` |
| `/api/user` | 4 | `price-compare`、`price-distance`、`idle-ranking`、`peak-heatmap` |
| `/api/station` | 4 | `coverage`、`{id}/utilization`、`{id}/mix`、`{id}/health`（`{id}` = 1–8） |
| `/api/gov` | 5 | `coverage`、`service-stats`、`carbon`、`peak-load`、`utilization` |
| `/api/forecast` | 3 | `24h`、`metrics`、`recommend`（选做） |
| — | 1 | `/api/health`（非契约，联调排障用） |

错误码约定（HTTP 一律 200，业务码放信封里，避免 axios 先 reject 拿不到 `message`）：

| code | 含义 |
|---|---|
| 0 | 成功 |
| 4001 | 查询参数非法（`days` 不在 7/30/90、`limit` ≤ 0 等） |
| 4004 | 站点不存在 / 接口不存在 |
| 4041 | 无有效预测批次（前端据此显示「暂无预测」） |
| 5000 | 服务内部错误 |
| 5001 | ADS 库缺失或不可读（含修复命令） |

## 5. 数据链路与口径（**联调前先读这一节**）

### 5.1 数据从哪来

```
SCML 生成器 ──► handoff/ods/（ODS 交接包）
                    │  part2/scml/warehouse/jobs/build_local.py  ← 纯标准库，约 15s
                    ▼
              handoff/ads/ads.db（SQLite 单文件，15 张表，5.5 MB）
                    │  server/services/ads_reader.py  ← sqlite3 只读
                    ▼
              Flask /api/*  ──►  web/dist
```

> **重要事实**：任务下达时提到的 `handoff/ads/ads.db` 与 `handoff/ads/json/*.json`
> 在仓库和本机**都不存在**。当前 `ads.db` 是本次用 SCML 生成器自产 ODS 后，
> 由 #4 的 `build_local.py` 自行物化出来的**过渡产物**（`ads_meta.sourceKind = ods-handoff`）。
> 正式链路上 DWS/ADS 应由 SparkSQL 产出 Parquet，再导出同一张表结构的 SQLite；
> 届时**只换数据、不改 Flask**。

按仓库 `.gitignore`，`handoff/` 与 `*.db` **不入库**（数据走共享文件夹/网盘），
所以 clone 之后需要自己把数据放回来：

```bash
# 从共享文件夹取 handoff/ods，或按配置重跑 SCML 生成器（见 handoff/ods_config_used.yaml）
python part2/scml/warehouse/jobs/build_local.py --ods handoff/ods --dws handoff/dws --ads handoff/ads
python server/app.py
```

### 5.2 关键口径

* **金额**一律整数分（`*Fen`）；**时间**一律 `+08:00` ISO 8601；**百分比**为 0–100。
* **窗口**统一 90 天（`2026-06-17 → 2026-09-14`）。KPI 的总营收/总订单/总电量与
  `/api/enterprise/revenue-trend?days=90` 的逐日汇总**严格相等** —— 由
  `ads_reader.window_totals()` 单一口径产出，不存在两套算法。
* **五状态之和 = 总桩数**、**快慢桩数之和 = 该站总桩数**：都由清洗后的桩维度聚合，
  天然自洽（当前验证值：287 桩，其中 1 个站的状态字段非法被 R08 剔除）。
* **营收只统计 `completed` 订单**；`cancelled` 不进 ADS（口径见 `ads_meta.cleaningSource`）。
* **距离**以天安门 `39.9087, 116.3975` 为参考点，haversine 后**保留 2 位小数**
  （前端 `userStationModels.test.mjs` / `viewModel.test.mjs` 按 2 位校验排序）。
* 清洗规则逐条对齐《03-PRL》§3.2；`/api/quality/summary` 的 `injected` 取自生成器
  `injection_log.json`（Q1–Q10 ↔ R01–R10），`detected` 是 ADS 侧独立复算，
  两者统计口径不完全等价，`recall` 仅作覆盖度参考（当前 injected 32775 / detected 10717）。

### 5.3 如实标注的降级与假设（答辩防追问）

全部写在 `ads_meta` 表里，可直接 SQL 查证：

| key | 说明 |
|---|---|
| `forecastIsBaseline` | `1`。Spark MLlib 批次未交接，按《05-PE》§8 降级预案用 **seasonal-naive** 顶替，批次与响应里都带 `isBaseline` / `note`，**不伪造**。 |
| `newUserNote` | `new_user_cnt` 用**首单新客**口径（当天首次完成订单的用户数）。生成器把 5000 个用户的注册时间全放在窗口之前（2026-03~06），窗口内没有任何注册事件，按 `registered_at` 聚合会得到一条零线。 |
| `populationSource` | 北京市第七次全国人口普查常住人口，**外部参考数据**，非生成器产出。 |
| `serviceRadiusNote` | 服务半径是运营规划参数（按站点订单需求折算），**非实测**。 |
| `avgWaitNote` | 平均等待 = `started_at − reserved_at`；ODS 未建模「预约到开工」的排队时长，故当前为较低值。 |
| `carbonFactorNote` | 0.581 tCO₂/MWh（全国电网平均排放因子，项目假设）。 |

## 6. 自测

```bash
python server/tools/selftest_contract.py        # 契约口径：信封 / 自洽 / 格式 / 错误码
python server/tools/selftest_frontend_shape.py  # 前端字段结构对齐（「组件不用动」的证据）
```

当前结果（2026-09-14）：

* `selftest_contract.py` — **55 项全通过**：27 条契约路由 `code=0` / `data` 非空 /
  `+08:00`；KPI = 90 天趋势汇总；`co2SavedTon = 电量/1000 × 0.581`；五状态之和 = 287；
  8 个站快慢桩之和 = 各站总桩数；金额整数分；距离 2 位小数且升序；矩阵维度正确。
* `selftest_frontend_shape.py` — **46 项结构性比对、0 处不兼容**：
  mock 出现的每个字段接口都有且类型兼容；接口额外多出 65 个字段（`dt`、`stationId`、
  `isBaseline`、`note` 等），前端不使用、无害。

## 7. 目录结构

```
server/
├── app.py                          应用入口：信封错误处理、CORS、静态托管、/api/health
├── requirements.txt
├── api/                            按契约命名空间拆分的蓝图
│   ├── overview.py  quality.py  enterprise.py  user.py
│   └── station.py   gov.py      forecast.py
├── services/
│   ├── ads_reader.py               sqlite3 只读连接、口径工具、参数校验、排序白名单
│   # ads_cleaning.py 已迁至 part2/scml/warehouse/jobs/_lib.py（清洗是 #4 的生产职责）
│   └── envelope.py                 统一响应信封
└── tools/
    # build_ads_db.py 已迁至 part2/scml/warehouse/jobs/build_local.py
    ├── selftest_contract.py        契约一致性自测
    └── selftest_frontend_shape.py  前端字段结构对齐自测
```

## 8. 已知限制

1. **数据是过渡产物**：`ads.db` 由本仓库的离线物化作业生成，不是 Spark/Hive 正式链路
   产出；换成正式 Parquet 导出的 SQLite 后 Flask 侧零改动（表结构即契约）。
2. **遥测表未进 ADS**：100 万行遥测只做质量计数（流式扫描），ADS 不直接消费桩级明细；
   桩级指标来自 `ods_chargers` 的快照列与 `ods_station_hourly` 的小时聚合。
3. **预测是基线**：`seasonal-naive`（昨日同时刻值），`/api/forecast/metrics` 给出
   MAE/RMSE/WAPE 与「常量均值基线」的 `baselineWape` 对照，可自证增益。
4. **未做鉴权**：契约 §1 明确全部只读、大屏内网演示，故不加认证；
   若上公网须另加反向代理与鉴权。
