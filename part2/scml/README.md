# #4 SCML：模拟数据生成 + 数仓分层（ODS → DWD → DWS → ADS）

对应《04-SCML-模拟数据生成与数仓分层设计说明书》。两条主线：

1. **模拟数据生成器**（要求 2）：以第一阶段 `database/schema.sql` 为契约，生成确定性、
   含 10 类真实质量问题的大规模业务数据，按 `dt=YYYY-MM-DD` 分区落地为 ODS 交接包 → #3。
2. **数仓分层**（要求 5）：SparkSQL 完成 DWD → DWS → ADS 加工，ADS 导出 SQLite 单文件
   `handoff/ads/ads.db` → #2 Flask / #1 大屏；DWS 交接包 → #5 预测输入。

---

## 目录

```text
part2/scml/
├── data_generator/generator.py   确定性 ODS 生成器 + 10 类质量注入
├── config/
│   ├── part2_scml_full.yaml      正式规模（含冻结决策与理由）
│   └── part2_scml_sample.yaml    小样例（自测用）
├── scripts/
│   ├── run_scml_full.sh          全量生成 + 校验
│   ├── run_scml_sample.sh        小样例生成 + 校验
│   ├── validate_handoff.py       行数 / SHA-256 / 分区 / 注入日志校验
│   ├── check_scml_delivery.py    ODS/DWS/ADS/DWD 状态一键体检
│   ├── hdfs_put_ods.sh           ODS → HDFS
│   ├── run_dim_date.sh           DWD 的 dim_date（Spark on YARN）
│   └── run_dws_ads.sh            ★ DWS + ADS + 导出 + 对账（虚拟机一键）
├── warehouse/                    ★ DWS/ADS 全部加工与导出，见 warehouse/README.md
├── contracts/handoff_contract.md ODS/DWD/DWS/ADS 的接口契约（含 DWD 字段清单）
├── tests/                        40 项单测
└── handoff/                      交接包落盘位置（被 .gitignore 忽略）
```

---

## 快速开始

### 只跑 ODS（→ #3）

```bash
cd part2/scml
bash scripts/run_scml_full.sh          # 约 15s，产出 handoff/ods
bash scripts/hdfs_put_ods.sh           # 可选：入 HDFS
```

期望输出：

```text
[OK] handoff\ods manifest, hashes, row counts, dt partitions and injection log are valid
```

正式规模（`config/part2_scml_full.yaml`）：8 站 / 288 桩 / 5000 用户 / 12 万订单 /
100 万遥测 / 17,280 条站点小时 / 2 万事件，窗口 **2026-06-17 → 2026-09-14**。

### 跑完整条链路（→ #2 / #1 / #5）

**本地（不需要 Hadoop，约 15 秒）**

```bash
python3 warehouse/jobs/build_local.py --ods handoff/ods --dws handoff/dws --ads handoff/ads
python3 warehouse/jobs/reconcile.py   --ods handoff/ods --dws handoff/dws --ads handoff/ads/ads.db
# → 30 项，通过 29，跳过 1（D 组待 #3 的 DWD）  [OK] 对账全绿
python3 scripts/check_scml_delivery.py --ods handoff/ods --dws handoff/dws --ads handoff/ads
# → SCML delivery: ready
```

**虚拟机（伪分布式 Hadoop + Spark，出 YARN 记录）**

```bash
source /etc/profile.d/ev-second-project.sh
bash scripts/hdfs_put_ods.sh
bash scripts/run_dws_ads.sh           # 或先 --dry-run 看要执行的 SQL
```

细节、口径单点化清单、已知限制，全部在 **`warehouse/README.md`**。

### 自测

```bash
python3 -m unittest discover -s tests -v      # 40 项
```

---

## 交接给谁、给什么

| 下游 | 产物 | 路径 | 说明 |
|---|---|---|---|
| #3 PRL | ODS | `handoff/ods` | 含 10 类质量注入 + 逐条 `injection_log.json` 供检出对账 |
| #5 PE | DWS | `handoff/dws` | `dws_station_day` / `dws_charger_day` / `dws_user_day` / `dws_region_day` |
| #2 TL | ADS | `handoff/ads/ads.db` | SQLite 单文件，15 张表 + 7 个设计文档兼容视图，标准库只读 |
| #1 PM | ADS | 同上 | 经 Flask 间接读取 |
| 全员 | 接口契约 | `contracts/handoff_contract.md` | 含 **DWD 字段清单**（#4 冻结，#3 按它产出） |

产物一律**不进 git**（`handoff/**` 已忽略），走共享文件夹 / 网盘交接。

---

## 数据质量注入

10 类问题按设计文档的比例注入，**每个被改动的行都记录在 `injection_log.json`**
（`{rule, table, row_id, business_key, description}`）。一条规则可以命中多张表：

| Rule | Tables | Rate |
|---|---|---|
| Q1 缺失值 | `ods_orders`, `ods_telemetry` | 0.5% |
| Q2 重复记录 | `ods_orders`, `ods_telemetry` | 0.5% |
| Q3 异常值 | `ods_orders`, `ods_telemetry` | 0.3% |
| Q4 时间格式混杂 | `ods_orders`, `ods_telemetry` | 1% |
| Q5 逻辑矛盾 | `ods_orders`, `ods_station_hourly` | 0.3% |
| Q6 金额口径错误 | `ods_orders` | 0.5% |
| Q7 孤儿引用 | `ods_orders` | 0.3% |
| Q8 非法字段值 | `ods_users`, `ods_chargers` | 0.3% |
| Q9 经纬度越界 | `ods_stations` | 1–2 行 |
| Q10 文本脏数据 | `ods_stations`, `ods_users` | 0.5% |

正式规模实测注入 **32,775 条**。`summary` 汇总块供 `/api/quality/summary` 直接取用。

---

## 遥测时间轴与规模口径（原 T2，已冻结）

遥测帧按 **5 分钟栅格** 铺在历史窗口内：第 `n` 帧落在
`(n-1) × window_slots // total` 槽位，因此序列不会越出 `start_date + history_days`。
正式规模下每个 5 分钟 tick 约 39/288 个桩上报，100 万帧铺满 90 天。

**规模取 100 万条 / 5 分钟级**，依据是《04-SCML》§2.2 原文「按 5 分钟级采样（与 100 万条
规模自洽；若改分钟级需提量至千万级）」—— 设计文档已给出结论，不再挂「待 #2 拍板」。
如需分钟级精度，必须同步把规模提到千万级并重新评估生成耗时与 HDFS 容量。

> 历史 bug：早期用 `recorded_at = base + row_id × 5min`，末帧被推到约 9.5 年后，
> 直接破坏 `dt` 分区与所有时间窗指标。

---

## 需求分布模型（为什么负荷曲线不是平的）

`_orders` 的开工时刻与 `_station_hourly` 的占用率由**三层因子**驱动：

```text
占用率 = 日内形状(08 点早高峰 / 18 点晚高峰双峰)
       × 星期修正(周末压平早高峰、抬高白天与深夜)
       × 站点规模(与投运时间反序：1 号站最大、末号站最小)
       × 新站爬坡(60 天趋于饱和)
       × 噪声(0.88–1.12)
```

`load_kw` 是 #5 的**预测目标**：早期版本用均匀随机抽占用，曲线是平的，
MLlib 只能拟合噪声。建模后 seasonal-naive 基线的 WAPE 从 **67.6% 降到 17.6%**，
`peak_hour` 也集中在 08 点与 18 点。`tests/test_generator_contract.py::DemandShapeTest`
（4 项）防止退回旧状态。

---

## DWD 日期维度

#4 负责 `dim_date`；Hadoop/YARN 起来之后：

```bash
source /etc/profile.d/ev-second-project.sh
bash scripts/run_dim_date.sh 2026-06-17 90 /ev-charging/dwd/dim_date
```

---

## 当前状态与遗留

见 `docs/management/part2-scml-status.md`。**最大的外部依赖是 #3 的 `handoff/dwd`
尚未交付** —— 它一到，`scripts/run_dws_ads.sh` 就能在虚拟机上跑通全部四层并出 YARN 记录；
在那之前，本地 `build_local.py` 路径已能产出完整的 DWS/ADS 交接包。
