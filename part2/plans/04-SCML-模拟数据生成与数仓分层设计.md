# 04 · 模拟数据生成与数仓分层设计说明书（#4 SCML）

> 承担人：#4（SCML，配置管理负责人）
> 任务性质：**第二阶段主要任务（必做）**
> 管理职责：Git、配置项、版本、发布包
> 对应老师要求：（2）基于第一阶段表结构生成含质量问题的模拟数据；（5）SparkSQL 完成 ODS→DWD→DWS→ADS 分层分析
> 上游输入：第一阶段 `database/schema.sql`（表结构契约）
> 下游输出：ODS 原始数据（→ #3 检测清洗）、DWS/ADS 层（→ #2 API、#1 大屏、#5 预测）

---

## 1. 模块目标

1. **模拟数据生成器**：以第一阶段 SQLite 表结构为契约，生成大规模、确定性、含真实场景数据质量问题的模拟业务数据，并写入 HDFS 形成 ODS 层。
2. **数仓分层**：用 SparkSQL 完成 ODS→DWD→DWS→ADS 四层建模与加工，ADS 层面向用户/充电站/企业/政府四类视角输出应用指标。
3. 履行 SCML 职责：分支、标签、发布清单与配置项管理。

## 2. 模拟数据生成器设计（要求 2）

### 2.1 设计原则

- **契约复用**：表名、字段、枚举（桩状态 `idle/reserved/charging/fault/restarting`、订单状态 `reserved/charging/completed/cancelled`）、金额整数分、`+08:00` ISO 8601 时间，全部沿用 `database/schema.sql`。
- **确定性**：固定随机种子（如 `seed=20260914`），生成参数集中在 `config.yaml`；**可复现哈希**：对每张表的输出文件计算 **SHA-256**（粒度＝每表一个哈希），连同 `seed`、行数写入 `generation_manifest.json`，同种子重跑哈希必须一致。
- **真实分布**：订单在日内呈早晚双峰、周末/节假日差异；快充订单时长短电量高、慢充相反；新站利用率爬坡；温度按季节模拟。

> **与老师参考数据集的关系**：老师下发的 `04.数据集最终版`（郑州充电桩 会话/电池/站点 三个 CSV）与四层参考实现属于**教学范例与参考**；本项目按老师要求（2）**仍以第一阶段 `schema.sql` 为契约自生成数据**，表结构保持第一阶段口径不变。参考实现中已验证的真实数据坑（表头带单位、`soc` 类型误判、异常年份）已在本生成器的质量注入（§2.3）与《03》清洗规则中对应覆盖，确保我们的清洗能力可处理同类真实问题。

### 2.2 数据规模与结构

| 表 | 规模 | 关键设计 |
|---|---|---|
| `stations` | 8 | 分布于**北京市** 5 个行政区（朝阳/海淀/丰台/通州/大兴），含名称/地址/经纬度/单价（80–160 分/度） |
| `chargers` | ≈300 | 单站 24–48 桩；快充（60/120kW）约占 40%，慢充（7/30kW）约 60% |
| `users` | 5,000 | 11 位手机号、昵称、余额、注册时间（90 天内爬坡）、少量冻结 |
| `orders` | **≥120,000** | 90 天；状态以 completed 为主，含 cancelled 与异常进行中单 |
| `telemetry` | **≥1,000,000** | 充电中桩按 **5 分钟级**采样功率/电量增量（与 100 万条规模自洽；若改分钟级需提量至千万级） |
| `station_hourly_history` | 17,280 | 8 站 × 24h × 90 天，含温度/节假日/占用/负荷（ML 训练用） |
| `events` | ≥20,000 | 订单完成、故障、恢复等事件流 |

### 2.3 数据质量问题注入（10 类，约 3%–5%）

| # | 类型 | 注入实现 | 目标表 | 比例 |
|---|---|---|---|---|
| Q1 | 缺失值 | 随机将 completed 订单 `ended_at` 置 NULL；遥测 `power_kw` 置空 | orders、telemetry | 0.5% |
| Q2 | 重复记录 | 随机复制订单整行、遥测帧（模拟重传） | orders、telemetry | 1% |
| Q3 | 异常值 | `energy_kwh` 置负或 9999；`power_kw` 放大 10 倍 | orders、telemetry | 0.3% |
| Q4 | 时间格式混杂 | 部分时间写为 `yyyy/MM/dd HH:mm:ss` 或 Unix 时间戳 | orders、telemetry | 1% |
| Q5 | 逻辑矛盾 | 交换 `started_at/ended_at` 使其倒置；`busy_count > pile_count` | orders、station_hourly | 0.3% |
| Q6 | 金额口径错误 | 部分 `amount_fen` 按「元」写入（÷100）；部分与单价×电量不符 | orders | 0.5% |
| Q7 | 孤儿引用 | 订单引用不存在的 `charger_id`/`user_id` | orders | 0.3% |
| Q8 | 非法字段值 | 手机号 10 位/含字母；状态写 `unknown` | users、chargers | 0.3% |
| Q9 | 经纬度越界 | 个别站点坐标移出**北京市**范围 | stations | 1–2 条 |
| Q10 | 文本脏数据 | 站名/地址加前后空格、全角字符、乱码 | stations、users | 0.5% |

**注入日志**：`injection_log.json` 记录每类问题的注入条数、命中的主键清单与种子，供 #3 检出对账——这是质量检测可验证的关键设计。

> **表名口径统一**：第一阶段 `station_hourly_history` → ODS 层 `ods_station_hourly` → DWD 层 `dwd_station_hourly`，下游一律使用后两者。

### 2.4 输出与入 HDFS

- 输出 CSV（业务表）+ JSON（事件流），按 `dt=yyyy-MM-dd` 分区目录组织。
- `hdfs dfs -put` 至 `/ev-charging/ods/`，并生成 `_SUCCESS` 标记与行数清单。
- **交接**：本地生成结果与 `injection_log.json` 一并打包为 `handoff/ods/`（含 `manifest.json`：行数 + `sha256` + `seed`），经共享文件夹/网盘交付 #3；DWS/ADS 另产出 `handoff/dws`、`handoff/ads`（含 **SQLite 单文件 `ads.db`**）交付 #2/#1（见《02》§2.4）。

## 3. 数仓分层设计（要求 5）

### 3.1 分层与加工方式

| 层 | 表 | 加工 | 责任边界 |
|---|---|---|---|
| ODS | `ods_users`、`ods_stations`、`ods_chargers`、`ods_orders`、`ods_telemetry`、`ods_station_hourly`、`ods_events` | 原样落地，**保留脏数据** | 本模块 |
| DWD | `dwd_order_detail`、`dwd_telemetry_detail`、`dwd_station_hourly`、`dim_users`、`dim_stations`、`dim_chargers`、`dim_date` | 清洗与维度拉宽由 #3 产出；本模块仅负责 `dim_date` 生成并校验 | **#3 主责**；#4 仅 `dim_date` |
| DWS | `dws_station_day`、`dws_charger_day`、`dws_user_day`、`dws_region_day` | SparkSQL 按 站点/桩/用户/区域 × 日 聚合 | 本模块（`dws_region_day` 由 **#5 分担**） |
| ADS | 见 3.3 | SparkSQL 汇总 + 窗口函数 | 本模块 |

### 3.2 DWS 汇总宽表（核心字段）

- `dws_station_day`：`dt, station_id, order_cnt, energy_kwh, revenue_fen, avg_utilization, peak_hour, fast_order_cnt, slow_order_cnt, fault_cnt`
- `dws_charger_day`：`dt, charger_id, order_cnt, energy_kwh, charge_duration_sec, fault_flag`
- `dws_user_day`：`dt, user_id, order_cnt, energy_kwh, amount_fen, active_flag`
- `dws_region_day`：`dt, district, station_cnt, charger_cnt, order_cnt, energy_kwh, revenue_fen, served_user_cnt`

### 3.3 ADS 应用指标表（面向四类视角）

| 表 | 面向 | 内容 |
|---|---|---|
| `ads_revenue_overview` | 企业/主页 | 总营收、总电量、总订单、日趋势、月度汇总 |
| `ads_station_ranking` | 企业/用户 | 站点营收/利用率/空闲排行、价格对比 |
| `ads_user_profile_rfm` | 企业 | 用户 RFM 分层、增长与活跃 |
| `ads_peak_hour` | 用户/政府 | 站点 × 24h 繁忙热力、全城高峰负荷 |
| `ads_charger_health` | 充电站 | 桩故障率、累计次数/时长、状态分布 |
| `ads_gov_service` | 政府 | 区域覆盖密度、服务指标、等效碳减排（电量 × 公开换算系数，注明来源假设）；**由 #5 分担** |
| `ads_forecast_result` | 预测（选做，#5 写入） | 未来 24h 各站负荷/空闲/拥堵/高峰 |

> **落地方式**：ADS 各表在 HDFS 以 Parquet 物化后，导出为 **SQLite 单文件 `ads.db`** 交付 #2（Flask 用 Python 标准库 `sqlite3` 只读访问）。**选 SQLite 而非 MySQL 的理由**：① 与第一阶段数据库端同为 SQLite，口径与工具链延续；② 零额外依赖——无外网虚拟机也能跑，不需要 MySQL 服务与 JDBC 驱动；③ 单文件便于经 `handoff/ads` 交接，答辩现场可用 DB Browser 直接查数复核。

### 3.4 对账约束（验收硬指标）

- DWS 营收合计 = DWD 订单金额合计（误差 0）。
- ADS 各指标可由 DWS/DWD 重算复现；抽验脚本 `reconcile.py` 自动比对。
- 口径（在线率、利用率、金额分、ISO 时间）与第一阶段定义一致，文档化冻结。

## 4. 配置管理职责（SCML）

- 分支：`feat/part2-data`（生成器+数仓），评审后由 #2 合入 `dev`。
- 标签：`v2.0-ods`、`v2.1-dwd`、`v2.2-ads`、`v2.5-integrated`、`v2.final-demo`。
- 发布清单：生成器与配置、数仓 SQL/作业、注入日志、对账脚本、环境部署手册（#2 产出，本模块归档）。
- 不提交：HDFS 数据、本地临时文件、虚拟机镜像。

## 5. 排期（3 天，对应总体排期 D1–D3）

| 日 | 任务 |
|---|---|
| D1 | 生成器 + `config.yaml`；维度表 + 订单/遥测/小时历史全量生成（含 10 类注入）；入 HDFS 形成 ODS，产出 `handoff/ods` |
| D2 | DWS 四张汇总表 + ADS 指标表完成，与 #2 冻结表结构；产出 `handoff/dws`、`handoff/ads` |
| D3 | 对账脚本全绿；配合 #5 预测接入；联调修复；晚间标签、发布清单、彩排 |

## 6. 验收标准

1. 订单 ≥ 10 万、遥测 ≥ 100 万、站数 ≤ 10；同种子重复生成哈希一致。
2. 注入日志覆盖 10 类问题，与 #3 检出报告对账一致。
3. ODS→DWD→DWS→ADS 逐层可对账，`reconcile.py` 全绿。
4. 全部分层加工以 SparkSQL 在**本机伪分布式 Hadoop** 环境执行，可出示 YARN 记录；DWS/ADS 经 `handoff/dws`、`handoff/ads` 交接交付。

---

## 附：核心表数据字典（字段级）

> 完整字段以第一阶段 `database/schema.sql` 为准；下表列第二阶段关心的关键字段与口径。

**`stations`（维度）**

| 字段 | 类型 | 口径/说明 |
|---|---|---|
| `station_id` | int | 主键，1–8 |
| `name` / `address` | string | 站名/地址；可能含前后空格/全角字符（Q10） |
| `district` | string | 北京市行政区（朝阳/海淀/丰台/通州/大兴） |
| `latitude` / `longitude` | double | 北京市范围内（纬度 39.4–41.1、经度 115.4–117.5） |
| `price_fen_per_kwh` | int | 单价，整数分/度（80–160） |

**`chargers`（维度）**

| 字段 | 类型 | 口径/说明 |
|---|---|---|
| `charger_id` | int | 主键 |
| `station_id` | int | 外键 → `stations` |
| `rated_power_kw` | int | 额定功率 7/30/60/120 |
| `status` | string | `idle/reserved/charging/fault/restarting` |

**`users`（维度）**

| 字段 | 类型 | 口径/说明 |
|---|---|---|
| `user_id` | int | 主键 |
| `phone` | string | 11 位手机号（脏数据可能 10 位或含字母，Q8） |
| `nickname` | string | 昵称，可缺失 → 清洗时填默认值 |
| `balance_fen` | int | 余额，整数分 |
| `registered_at` | string | ISO 8601 `+08:00` |
| `status` | string | `active/frozen` |

**`orders`（事实）**

| 字段 | 类型 | 口径/说明 |
|---|---|---|
| `order_id` | int | 主键 |
| `user_id` / `charger_id` / `station_id` | int | 外键（可能孤儿引用，Q7） |
| `status` | string | `reserved/charging/completed/cancelled` |
| `started_at` / `ended_at` | string | ISO 8601 `+08:00`（可能倒置 Q5 或缺失 Q1） |
| `energy_kwh` | double | ≥ 0（可能为负/9999，Q3） |
| `amount_fen` | int | 金额，整数分（可能存在「元」混入，Q6） |

**`telemetry`（事实）**

| 字段 | 类型 | 口径/说明 |
|---|---|---|
| `charger_id` | int | 与 `recorded_at` 组成业务主键 |
| `recorded_at` | string | ISO 8601 `+08:00`（可能重复或格式混杂） |
| `power_kw` | double | 采样功率，≤ 额定功率（可能超限 Q3 / 缺失 Q1） |
| `energy_delta_kwh` | double | 采样区间电量增量，≥ 0 |

**`station_hourly`（事实，ML 训练用）**

| 字段 | 类型 | 口径/说明 |
|---|---|---|
| `station_id` + `observed_at` | int + string | 业务主键（站点 × 小时） |
| `pile_count` | int | 该站总桩数 |
| `busy_count` | int | 占用桩数，≤ `pile_count`（可能违反 Q5） |
| `temperature_c` | double | 模拟温度 |
| `is_holiday` | boolean | 是否节假日 |
| `load_kw` | double | 站点小时负荷（预测目标） |
