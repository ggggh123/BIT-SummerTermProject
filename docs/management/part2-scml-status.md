# 第二阶段 #4（SCML）进度梳理与待办清单

> 整理人：应 #4 要求整理｜日期：2026-09-14
> 核对对象：`part2/scml/**`（`feat/part2_SCML`）、`docs/design/part2-api-contract.md`、`docs/management/part2-design-review.md`、`docs/management/part2-metric-checklist.md`（`feat/part2-web`）、`Part2/04-SCML-模拟数据生成与数仓分层设计.md`、第一阶段 `database/schema.sql`、`#3 PRL 输入/输出契约草案 v0.1`
> 用法：只做梳理与排期，不改口径。每条待办给定位与归属，闭环后在「状态」列改标。

---

## 0. 结论速览（先看这 6 条）

| # | 事实 | 影响 |
|---|---|---|
| 1 | **ODS 生成器 + 交接校验已跑通**：样例与**正式规模**双双 `validate_handoff.py` 返回 `[OK]`，4 个单测全绿，同种子哈希一致（实测 2026-09-14） | 基础可用，#3 可以开始联调 |
| 2 | ~~最新一轮改造还在工作区、未提交~~ → **已于本轮提交并推送到 `feat/part2_SCML`**，且顺带修掉了 T1/T3/T4/T5 四个 P0 | 队友在 GitHub 上看得到最终字段 |
| 3 | **数仓分层 DWS/ADS 一行 SQL 都还没写**：`warehouse/` 里只有 `sql/ads_schema.sql`（DDL 契约）和 `jobs/build_dim_date.py` | 老师要求 (5)「SparkSQL 完成 ODS→DWD→DWS→ADS」未开工，**当前最大缺口** |
| 4 | `ads_schema.sql` 只覆盖前端接口契约的**约一半字段**（缺 chargerCount / 经纬度 / 单价 / onlineRate / 月度 / 用户增长 / population / avgWaitMin / utilizationRate / 快慢桩 mix / 小时级 observedAt 等） | 直接按现 DDL 建 ADS，前端约 12 个接口会拿不到数据 |
| 5 | 设计审查 R1/R2/R4：**R2 代码已落实**（`mobile`/`energy_increment_kwh`/orders 无 `station_id`）、**R1 只落了一半**（生成器加了 `district`，`schema.sql` 与《04》字典未同步）、**R4 未决策** | 需要文案闭环 + 一次口径决策 |
| 6 | `origin/feat/part2-02`（#2 TL）**零独有提交**，指向 `origin/main` 的 `22b146c` | Hadoop 环境 + Flask 后端在远端完全不可见；而 `scripts/part2/*` 环境脚本反而挂在 `feat/part2-web` 上 |

---

## 1. 团队三分支进度（2026-09-14 fetch 实况）

本地仓库为浅克隆，`BIT-SummerTermProject-latest` 是挂在 `feat/part2_SCML` 上的 worktree。

| 分支 | 角色 | 最新提交 | 实际内容 | 状态 |
|---|---|---|---|---|
| `feat/part2_SCML` | #4 SCML（本文） | `ff255d1 feat(part2): add scml data foundation` + 本轮 `fix(part2/scml)` | `part2/scml/` 17 个文件：生成器、config ×2、脚本 ×5、契约、`ads_schema.sql`、`build_dim_date.py`、测试 ×2 | **已提交并推送**：`_row_id`/`\N`/JSONL/manifest 契约/去 `station_id` + T1/T3/T4/T5 四个 P0 修复 |
| `feat/part2-web` | #1 PM | `373ded1 docs(part2): 接口契约冻结稿、设计缺陷审查、数值抽验表模板` | ① `web/**` Vue3+ECharts 大屏：主页/企业/用户/充电站/政府 5 个视角全部接通同构 mock（23 个 JSON）；② `dashboard/**` 一份备用静态大屏 + 快照降级；③ `docs/design/part2-api-contract.md`（接口契约冻结稿）、`docs/management/part2-design-review.md`（设计缺陷清单 R1–R10）、`docs/management/part2-metric-checklist.md`（15 项指标抽验表）；④ **`scripts/part2/**` 环境安装/体检/验收脚本 + `docs` 里的版本基线**（含 JDK17 / Hadoop 3.4.1 / Spark 3.5.7，实测 21+12 项全过） | #1 侧基本就绪，等 #2 后端 |
| `feat/part2-02` | #2 TL | `22b146c`（== `origin/main`） | **无独有提交** | 环境脚本实际落在 part2-web；Hadoop/Flask 后端尚未推送 |
| `dev` / `main` | 基线 | `1bd4b89` / `22b146c` | 第一阶段三项系统 + 文档 | 冻结基线 |

> **提醒 #2**：`feat/part2-02` 目前与 `main` 同点。若 Hadoop/Spark/Flask 的产物已经做出来（环境脚本已证明做过），请尽快 commit + push，否则「全链路集成」这一环在 GitHub 上等于不存在；`scripts/part2/**` 是否应从 `part2-web` 迁到这条分支也需要定一下归属。

---

## 2. #4 已完成清单（代码级，逐文件）

| 产物 | 路径 | 状态 |
|---|---|---|
| 确定性 ODS 生成器（7 张表、固定 seed、SHA-256） | `part2/scml/data_generator/generator.py` | 已跑通 |
| 小样例配置（2 站 / 4 桩 / 20 用户 / 50 单 / 120 遥测 / 2 天） | `config/part2_scml_sample.yaml` | 已跑通 |
| 正式规模配置（8 站 / 36 桩·站 / 5000 用户 / 12 万单 / 100 万遥测 / 90 天 / 2 万事件） | `config/part2_scml_full.yaml` | 已实跑：12.9s，`[OK]`，4 张事实表各 90 个 `dt=` 分区（09-01 → 11-29） |
| 生成 + 校验脚本 | `scripts/run_scml_sample.sh`、`run_scml_full.sh` | 已跑通（样例） |
| 交接包校验（manifest / 行数 / SHA-256 / 注入日志 / 契约版本） | `scripts/validate_handoff.py` | 已跑通 |
| ODS 入 HDFS（`/ev-charging/ods` + `_SUCCESS`） | `scripts/hdfs_put_ods.sh` | 未在集成机验证 |
| `dim_date` 生成（Spark on YARN） | `scripts/run_dim_date.sh` + `warehouse/jobs/build_dim_date.py` | 未在集成机验证 |
| 交接契约说明 | `contracts/handoff_contract.md` | 已有 |
| ADS 落地 DDL（SQLite 契约） | `warehouse/sql/ads_schema.sql` | 已有但**字段不全**，见 §5 |
| 契约测试 | `tests/test_generator_contract.py`、`tests/test_ads_schema_contract.py` | 4/4 通过（新增「遥测落在时间窗内」「10 类注入覆盖全部目标表」） |
| DWS/ADS SparkSQL 作业 | `warehouse/jobs/**` | **未开始** |
| 对账脚本 `reconcile.py` | — | **未开始** |
| 正式 ODS 交接包（`kind: ods-handoff`） | `handoff/ods`（git 忽略） | 只有 `prl-test-fixture` 样例包 |

### 已实测通过（本次核对）

```text
$ python -m unittest discover -s tests -v
test_ads_schema_creates_required_tables ... ok
test_generator_writes_ods_handoff_manifest_and_injection_log ... ok
Ran 2 tests in 0.083s   OK

$ python data_generator/generator.py --config config/part2_scml_sample.yaml --out <tmp>
$ python scripts/validate_handoff.py <tmp>
[OK] <tmp> manifest, hashes, row counts and injection log are valid
```

实测表头（**已经符合 R2 与 PRL 契约**）：

```text
ods_stations        _row_id,id,name,address,district,latitude,longitude,price_fen_per_kwh,forecast_enabled,created_at
ods_orders          _row_id,id,user_id,charger_id,status,reserved_at,started_at,ended_at,energy_kwh,amount_fen
ods_telemetry       _row_id,id,charger_id,recorded_at,power_kw,energy_increment_kwh,event_type
ods_station_hourly  _row_id,station_id,observed_at,pile_count,rated_power_kw,temperature_c,is_holiday,busy_count,load_kw
ods_users           _row_id,id,mobile,nickname,avatar_path,balance_fen,status,registered_at
ods_chargers        _row_id,id,station_id,code,type,power_kw,status,charge_count,total_duration_sec,updated_at
ods_events          JSONL（一行一对象）
```

对照 §2 的三条「会直接导致返工」的缺陷：

- **R2 字段名漂移 → 代码里已经修对了**：用的是 `users.mobile`、`telemetry.energy_increment_kwh`（并保留 `event_type`）、`orders` 里**没有** `station_id`（`station_id` 只用于算金额，经 `charger_id → chargers.station_id` 派生）。**但《04》文档数据字典还没改**，仍是错的（见 T12）。
- **R1 `stations.district` → 生成器已显式新增该列**，且贯穿 ODS；**但 `database/schema.sql` 未加列、《04》字典未标注「第二阶段扩展列」**，#3 的 `dim_stations`、#5 的 `dws_region_day`/`ads_gov_service` 若无统一约定会各写一套（见 T11）。
- **R4 遥测口径/规模 → 未决策，且代码有硬 bug**（见 T1/T2）。

---

## 3. 中断中的改动（**已提交**，`git status` 已清空）

原工作区有 3 个文件被改动、尚未 commit，本轮已一并提交，并在此基础上修掉 4 个 P0：

| 文件 | 中断时已有的改动 | 本轮新增的修复 |
|---|---|---|
| `data_generator/generator.py` | ① 全表加 `_row_id`；② null 统一写 `\N`；③ `ods_events` 改 JSONL；④ manifest 补 `contractVersion / kind / run_id / sourceKind / files[]`；⑤ `orders` 去掉 `station_id`；⑥ 注入日志改 `rule/table/row_id/business_key` | ⑦ **T1** 遥测时间轴收敛到窗口内；⑧ **T3** 四张事实表按 `dt=` 分区；⑨ **T4** 3 处静默注入改为记账；⑩ **T5** 注入量按设计比例（0.3%–1%），且每类覆盖设计指定的**全部**目标表；⑪ 事件流同样铺满 90 天 |
| `scripts/validate_handoff.py` | `count_file_rows()` 支持 csv/jsonl/json；契约版本与 `kind` 白名单；注入条目必须有 `row_id` | 新增分区校验（分区行数合计 == 表行数、`dt` 目录命名、分区必须落在 `dataWindow` 内、每个分区有 `_SUCCESS`）；新增「每条 Q 规则必须覆盖设计指定的表集合」校验（复用 `generator.QUALITY_RULES`，不重复定义） |
| `tests/test_generator_contract.py` | 断言 `_row_id` 在、`station_id` 不在、`ended_at == \N`、events 是 JSONL、日志用 `rule` 字段 | 断言改为「按注入日志里的 `row_id` 反查该行确是 `\N`」（不再依赖固定行号）；新增 `test_telemetry_stays_inside_history_window`、`test_injection_covers_every_documented_table` |
| `contracts/handoff_contract.md` | — | 补 ODS 分区表、分区键取**注入前**的干净时间戳、`dt` 不入列 + Spark `basePath` 读法 |
| `README.md` | — | 补分区输出结构、10 类注入比例表、遥测时间轴口径 |

### 修复后的实测结果

```text
$ python -m unittest discover -s tests
Ran 4 tests in 0.399s   OK

# 小样例（prl-test-fixture）
[OK] manifest, hashes, row counts, dt partitions and injection log are valid

# 正式规模（ods-handoff），12.9s
ods_stations           rows=       8 parts=  0
ods_chargers           rows=     288 parts=  0
ods_users              rows=    5000 parts=  0
ods_orders             rows=  120000 parts= 90 (2026-09-01 -> 2026-11-29)
ods_telemetry          rows= 1000000 parts= 90 (2026-09-01 -> 2026-11-29)
ods_station_hourly     rows=   17280 parts= 90 (2026-09-01 -> 2026-11-29)
ods_events             rows=   20000 parts= 90 (2026-09-01 -> 2026-11-29)
totalInjected = 32775        # 旧版是 10 条

Q1  5600 {orders 600, telemetry 5000}   Q6   600 {orders 600}
Q2 11200 {orders 1200, telemetry 10000} Q7   360 {orders 360}
Q3  3360 {orders 360, telemetry 3000}   Q8    16 {chargers 1, users 15}
Q4 11200 {orders 1200, telemetry 10000} Q9     1 {stations 1}
Q5   412 {orders 360, station_hourly 52} Q10  26 {stations 1, users 25}

同种子两次生成：逐表 SHA-256 一致，injection_log.json 逐字节一致
```

> **仍待 #2/#3 确认**：T2 遥测口径（100 万「抽样帧」vs 400 万「全量 5 分钟」）。
> 现在的实现是**前者**——100 万帧均匀铺在 90 天的 5 分钟栅格上，即每个 tick 约 39/288
> 个桩上报。若改为全量 5 分钟，需要把 `telemetry_count` 提到约 373 万–448 万，
> 生成耗时约 45s、内存占用显著上升，且要重估 HDFS 与 Spark 资源。

> 顺手可清理的小问题（不阻塞）：
> - manifest 同时写了 `runId/run_id`、`generatedAt/generated_at` 两套键，属兼容期补丁，建议约定「只保留一套 + 在契约里声明」避免两处漂移；
> - `validate_handoff.py` 未校验「7 张 ODS 表齐全」与每表 `_SUCCESS` 存在；
> - `validate_handoff.py` 的 `count_file_rows(..., "json")` 对 `injection_log` 用 `len(payload["issues"])`，一旦以后给日志加别的数组字段会误判，建议显式传一个 `rows_key` 或直接复用 manifest 里的 `rows`。

---

## 4. 待办清单（按阻塞程度排序）

### P0 — 不修就会报错 / 返工 / 无法验收

| ID | 待办 | 定位 | 归属 | 状态 |
|---|---|---|---|---|
| ~~**T1**~~ | ~~**遥测时间轴越界（真 bug）**~~ 改为 `slot = (n-1) * window_slots // total`，严格落在 `[start_date, +history_days)` | `generator.py::_telemetry` | #4 | ✅ 已修 |
| **T2** | **R4 口径决策**：现已实现为「① 仅充电中桩抽样、100 万帧铺满 90 天 5 分钟栅格」。② 全量 5 分钟方案需把 `telemetry_count` 提到 ~373–448 万。**定了才好冻结 config 与文案** | `config/part2_scml_full.yaml`、《04》§2.2 | #4 + #2 确认 | ☐ |
| ~~**T3**~~ | ~~**ODS 未按 `dt=YYYY-MM-DD` 分区**~~ 四张事实表已按 `dt=` 分区，维度快照保持扁平 | `generator.py` 写出逻辑 | #4 | ✅ 已修 |
| ~~**T4**~~ | ~~**注入日志漏记 3 处静默注入**~~ 三处已分别归入 Q1（telemetry）、Q8（chargers）、Q5（station_hourly）并全部记账 | `generator.py::_inject_quality_issues` | #4 | ✅ 已修 |
| ~~**T5**~~ | ~~**注入量只有每类 1 条**~~ 已按设计比例注入，正式规模共 32775 条；`QUALITY_RULES` 里声明每类的目标表，校验器强制覆盖 | 同上 | #4 | ✅ 已修 |
| **T6** | **DWS 四表 SparkSQL 未开始**：`dws_station_day` / `dws_charger_day` / `dws_user_day` / `dws_region_day` | `warehouse/sql/`、`warehouse/jobs/` | #4（`dws_region_day` 与 #5 分担） | ☐ |
| **T7** | **ADS 各表 SparkSQL + 导出 `handoff/ads/ads.db`**（Parquet 存 HDFS 为真源，导出 SQLite 供 Flask 只读） | 同上 | #4 | ☐ |

### P1 — 下游正等着

| ID | 待办 | 定位 | 归属 | 状态 |
|---|---|---|---|---|
| **T8** | **补齐 ADS schema 缺口**（见 §5 全表）：至少补 `chargerCount`、站点经纬度/单价/`forecastEnabled`、`onlineRate`、月度汇总、用户增长、`avgWaitMin`、`population`、`district` 利用率、快慢桩 mix、小时级 `observedAt+loadKw`、`serviceRadiusKm`、质量报告导入表 | `warehouse/sql/ads_schema.sql` | #4 | ☐ |
| **T9** | **写 `reconcile.py` 对账**：DWS 营收合计 == DWD 订单金额合计（误差 0）、ADS 可由 DWS/DWD 重算复现；失败必须非零退出 | 新增 `scripts/reconcile.py` | #4 | ☐ |
| **T10** | **填《指标抽验表》15 项真值**（`docs/management/part2-metric-checklist.md`，已由 #1 备好模板，签字栏含 #4 的 SQL 来源） | 与 #1/#3 联签 | #4 | ☐ |
| **T11** | **R1 闭环**：`district` 要么进 `database/schema.sql`（会动第一阶段冻结契约，需全组同意），要么在《04》字典显式标注「第二阶段扩展列」并与 #3 `dim_stations`、#5 统一引用；**否则 #4/#5 各写一套** | `database/schema.sql`、《04》附录 | #4 + #5 | ☐ |
| **T12** | **R2 闭环**：修《04》数据字典字段名——`users.phone → mobile`、`telemetry.energy_delta_kwh → energy_increment_kwh`（并补 `event_type`）、删掉 `orders.station_id`（改注「经 `chargers.station_id` 派生」） | 《04》附录数据字典 | #4 | ☐ |
| **T13** | **R6 闭环**：ADS 落地口径三处统一为「Parquet 存 HDFS 为真源 + 导出 SQLite `ads.db` 供 Flask 只读/答辩查数，JSON 可选」 | 《00》架构图、《02》§3.2、《04》§3.3 | #4 + #2 | ☐ |
| **T14** | **集成机演练**：`hdfs_put_ods.sh` → `/ev-charging/ods`、`run_dim_date.sh` → `/ev-charging/dwd/dim_date`，留下 YARN 记录 | 集成机 | #4 + #2 | ☐ |

### P2 — 文书与一致性

| ID | 待办 | 状态 |
|---|---|---|
| T15 | `README.md` / `README-SCML.md` 补：`files[]`、`contractVersion`、`kind` 说明，以及 `prl-test-fixture`（样例）与 `ods-handoff`（正式）**不得混用**的告警 | ☐ |
| T16 | 与 #3 对齐 `_row_id` 的语义边界：是否要求「同一批次内跨表唯一」还是「单表内唯一」（PRL 契约 §1 说的是**单表、同批次内唯一**） | ☐ |
| T17 | 把「`dim_date` 由 #4 生成、#3 校验」写进 `contracts/handoff_contract.md` 的显式条目（现仅在 README 里一句） | ☐ |

---

## 5. ADS 契约 ↔ 前端接口缺口表（#4 修 T8 时照这张表补）

前端契约已冻结在 `docs/design/part2-api-contract.md`（#1 已按它实现并接通 mock）。当前 `ads_schema.sql` 缺的字段如下：

| 前端接口 | 需要的字段 | 现 `ads_schema.sql` | 缺口 |
|---|---|---|---|
| `/api/overview/kpis` | `totalRevenueFen, totalEnergyKwh, totalOrders, chargerCount, idleCount, onlineRate, windowDays` | `ads_revenue_overview` 是**按 dt 一行**，只有 `revenue_fen/energy_kwh/order_cnt/active_user_cnt` | ❌ 无总量行、无桩数/在线率/窗口天数 |
| `/api/overview/stations` | `stationId,name,district,longitude,latitude,chargerCount,idleCount,utilizationRate,revenueFen,priceFenPerKwh,orderCount,forecastEnabled` | `ads_station_ranking` 有 `station_id,station_name,district,revenue_fen,energy_kwh,order_cnt,avg_utilization,idle_charger_cnt,rank_no` | ❌ 缺经纬度、单价、`forecastEnabled`、`chargerCount` |
| `/api/overview/charger-status` | 五状态计数（和 = 总桩数） | `ads_charger_health` 是按桩一行 | ⚠️ 可聚合，但要求**全部桩都在表内**（含 `idle`） |
| `/api/overview/load-24h` | `points[{stationId, observedAt, loadKw}]` | `ads_peak_hour` 只有 `hour`（整数），且语义是繁忙/利用率 | ❌ 缺 `observedAt` 与站点×小时 `loadKw` |
| `/api/overview/events` | `[{eventType,message,createdAt}]` 倒序 | 无（`ods_events` 在 ODS 层） | ❌ 需一层 ADS 事件流或直接读 DWD |
| `/api/quality/summary` | `{runId, tables[{name,rowsBefore,rowsAfter}], issues[{rule,type,injected,detected,handled,recall}]}` | 无 | ❌ 要承接 #3 的 `quality_report.json` 并导入 `ads.db` |
| `/api/enterprise/revenue-trend` | `points[{date,revenueFen,orderCount,energyKwh}]` | `ads_revenue_overview` 按 dt | ✅ 基本够，仅字段名映射（`dt→date`） |
| `/api/enterprise/station-ranking` | `+chargerCount` | 同 `ads_station_ranking` | ❌ 缺 `chargerCount` |
| `/api/enterprise/user-growth` | `points[{date,newUsers,activeUsers}]` | 无 | ❌ 缺 |
| `/api/enterprise/user-rfm` | `[{segment,userCount,revenueFen}]` | `ads_user_profile_rfm` 有 `user_cnt/avg_recency_days/avg_frequency/avg_monetary_fen` | ⚠️ 缺每分层 `revenueFen`（可由 `user_cnt × avg_monetary` 派生，需与 #1 冻结） |
| `/api/enterprise/monthly` | `[{month,revenueFen,energyKwh,orderCount,revenuePerChargerFen}]` | 无（只有日粒度） | ❌ 缺月度汇总 |
| `/api/user/price-compare` | `cityAvgFenPerKwh` + 各站单价 | 无单价列 | ❌ 缺 |
| `/api/user/price-distance` | `distanceKm`（以天安门为参考点） | 无经纬度 | ❌ 缺 |
| `/api/user/idle-ranking` | `idleCount,chargerCount,idleRate` | 有 `idle_charger_cnt` | ❌ 缺 `chargerCount` |
| `/api/user/peak-heatmap` | `hours[], names[], values[[h,s,busyCount]]` | `ads_peak_hour` 有 `hour/station_id/busy_count` | ✅ 基本够（需与站点名顺序对齐） |
| `/api/station/coverage` | `+经纬度、serviceRadiusKm` | 无 | ❌ 缺 |
| `/api/station/{id}/utilization` | 小时级 `observedAt + utilizationRate` | 无小时级利用率 | ❌ 缺 |
| `/api/station/{id}/mix` | `fastCount,slowCount,fastPowerKw,slowPowerKw,fastOrderShare` | 无 | ❌ 缺（需 `chargers.type/power_kw`） |
| `/api/station/{id}/health` | `faultRate,faultCount,topChargers[{code,chargeCount,totalDurationSec,faultFlag}]` | `ads_charger_health` 有 `charger_code,order_cnt,energy_kwh,charge_duration_sec,fault_cnt,status` | ⚠️ 缺 `chargeCount/totalDurationSec`（在 `chargers` 维度里） |
| `/api/gov/coverage` | `+population, chargersPer10k` | `ads_gov_service` 无 `population` | ❌ 缺（需外部/静态口径，要在文档里写明来源假设） |
| `/api/gov/service-stats` | `+avgWaitMin` | 无 | ❌ 缺（或明确声明不提供） |
| `/api/gov/carbon` | `totalEnergyKwh,co2SavedTon,factorTonPerMwh,factorNote,equivalentTrees` | 有 `carbon_reduction_kg`（按行政区） | ❌ 缺全市总量、换算系数与「等效种树」，且系数来源要在文档注明假设 |
| `/api/gov/peak-load` | `points[{hour,loadKw}]` | `ads_peak_hour.load_kw` | ✅ 可聚合 |
| `/api/gov/utilization` | `district,chargerCount,stationCount,utilizationRate` | `ads_gov_service` 无 `utilizationRate` | ❌ 缺 |

> **命名要一次冻结**：《指标抽验表》里写的 `ads_revenue_overview.total_revenue_fen`、`ads_charger_health.idle_cnt`、`ads_station_ranking.idle_cnt` 在当前 DDL 里**都不存在**。要么 #4 改 DDL 去匹配，#1 改抽验表，要么双方约定一层「ADS→API」映射并写进契约，别让两边各自理解。

---

## 6. 建议排期（对齐《00》总体排期，但按 #1 的 R9 判断已下调）

| 日 | #4 动作 | 交付/验收 |
|---|---|---|
| **D1** | ✅ 已完成：提交中断改动（§3）+ T1/T3/T4/T5 → 剩 **T2 定口径**（已给默认实现，等 #2 拍板）→ 冻结 `config` 与 ODS 字段 | 全量 ODS 生成 + `validate_handoff.py` 通过 + `hdfs_put_ods.sh` 入 HDFS（`kind: ods-handoff`） |
| **D2** | T6 DWS 四表 SparkSQL → T8 补 ADS DDL → T7 ADS 表 + 导出 `ads.db` | `/ev-charging/dws`、`/ev-charging/ads`、`handoff/ads/ads.db`，YARN 有记录 |
| **D3** | T9 `reconcile.py` 全绿 → T10 填 15 项抽验表 → T11–T13 文档闭环 → T14 集成机全链路演练 | 对账零误差 + 抽验表签字 + 发布清单/标签（`v2.0-ods` / `v2.1-dwd` / `v2.2-ads` / `v2.5-integrated` / `v2.final-demo`） |

---

## 7. 需要向别人确认的问题（建议今天就发群里）

1. **#2**：`feat/part2-02` 什么时候推？`scripts/part2/**` 归哪条分支？Flask 读 `ads.db` 的确切路径与表名以 `ads_schema.sql` 为准还是以 `part2-api-contract.md` 为准？
2. **#2/#3**：**T2 遥测口径**选哪个——「仅充电中桩 5 分钟、总量 100 万」还是「全量 5 分钟、目标 400 万」？（影响资源估算与文案）
3. **#3**：`_row_id` 唯一性范围（单表 vs 跨表）；ODS 是否必须 `dt=` 分区；`\N` 与空字符串的区分是否按 PRL 契约 §1 执行。
4. **#1**：ADS 字段命名（§5 的 `chargerCount` 等）与抽验表口径以哪边为准；`revenueFen` 是按分层派生还是单列存储。
5. **#5**：`dws_region_day` / `ads_gov_service` 的 `district`、`population`、`carbon factor` 口径谁定；`ads_forecast_result` 由 #5 写入 `ads.db` 还是单独交接。
6. **全组**：`stations.district` 是否允许加进第一阶段 `schema.sql`（会动冻结契约）。
