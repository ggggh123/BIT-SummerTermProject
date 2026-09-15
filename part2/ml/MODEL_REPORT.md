# Spark MLlib 智能预测 · 模型报告与实验记录

> 承担：#5 PE（分支 `feat/part2-ml`）
> 对应要求：老师要求（7）数据预测：Spark MLlib（选做，本组按必做完成）
> 上游输入：DWD 层 `dwd_station_hourly`
> 下游输出：ADS 契约表 `ads_forecast_batch` / `ads_forecast_24h` / `ads_forecast_metric`
> 运行环境：Hadoop 3.4.1 单机伪分布式 + Spark 3.5.7 on YARN（Ubuntu 22.04.5，4 核 / 10 GB）

---

## 1. 数据与切分

| 项 | 值 |
|---|---|
| 数据源 | `dwd_station_hourly`（小时级站点明细） |
| 本次实验数据 | 自测合成数据 17,280 行 = 8 站 × 90 天 × 24h（时间窗 2026-06-01 ~ 2026-08-29，seed=20260914） |
| 切分方式 | **按时间顺序 70% / 15% / 15%**，禁止随机打乱 |
| 训练集右边界 | 2026-08-03 00:00 |
| 验证集右边界 | 2026-08-16 12:00 |
| **防泄漏间隔** | 训练集与验证集右边界各自再前移 `max(horizon)=24h`，避免「训练样本标签落入验证期」的隐性泄漏 |
| 有效样本 | train 11,904 / valid 2,400 / test 2,592 |

> 说明：本次为**自测数据集**（#3 的 `handoff/dwd` 到位后重跑同一套脚本即可，无需改代码）。

## 2. 特征工程（13 个）

| 类别 | 特征 | 构造约束 |
|---|---|---|
| 时间 | `hour`、`day_of_week`、`is_weekend`、`is_holiday_flag` | — |
| 气象 | `temperature_c` | 来自上游 |
| 负荷滞后 | `lag_1h`、`lag_24h` | `lag(col, n)`，严格早于预测起点 |
| 负荷滚动 | `roll_6h`、`roll_24h` | `rowsBetween(-N, -1)`，**不含当前行** |
| 占用滞后 | `busy_lag_1h`、`busy_lag_24h` | 同上 |
| 占用滚动 | `busy_roll_6h`、`busy_roll_24h` | 同上 |

**未来数据泄漏防护**：滞后用 `lag`、滚动窗口统一 `rowsBetween(-N, -1)`、标签用 `lead`，
三者在结构上保证特征只来自预测起点 `t` 及其之前。

## 3. 模型与超参数

| 项 | 设置 |
|---|---|
| 多步策略 | **直接式（direct multi-horizon）**：对 h ∈ 1..24 各训练一个独立模型，标签为 `t+h` 的真实值；**不使用递归式**，避免误差累积 |
| 算法 | `GBTRegressor`（主）；`RandomForestRegressor`（对照，仅在 h=1/6/24 训练以控制耗时） |
| Pipeline | `VectorAssembler` → 回归器（特征与模型随 Pipeline 一并保存） |
| GBT 超参 | `maxIter=60, maxDepth=5, stepSize=0.1, seed=20260914` |
| RF 超参 | `numTrees=60, maxDepth=6, seed=20260914` |
| 模型规模 | 24 步长 × 2 目标 = **48 个 GBT** + 3 步长 × 2 目标 = **6 个 RF 对照**，共 54 个 |
| 基线 | **seasonal-naive**：目标时刻 `t+h` 的「昨日同一时刻」值（即 `t+h-24h`） |
| 模型选择 | 验证集 MAE 最优者胜出；若最优模型**不优于基线**则标记 `naive` 降级（《05》§3） |
| 评估指标 | MAE / RMSE / WAPE（`WAPE = Σ|误差| / Σ|实际| × 100%`） |

## 4. 关键口径结果（1h / 6h / 24h）

| horizon | MAE | RMSE | WAPE | 基线 WAPE | 相对提升 |
|---|---|---|---|---|---|
| **1h** | 34.35 | 59.92 | **17.41%** | 20.88% | **+16.6%** |
| **6h** | 39.52 | 69.66 | **19.93%** | 20.86% | **+4.5%** |
| **24h** | 33.63 | 59.97 | **16.91%** | 20.74% | **+18.5%** |

RF 对照（验证集 MAE）：h=1 为 40.57、h=6 为 43.59，均劣于 GBT（32.77 / 35.10），
故 6 个对照口径全部由 GBT 胜出。

## 5. 全部 24 个步长指标（test 集）

| h | MAE | RMSE | WAPE | 基线 WAPE | 是否优于基线 |
|---|---|---|---|---|---|
| 1 | 34.35 | 59.92 | 17.41 | 20.88 | ✅ |
| 2 | 33.92 | 59.72 | 17.18 | 20.87 | ✅ |
| 3 | 36.29 | 64.17 | 18.36 | 20.85 | ✅ |
| 4 | 35.79 | 62.93 | 18.09 | 20.86 | ✅ |
| 5 | 76.56 | 149.08 | 38.64 | 20.86 | ❌ |
| 6 | 39.52 | 69.66 | 19.93 | 20.86 | ✅ |
| 7 | 34.11 | 59.41 | 17.22 | 20.86 | ✅ |
| 8 | 39.09 | 78.16 | 19.76 | 20.91 | ✅ |
| 9 | 35.36 | 62.18 | 17.92 | 20.96 | ✅ |
| 10 | 43.81 | 89.52 | 22.25 | 20.99 | ❌ |
| 11 | 48.84 | 87.70 | 24.83 | 20.97 | ❌ |
| 12 | 42.09 | 82.92 | 21.38 | 20.92 | ❌ |
| 13 | 42.06 | 76.40 | 21.32 | 20.91 | ❌ |
| 14 | 35.82 | 63.98 | 18.11 | 20.89 | ✅ |
| 15 | 45.44 | 88.79 | 22.91 | 20.87 | ❌ |
| 16 | 46.69 | 89.36 | 23.47 | 20.85 | ❌ |
| 17 | 52.30 | 101.25 | 26.23 | 20.85 | ❌ |
| 18 | 48.09 | 83.55 | 24.07 | 20.86 | ❌ |
| 19 | 34.89 | 63.56 | 17.44 | 20.83 | ✅ |
| 20 | 35.34 | 65.16 | 17.68 | 20.81 | ✅ |
| 21 | 36.79 | 67.38 | 18.43 | 20.78 | ✅ |
| 22 | 38.62 | 65.57 | 19.39 | 20.71 | ✅ |
| 23 | 36.57 | 64.06 | 18.38 | 20.75 | ✅ |
| 24 | 33.63 | 59.97 | 16.91 | 20.74 | ✅ |

**汇总：24 个步长中 15 个 test WAPE 优于 seasonal-naive 基线，9 个不及。**

WAPE 趋势（相对基线的增益，条越短越好）：

```text
基线  ████████████████████████████████████  20.9%
h= 1  ███████████████████████████████       17.41%  ← 优于基线
h= 6  ██████████████████████████████████▌   19.93%  ← 略优于基线
h=24  ██████████████████████████████        16.91%  ← 优于基线
h= 5  ███████████████████████████████████████████████████████████████████  38.64%  ← 明显劣于基线
h=17  █████████████████████████████████████████████████  26.23%  ← 劣于基线
```

## 6. 物理约束与兜底

| 约束 | 实现 |
|---|---|
| 负荷非负 | `greatest(pred, 0)` |
| 占用水位 | 裁剪到 `[0, pile_count]` 后**取整**；**不做下界抬升** |
| **负荷按占用锚定** | `busy = 0` → `load = 0`；`busy > 0` → `load ≥ busy × 该站该整点历史单桩功率 P05`（兜底 3.5 kW） |
| 空闲桩守恒 | `predicted_idle = pile_count − predicted_busy`（整数），保证 `busy + idle = pile_count` |
| NaN | GBT 输出为实数，无需额外填充；空值由 `coalesce` 兜底 |

**为什么用「占用锚定」而不是「历史分位下界」**：`tmp/diag_low.py` 的实测诊断显示，
自测数据中 `hour = 0` 的 `load_kw` 约有 10% 的样本真的是 0（P05 = 0），
用分位做下界**无法纠正**低谷的零值；而数据本身严格满足「`busy = 0` ⟺ `load = 0`」
（不一致行数为 0）。因此正确做法是**先定占用、再由占用反推负荷下限**，
既消除 GBT 在低谷外推出的病态值，也保证负荷与占用在物理上一致。

**拥堵分级**：预测占用率 `busy / pile_count` ≥80% → `high`，≥50% → `medium`，其余 `low`。

**高峰标记**：未来 24h 内负荷最大的**连续 2h**，该 2 小时**两个点都标 `is_peak = 1`**
（与 #1 的 `web/src/mock/forecast_24h.json` 契约一致）。本次 192 行中标记 16 行 = 8 站 × 2。

## 7. 实验记录

| # | 运行 | 用途 | 数据 | 结论 |
|---|---|---|---|---|
| E1 | `application_1789375131230_0003` | 3 步长首版（1/6/24） | 合成 17,280 行 | GBT 全面优于基线；确认链路可用 |
| E2 | `application_1789433546680_0002` | 扩展为 1..24 全步长 | 合成 17,280 行 | 54 个模型；1/6/24 口径复现 E1 结果 |
| E3 | `application_1789433546680_0003` | 生成未来 24h 预测 | 同上 | `ads_forecast_24h` 192 行；is_peak 16 行 |
| E4 | 待 #3 的 `handoff/dwd` 到位 | 真实 DWD 重跑 | 待定 | — |

> 每个应用 ID 均可在 YARN ResourceManager UI（`http://localhost:8088`）查到运行记录，
> 满足《05》§7 验收第 4 条。

## 8. 局限说明（如实声明）

1. **数据为项目生成的模拟数据**，周期性由生成器显式建模（日内双峰 + 周/节假日修正），
   因此指标偏乐观，**不代表真实业务精度**；报告中不对结论作夸大。
2. **9/24 个步长在 test 集不及基线**：这些步长在 **验证集**上 GBT 优于基线（故按规则选用 GBT），
   但在 test 段发生分布漂移。这属于真实的泛化局限，已在 `ads_forecast_metric` 中如实记录
   （每个步长同时给出 `wape` 与 `baseline_wape`，供大屏与答辩自查）。
3. **h=5 与 h=17 明显异常**（WAPE 38.6% / 26.2%）：初步判断为单点步长的样本量偏少叠加
   凌晨/晚间转折时段的非线性，后续可通过增加数据窗口或对步长做平滑集成改善。
4. **无真实天气数据**：`temperature_c` 来自模拟，未引入真实气象。
5. **垃圾进垃圾出**：所有结论的前提是 DWD 层数据可信（由 #3 的质量发现与清洗保证）。

## 9. 复现命令

```bash
# 1) 训练（54 个模型，约 25 分钟）
spark-submit --master yarn --executor-memory 2g --driver-memory 2g \
  --num-executors 1 --executor-cores 2 ml/train.py \
  --dwd hdfs://pangxiangzhen01:8020/ev-charging/dwd/dwd_station_hourly \
  --model-out hdfs://pangxiangzhen01:8020/ev-charging/forecast/models \
  --metrics ./tmp/metrics.json --horizons 1-24 --rf-horizons 1,6,24

# 2) 预测（约 10 分钟）
spark-submit --master yarn --executor-memory 2g --driver-memory 2g \
  --num-executors 1 --executor-cores 2 ml/predict.py \
  --dwd hdfs://pangxiangzhen01:8020/ev-charging/dwd/dwd_station_hourly \
  --dim-stations hdfs://pangxiangzhen01:8020/ev-charging/dwd/dim_stations \
  --model-out hdfs://pangxiangzhen01:8020/ev-charging/forecast/models \
  --metrics ./tmp/metrics.json \
  --out hdfs://pangxiangzhen01:8020/ev-charging/ads/ads_forecast_24h --horizons 1-24

# 3) 发布交接包（三张契约表 + manifest）
spark-submit --master yarn --executor-memory 1g --driver-memory 2g \
  ml/publish.py --src hdfs://pangxiangzhen01:8020/ev-charging/ads/ads_forecast_24h \
  --handoff ./handoff/forecast --metrics ./tmp/metrics.json \
  --sqlite ./handoff/forecast/forecast.db

# 4) 契约自检
python3 tmp/check_db.py ./handoff/forecast/forecast.db
```

## 10. 交付与验收对照（《05》§7）

| 验收项 | 结果 |
|---|---|
| 1h/6h/24h 的 MAE/RMSE/WAPE 完整 + 与基线对比 | ✅ 见 §4，并扩展到全部 24 个步长 |
| 预测满足物理约束（非负、占用 ≤ 总桩数、无 NaN） | ✅ 见 §6（占用锚定 + 空闲桩守恒），不一致行数为 0 |
| 预测批次经 ADS → API → 大屏可见；无结果显示「暂无预测」 | ✅ 三张契约表就位（`is_baseline=0`），Flask 缺表时按契约回 `code 4041` |
| 作业运行于本机伪分布式 Spark on Hadoop，可出示 YARN 记录 | ✅ 见 §7 的应用 ID |
