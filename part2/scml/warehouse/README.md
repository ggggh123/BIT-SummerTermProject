# SCML Warehouse：DWS / ADS 作业与交接包

> 承担人 #4。对应《04-SCML》§3「数仓分层设计」、§3.4「对账约束」、§6「验收标准」。

这个目录是 **ODS 之后的全部加工**：DWS 四张汇总宽表 → ADS 指标表 → 导出 SQLite 单文件
`handoff/ads/ads.db`，外加三层对账。

---

## 1. 目录结构

```text
warehouse/
├── sql/
│   ├── dws_schema.sql     DWS 四表 DDL（Hive/SparkSQL 方言）
│   ├── dws_etl.sql        DWS 四表 ETL（SparkSQL）
│   ├── ads_etl.sql        ADS 7 张业务指标表的 DDL + ETL（SparkSQL）
│   └── ads_schema.sql     ADS **SQLite** 导出契约（Flask 只读的那一份）
├── jobs/
│   ├── _lib.py            共用工具：ODS 读取、PRL 清洗规则、表结构装载
│   ├── _ads_extras.py     8 张「不由 Spark 产出」的 ADS 表 + ads_meta 装配
│   ├── build_local.py     ★ 无 Spark 环境的同构物化：ODS → DWS + ads.db
│   ├── build_dws.py       spark-submit：dws_schema.sql + dws_etl.sql
│   ├── build_ads.py       spark-submit：ads_etl.sql
│   ├── build_dim_date.py  DWD 的 dim_date（原本就有）
│   ├── export_dws_csv.py  DWS Parquet → handoff/dws CSV 交接包
│   ├── export_ads_db.py   ADS Parquet + Python 旁路表 → handoff/ads/ads.db
│   └── reconcile.py       ★ 三层对账（验收硬指标）
└── README.md
```

---

## 2. 两条产出路径（同一套口径，两个实现）

| | 路径 A：本地（无 Spark） | 路径 B：虚拟机（伪分布式 Hadoop） |
|---|---|---|
| 入口 | `python warehouse/jobs/build_local.py` | `bash scripts/run_dws_ads.sh` |
| 输入 | `handoff/ods`（CSV/JSONL） | HDFS `/ev-charging/ods`、`/ev-charging/dwd` |
| 实现 | 纯标准库 Python（`build_local.py` + `_lib.py`） | SparkSQL（`dws_etl.sql` + `ads_etl.sql`） |
| 产出 | `handoff/dws/`、`handoff/ads/ads.db` | HDFS `/ev-charging/{dws,ads}`，再导出同样的两个本地交接包 |
| 用途 | 前端联调、答辩查数、CI 自测、**DWD 未交付前的过渡** | 要求（5）的验收点，可出示 YARN 记录 |

**为什么要有两条**：Spark 只在虚拟机里，而前端联调、答辩现场查数、CI 都发生在没有
Hadoop 的机器上。两条路的交接包格式**完全一致**（同一个 `write_dws` 写盘），
所以下游（#5 的预测输入、#2 的复核）不需要知道数据是谁产的。

`reconcile.py` 在两者同时存在时会做交叉比对 —— 不是「随便挑一个信」。

---

## 3. 职责边界：为什么 ADS 只有 7 张表走 Spark

`ads_etl.sql` 只产出 7 张业务指标表。另外 8 张由 Python 侧产出，理由如下：

| ADS 表 | 由谁产出 | 为什么 |
|---|---|---|
| `ads_station` `ads_charger` `ads_daily` `ads_station_day` `ads_station_hourly` `ads_user_rfm` `ads_district` | **SparkSQL** | 纯 SQL 聚合/窗口函数，正是要求（5）的验收点 |
| `ads_meta` | Python | 要拼 ODS manifest 的 runId/seed 与一串口径说明文本，是元数据装配 |
| `ads_quality_table` `ads_quality_issue` `ads_quality_meta` | Python | R01–R10 的检出逻辑（金额单位识别、坐标范围、手机号正则、重复行判定）与 #3 的 DWD 清洗**共用一套规则**；再写一份 SQL 必然两处漂移，反而失去「独立复算」的对账意义 |
| `ads_forecast_batch` `ads_forecast_24h` `ads_forecast_metric` | Python | 默认产出 seasonal-naive 降级基线（《05-PE》§8）；传 `--forecast-handoff` 后读取 #5 交接包并整表替换 |
| `ads_event` | Python | 事件流文案要 JOIN 站点名与订单金额，属展示层组装 |

规则只在 `warehouse/jobs/_lib.py` 一处实现。`tests/test_ads_schema_contract.py` 会
断言「两张表清单的并集 == 契约里的 15 张」，漏一边就红。

---

## 4. 口径单点化（改之前先读这段）

`ads_schema.sql` 是 **ADS 表结构与列顺序的唯一真源**（Hive 侧由
`test_ads_schema_contract.py` 比对列集）。除此之外还有几条「只能有一处」的口径：

| 口径 | 唯一出处 | 为什么不能有两处 |
|---|---|---|
| ADS 表结构 / 列顺序 | `sql/ads_schema.sql` | `build_local.py` 用 `PRAGMA table_info` 取列序，不手抄；Parquet 侧靠测试比对 |
| 电量汇总 = 逐日 round(3) → 求和 → round(3) | `build_local.py:build_ads_business` / `ads_etl.sql:ads_daily` | 两侧舍入路径必须一致，否则 `reconcile.py` 的 B 组会差最后一个小数位 |
| 峰值时段并列取**更早**的整点 | `dws_etl.sql`（`ORDER BY busy_count DESC, hour ASC`）与 `build_local.py:peak_hour` | 并列是**必然**会发生的（occupancy 会被夹到上限），不约定顺序就逐行对不上 |
| RFM 分层顺序与打分 | `_lib.RFM_SEGMENTS` / `_ads_extras._ntile_buckets` / `ads_etl.sql` 的 `NTILE(5) ... , user_id` | `NTILE` 复刻了 Spark 的分桶边界，且必须带 `user_id` 次级键；否则整数天的 recency 一并列，两侧分层人数就不同 |
| 碳因子 0.581 tCO₂/MWh、等效树 18 kg/棵·年 | `_lib.py` 常量 + `ads_meta.carbonFactorNote` | 前端会自校验 `co2SavedTon = 电量/1000 × factor` |
| 行政区人口 | `_lib.DISTRICT_POPULATION` + `ads_meta.populationSource` | 属外部参考数据，必须注明来源 |

---

## 5. 怎么跑

### 5.1 本地（不需要 Hadoop，约 15 秒）

```bash
cd part2/scml
bash scripts/run_scml_full.sh        # 1) 生成全量 ODS → handoff/ods（约 15s）
python3 warehouse/jobs/build_local.py \
    --ods handoff/ods --dws handoff/dws --ads handoff/ads \
    --generated-at 2026-09-14T14:30:00+08:00      # 2) 物化 DWS + ADS（约 15s）
python3 warehouse/jobs/reconcile.py \
    --ods handoff/ods --dws handoff/dws --ads handoff/ads/ads.db
```

`--generated-at` 固定后重跑，`ads.db` 内容逐字节一致（可复现构建）。

### 5.2 虚拟机（伪分布式 Hadoop + Spark，出 YARN 记录）

```bash
source /etc/profile.d/ev-second-project.sh
cd <repo>/part2/scml
bash scripts/hdfs_put_ods.sh                  # ODS → HDFS /ev-charging/ods
bash scripts/run_dws_ads.sh                   # DWS + ADS + 导出 + 对账
bash scripts/run_dws_ads.sh --dry-run         # 只打印要执行的 SQL，不连 Spark
```

`run_dws_ads.sh` 在第 0 步会**硬性检查** `${HDFS_ROOT}/dwd` 是否存在，缺 DWD 直接失败 ——
不允许「静默出一份空表」当交付物。

### 5.2.1 已经在真实集群上验证过（2026-09-14）

DWD 还没交付，但 SQL 本身**已经在 Hadoop 3.4.1 + Spark 上跑通了**。做法是先用
`dwd_contract.sql` 建出**空的** DWD 表，再跑 DWS/ADS —— 空表照样会走完整的
analyze + plan + execute，任何列名/类型/函数错误都会立刻以
`AnalysisException: UNRESOLVED_COLUMN` 抛出。

```bash
# 在客户机里（Hadoop/Spark 已起）
spark-sql -f warehouse/sql/dwd_contract.sql     # 建空 DWD，验证契约 DDL
python3 warehouse/jobs/build_dws.py --sql-dir warehouse/sql
python3 warehouse/jobs/build_ads.py --sql-dir warehouse/sql
```

实测结果：`DWS_EXIT=0` / `ADS_EXIT=0`，全部 11 + 15 条语句通过，
`/ev-charging/dws/*` 与 `/ev-charging/ads/*` 都写出了 `_SUCCESS`；
`ads_user_rfm` 返回 8 行（固定八分层的 `segments` CTE 驱动，空输入下补 0），
其余表 0 行 —— 与「DWD 为空」完全自洽。

**这一步抓到了一个本地测试抓不到的 bug**：`dws_etl.sql` 里 `flt` CTE 把
`entity_id` 起别名为 `charger_id` 之后，`flt_station` 仍在引用 `e.entity_id`，
报 `UNRESOLVED_COLUMN`。纯 Python 路径（`build_local.py`）走自己的代码，
不会发现这类 SQL 方言问题 —— 所以**改完 SQL 一定要在客户机上跑一遍**。

> 结论：`dws_etl.sql` / `ads_etl.sql` 的**语法与列引用已验收**；
> 尚未验收的是**数值层面对账**（D 组），那要等 #3 的真实 DWD。

### 5.3 数据窗口

`config/part2_scml_full.yaml` 的 `start_date: "2026-06-17"` 是**冻结决策**：
窗口 = `[start_date, +90 天)` = 2026-06-17 → 2026-09-14，**末日正好是演示日**。
早期草稿的 `2026-09-01` 会把 90 天伸到 2026-11-29（未来），前端所有日期轴
与「至今」口径都会显得荒谬。

---

## 6. 三层对账：`reconcile.py`

《04-SCML》§3.4 的验收硬指标。四组断言：

| 组 | 内容 | 依赖 |
|---|---|---|
| **A 口径自洽** | KPI = 逐日汇总、五状态之和 = 桩数、快+慢 = 桩数、RFM 分层用户数之和 = 有订单用户数、碳减排 = 电量×因子、小时表行数 = 站数×24×天数…（18 项） | 只依赖 ADS |
| **B 层间一致** | DWS↔ADS：站点日表逐行比对、行政区/用户日/桩日各口径合计相等（7 项） | DWS + ADS |
| **C 独立重算** | 从 ODS 用同一套 PRL 规则**重新跑一遍清洗聚合**，比对 `ads_meta` 的总营收/总订单/总电量（4 项） | ODS + ADS |
| **D 上游一致** | `SUM(dwd_order_detail.amount_fen)` = `SUM(dws_station_day.revenue_fen)`，误差 **0** | 需 #3 的 `handoff/dwd`，缺则 SKIP（加 `--require-dwd` 则 FAIL） |

A/B/C 三组**任何人的交付都不依赖**：只要 ODS 在就能跑。这是刻意的 ——
不能让「等 DWD」变成「链路没法验收」。

退出码：`0` = 全绿；`1` = 有 FAIL。CI 与演示脚本据此判成败，**不要只看输出文本**。

实测（全量 8 站 / 12 万单 / 100 万遥测）：**30 项，通过 29，跳过 1（D 组待 DWD）**。

---

## 7. 交接包格式

### `handoff/dws/`（→ #5 预测输入 / #2 复核）

```text
dws_station_day/part-00000.csv  + _SUCCESS
dws_charger_day/part-00000.csv  + _SUCCESS
dws_user_day/part-00000.csv     + _SUCCESS
dws_region_day/part-00000.csv   + _SUCCESS
manifest.json   kind=dws-handoff，含行数与 SHA-256
```

### `handoff/ads/`（→ #2 Flask / #1 大屏）

```text
ads.db              SQLite 单文件，15 张表 + 7 个设计文档兼容视图
ads_manifest.json   表行数、数据窗口、质量统计、预测来源
```

`ads.db` 的列级契约见 `sql/ads_schema.sql`；Flask 侧唯一入口是
`server/services/ads_reader.py`。

---

## 8. 已知限制（如实标注，不藏）

1. **DWD 尚未交付**：`dws_etl.sql` / `ads_etl.sql` 读的是 `dwd_*` / `dim_*` 表，
   当前只有 `dim_date` 就绪。所以本目录的 D 组对账是 SKIP 状态，虚拟机上跑
   `run_dws_ads.sh` 会在第 0 步停住。**本地路径（`build_local.py`）不受影响**，
   它直接从 ODS 按同一套 PRL 规则清洗，属于过渡口径。
2. **ADS 的 3 张质量表是 ADS 侧独立复算**，不是 #3 的清洗报告。等
   `handoff/dwd/cleaning_report.json` 到位后以其为准。
3. **预测支持两种来源**：默认是 `seasonal-naive` 降级基线，
   `ads_forecast_batch.is_baseline = 1`，响应里带 `note`。若 #5 已交付
   `handoff/forecast`，执行 `build_local.py` 或 `export_ads_db.py` 时传
   `--forecast-handoff <path>`，会读取真实 MLlib 批次并整表替换三张预测表，
   此时 `is_baseline = 0`。
4. **行政区人口**是外部参考数据（七普常住人口），来源写在 `ads_meta.populationSource`。
5. **服务半径**是运营规划参数（按站点订单需求规模折算），**非实测**，
   见 `ads_meta.serviceRadiusNote`。
6. **平均等待恒为 0 —— 是数据特征，不是 SQL 缺陷（2026-09-15 重跑后的最终结论）**：
   ① **曾有一处技术缺陷**：DWD 的时间列是 STRING（`2026-06-17T12:34:56+08:00`），Spark 3.5.7 上直接
   `UNIX_TIMESTAMP(字符串)` 返回 `null`，相减与 `AVG` 静默变成 0；已于 2026-09-15 在
   `sql/ads_etl.sql` 修为显式 `UNIX_TIMESTAMP(CAST(... AS TIMESTAMP))`（详见 `ads_meta.avgWaitNote`）。
   ② **修完 SQL 后该指标仍是 0，因为数据里本来就没有等待时段**：生成器对 `reserved_at` 与 `started_at`
   写入的是**同一个时间戳**（`data_generator/generator.py:486-487` 同用 `started` 变量）。重跑批次实测
   **110,696 / 110,696 条已完成订单的 `started_at == reserved_at`**，故 `AVG` 恒为 0；
   另外 `ads_etl.sql` 的 `WHERE UNIX_TIMESTAMP(started) > UNIX_TIMESTAMP(reserved)` 会把等值行全部过滤。
   ③ 要让该指标非 0，需**改生成器**（让 `reserved_at` 早于 `started_at`）并重跑——属后续批次议题，本轮不做。
   **答辩口径**：以「生成器未建模『预约→开工』等待时段」解释，不要再表述为 SQL bug。
7. **`fault_cnt` 有两个口径**：`dws_station_day.fault_cnt` 是「当日发生故障上报的去重桩数」
   （取自事件流 `charger_fault`）；`ads_station.fault_cnt` 是「当期处于 fault 的桩数」快照。
   两者不同，见 `ads_meta.faultNote`。
8. **舍入差异**：Spark 的 `ROUND` 是 HALF_UP，Python 的 `round()` 是银行家舍入。
   金额与行数类断言用零容差；电量/百分比类用 `KWH_TOL=0.05` / `RATE_TOL=0.05`。

---

## 9. 自测

```bash
python3 -m unittest discover -s tests -v
```

| 测试 | 守什么 |
|---|---|
| `test_generator_contract.py` | ODS 交接包契约、13 项注入覆盖、遥测时间轴、**需求分布形状**（早晚双峰 / 站点规模 / 新站爬坡 / 周末压平） |
| `test_dws_schema_contract.py` | DWS 四表列集 == 设计文档 §3.2、金额 BIGINT、显式 HDFS LOCATION、ETL 不越级读 ODS |
| `test_ads_schema_contract.py` | ADS 15 表 + 7 视图、**SQLite 列集 == Hive 列集**、每张表都有产出方 |
| `test_dws_ads_pipeline.py` | 端到端：小样例 ODS → `build_local.py` → `reconcile.py` 全绿 + 15 项口径断言 |
