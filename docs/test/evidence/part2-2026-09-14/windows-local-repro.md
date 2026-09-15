# Windows 本地复现与真实接口验证（无 VM / 无 Spark，2026-09-14）

> 执行人：#1 王浩恩（PM）｜环境：Windows + Python 3.13.3（临时 venv `.venv-win`，flask 3.1.3 / flask-cors 6.0.5）+ Node 24.15.0 + Chrome headless
> 目的：在等 VM 的 Spark 链路时，用 #4 的纯标准库第二实现（`build_local.py`）把 `ODS → DWS → ADS → Flask → 前端` 在**没有 Hadoop/Spark 的机器上**整条走通，验证集成分支，并为抽验提供第二组真值。

## 1. 步骤与结果

| 步 | 命令（仓库根目录，venv 内执行） | 结果 |
|---|---|---|
| 1 生成正式规模 ODS | `python part2/scml/data_generator/generator.py --config part2/scml/config/part2_scml_full.yaml --out handoff/ods` | **18.0 s**；8 站 / 288 桩 / 5,000 用户 / 120,000 单 / 1,000,000 遥测 / 17,280 小时 / 20,000 事件；`kind=ods-handoff`、`seed=20260914`、窗口 2026-06-17→09-15；注入 32,775 条 |
| 2 物化 DWS + ADS | `python part2/scml/warehouse/jobs/build_local.py --ods handoff/ods --dws handoff/dws --ads handoff/ads` | **17.4 s**；`handoff/ads/ads.db` 5.71 MB / 15 张表；DWS：station_day 720、charger_day 25,601、user_day 98,389、region_day 450；质量 `injected 32775 / detected 10717`；预测 `seasonal-naive-baseline`（`isBaseline=1`） |
| 3 契约自检 | `python server/tools/selftest_contract.py` | **55/55 通过**（exit 0） |
| 4 前端字段对齐 | `python server/tools/selftest_frontend_shape.py` | **46 项结构比对、不兼容 0 处**；接口额外多出 65 处字段（8 类，见 §3） |
| 5 静态托管 | `python server/tools/selftest_static_dist.py` | `[OK] Flask serves web/dist and keeps /api errors in envelope` |
| 6 五页真实接口渲染 | `VITE_USE_MOCK=false VITE_API_BASE=/api npm run build` → Chrome `--headless=new --dump-dom` 逐页抓 DOM | 主页 **5** 图 / 用户 **4** / 充电站 **4** / 企业 **4** / 政府 **3**；控制台零报错 |

主页 KPI 与 `/api/overview/kpis` 逐位一致：

| 指标 | 接口值 | 页面显示 |
|---|---|---|
| 累计营收 | `totalRevenueFen=475820737` | **¥4,758,207.37** |
| 累计充电量 | `totalEnergyKwh=3972088.2` | **3,972,088.20 kWh** |
| 累计订单 | `totalOrders=112422` | 112,422 |
| 桩在线率 / 空闲桩 | `onlineRate=99.3` / `idleCount=285` | **99.3%** / 空闲桩 285 |

`/api/health`：`apiRouteCount=28`（27 契约 + `/api/health`）、窗口 90 天、`runId=ads-20260914194230`、表行数 `ads_station 8 / ads_charger 287 / ads_station_hourly 17280 / ads_event 20000 / ads_forecast_24h 144 / ads_quality_issue 10`。

## 2. 发现

**F1（会直接报错）`pipelines/part2/handoff/ads/ads.db` 不能作为服务端数据源。** 该库只有 5 张表（`ads_charger_health`、`ads_gov_service`、`ads_peak_hour`、`ads_station_ranking`、`dws_region_day`），缺 `ads_daily`、`ads_station`、`ads_station_hourly` 等。用它起服务，`/api/overview/kpis` 返回 `code=5000 服务内部错误：OperationalError: no such table: ads_daily`，契约自检 7 组里 3 组失败。服务端需要的是 #4 那套 **15 张表**的 `handoff/ads/ads.db`（Spark 路线 `export_ads_db.py`，或本机 `build_local.py`）。

**F2（抽验口径）两组 KPI 真值并存，必须在抽验表标注规模。**

| 数据源 | 营收 | 订单 | 在线率 | 桩数 |
|---|---|---|---|---|
| 1/10 规模（VM Spark 路线，见本目录 `README.md`） | 49,863,703 分 | 11,424 | 76.7% | 30 |
| 正式规模（本次本地 `part2_scml_full.yaml`） | **475,820,737 分** | **112,422** | **99.3%** | **287** |

`docs/management/part2-metric-checklist.md` 第 1–4 行填的是前者。若答辩用后者（正式规模）的库，两套数字相差约一个数量级 → 建议该表增列「数据源 `run_id` / 规模」，并明确答辩演示规模后再重测第 6–15 行。

**F3（非缺陷）** 五页 DOM 中的「暂无预测」是设计内降级文案：站点 7/8 `forecastEnabled=false`，预测批次为 `seasonal-naive` 基线（`isBaseline=1`），与《00》§1.4「不作虚假声明」一致。

## 3. 跨端字段差异（供 #4 核对）

`selftest_frontend_shape.py`：mock 出现的每个字段接口都有且类型兼容；**接口额外多出 65 处字段、8 类**：`dt`、`stationId`、`name`、`detectedTotal`、`injectedTotal`、`source`、`isBaseline`、`note`。前端不使用、无害，无需改动。

## 4. 未覆盖

HDFS/YARN 侧的 SparkSQL 路线（`part2/scml/scripts/run_dim_date.sh` → `run_dws_ads.sh` → `warehouse/jobs/export_ads_db.py`）仍需在 VM 上跑并出示 YARN 记录；`reconcile.py` 对「Spark SQL 侧」与「Python 侧」两份实现的交叉比对本次未执行（Python 侧单独 `13/13` 已在 VM 路线验证）。
