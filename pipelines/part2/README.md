# 第二阶段数据管道（1/10 规模的参考骨架）

> **权威口径变更（2026-09-14 16:20）**：ADS 表结构以 **#4 的 `part2/scml/warehouse/sql/ads_schema.sql`**（分支 `feat/part2_SCML`）为准。
> 本目录的 `gen_ods.py`（生成器）与 `build_warehouse.py` 的 ADS 部分**标记为 deprecated**，
> 仅保留可跑通的链路参考；对外产出统一走 `align_to_scml_ads.py`（按他的字段名与粒度重写 ADS）。
> 本目录保留且不重复的价值：**逐层对账 `reconcile.py`、接口契约导出 `export_api.py`**、以及 `scripts/part2/` 的环境三件套。

### 待交接：ODS 入口切换到 #4 的生成器

他的入口（已在分支 `feat/part2_SCML`）：

```bash
python3 part2/scml/data_generator/generator.py --config part2/scml/config/part2_scml_sample.yaml --out handoff/ods
python3 part2/scml/scripts/validate_handoff.py handoff/ods
```

**切换前置条件**（否则下游读不到数据）：他的 ODS 是「每表一个目录 + `_SUCCESS` + sha256 `manifest.json`，
表名带 `ods_` 前缀（`ods_orders` 等）」，而本目录的 `quality_check.py`、`clean_to_dwd.py` 目前按 `<table>.csv` 单文件读取。
切换入口时，这两步的读取路径与表名需要 #3/#4 一并改（属他们的模块范围），本脚本只在检测到他的生成器时提示，不擅自改写。

> 提出人：#1（PM）｜用途：**在不依赖任何人的前提下先把「生成 → 质量 → 清洗 → 分层 → ADS」整条链路跑通**，
> 作为 #3（质量/清洗）、#4（生成器/数仓）、#5（预测）的起点骨架；他们接手后按《03》《04》替换实现即可。
> 口径以第一阶段 `database/schema.sql` 为唯一真源（金额整数分、时间 `+08:00` ISO 8601）。

## 为什么先做这个

`00` §8.2 的 D1 硬闸门要求「环境打通 + 数据入 HDFS + 质量作业就绪」，而现实是环境才刚打通一台。
这份骨架用 1/10 规模把链路先贯通，D2 放量只需改 `gen_ods.py` 的 `SCALE` 常量。

## 文件

| 文件 | 作用 | 对应老师步骤 |
|---|---|---|
| `gen_ods.py` | 确定性生成模拟数据（注入 10 类质量问题），输出 `handoff/ods/` + `injection_log.json` + `manifest.json` | 第 2 步 |
| `quality_check.py` | PySpark 统计画像 + 10 条规则检测，输出 `quality_report.json`（含注入/检出对账） | 第 3 步 |
| `clean_to_dwd.py` | PySpark 去重/补缺/剔除/标准化，写 HDFS `/ev-charging/dwd`（Parquet） | 第 4 步 |
| `build_warehouse.py` | SparkSQL 分层 ODS→DWD→DWS→ADS，导出 ADS 到 SQLite `ads.db` | 第 6 步 |
| `reconcile.py` | 逐层对账：DWS/ADS 合计 = DWD 明细合计（《04》§3.4 误差 0），失败返回非零码 | 验收 |
| `run_all.sh` | 一键串联（HDFS 就绪前提下） | — |

## 用法

```bash
cd /mnt/hgfs/BIT-SummerTermProject/pipelines/part2
python3 gen_ods.py      # 生成 ODS（纯本地，秒级）
./run_all.sh            # 质量 → 清洗 → 分层 → ADS（Spark on YARN）
```

## 已知扩展（与《04》数据字典的差异，实现时以此为准）

1. `stations` 表在第一阶段 `schema.sql` 中**没有 `district`（行政区）列**，但区域/政府视角必需，生成器与分层显式新增该列，声明为「第二阶段扩展列」。
2. 字段名以 `schema.sql` 为准：`users.mobile`（不是 phone）、`telemetry.energy_increment_kwh`（不是 energy_delta_kwh）、`orders` **无** `station_id`（经 `chargers.station_id` 关联）。
3. `orders` 上有两个部分唯一索引（每用户/每桩最多一条 active 单），生成器已遵守。
4. 遥测采样口径：本骨架按「充电中桩 5 分钟级」写入，规模随 `SCALE` 线性变化。
