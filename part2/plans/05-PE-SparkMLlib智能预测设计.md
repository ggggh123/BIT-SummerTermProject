# 05 · Spark MLlib 智能预测子系统设计说明书（#5 PE）

> 承担人：#5（PE，模块开发 / 模型实验）
> 任务性质：**选做任务（老师标注选做；本小组决定实现）**
> 对应老师要求：（7）数据预测：Spark MLlib
> 上游输入：DWD 层 `dwd_station_hourly` 明细（#3 清洗产出，经 `handoff/dwd` 交接）
> 下游输出：`ads_forecast_result` 预测结果表 → #2 Flask `/api/forecast/*` → #1 大屏预测模块

---

## 1. 模块目标

基于 **Spark MLlib**，利用 90 天 × 8 站 × 小时级的充电历史数据，预测各充电站**未来 1 小时、6 小时、24 小时**的充电负荷、空闲桩数量与高峰时段，并将结果接入大屏，为用户推荐低拥堵站点、为运营端提供负荷预警。

> 选做说明：本子系统为老师要求中的选做项（文档**保留「选做」标注**）。**本组按必做标准完成**；若 D3 仍不达标，按降级预案以 seasonal-naive 基线顶上并如实标注，或大屏显示「暂无预测」，不伪造结果。

## 2. 数据与特征设计

### 2.1 数据源

- 主表：DWD 层 `dwd_station_hourly`（源自第一阶段 `station_hourly_history`，经 ODS `ods_station_hourly` 清洗）：`station_id, observed_at, pile_count, rated_power_kw, temperature_c, is_holiday, busy_count, load_kw`。
- 规模：8 站 × 24h × 90 天 = 17,280 条（小时级站点粒度；如时间允许可下钻桩级，但默认不做单桩预测）。

### 2.2 特征工程（沿用第一阶段口径）

| 特征 | 说明 |
|---|---|
| `hour` | 目标时刻小时（0–23） |
| `day_of_week` | 星期 |
| `is_weekend` / `is_holiday` | 周末/节假日标记 |
| `temperature_c` | 模拟温度 |
| `lag_1h` / `lag_24h` | 预测起点之前 1h / 24h 的负荷与占用 |
| `roll_6h` / `roll_24h` | 6h / 24h 滚动均值 |

滞后与滚动特征只允许使用预测起点**之前**的数据，禁止未来数据泄漏。

### 2.3 预测目标

1. `load_kw`：站点负荷（回归）。
2. `busy_count`：占用桩数（回归）；`predicted_idle = pile_count − predicted_busy`。
3. 派生：未来 24h 中负荷最大的连续 2h 标记为高峰（`is_peak`）；预测占用率 ≥ 80% 标记高拥堵（`congestion_level = low/medium/high`）。

## 3. 模型设计（Spark MLlib）

| 项 | 方案 |
|---|---|
| 算法 | `GBTRegressor`（主）、`RandomForestRegressor`（对照）；均为 MLlib 原生，满足「Spark MLlib 预测」要求 |
| Pipeline | `VectorAssembler` → `GBTRegressor`，特征与模型随 Pipeline 保存 |
| 切分 | 按时间顺序 train(70%)/valid(15%)/test(15%)，**禁止随机打乱** |
| 基线 | seasonal-naive：昨日同一时刻值 |
| 评估 | MAE、RMSE、WAPE（负荷）；MAE（空闲桩）；分 1h/6h/24h 三个 horizon 报告 |
| 物理约束 | 预测值非负、`busy_count ≤ pile_count`、无 NaN，违例裁剪 |
| 模型选择 | 验证集上 GBT 不优于基线 → 部署基线并如实标注 |
| **多步策略** | **直接式（direct multi-horizon）**：对 1h/6h/24h **各训练一个独立模型**（目标为 `t+h` 的值），特征只取 `t` 及其之前；**不使用递归式**，避免误差累积与未来泄漏 |

## 4. 作业与落地

```text
ml/
├── features.py        # 特征工程（Spark DataFrame）
├── train.py           # 训练 + 评估 + 模型选择，输出 metrics.json
├── predict.py         # 生成未来 24h × 8 站预测
└── publish.py         # 写 ads_forecast_result（Parquet + 导出 JSON）
```

- `ads_forecast_result` 字段：`run_id, station_id, forecast_at, horizon_h, predicted_load_kw, predicted_busy_count, predicted_idle_count, congestion_level, is_peak, model_version, generated_at`（与第一阶段 `forecasts` 表口径一致）。
- 大屏消费方式：主页叠加未来 24h 预测曲线；用户页「低拥堵推荐榜」按未来 1h 预测空闲与拥堵等级排序；企业/政府页展示负荷预警。
- 训练与预测均以 Spark 作业在本机伪分布式 Hadoop 环境运行，模型与 `metrics.json` 存 HDFS `/ev-charging/forecast/`，并导出 `handoff/forecast/`（含 `manifest.json`）交付 #2。

### 4.1 分担工作（#5）

为平衡 3 天工期负载，#5 在预测之外**分担**以下数仓工作（与 #4 对接、口径一致）：

- `dws_region_day`（区域 × 日汇总）
- `ads_gov_service`（政府视角：区域覆盖 / 服务指标 / 等效碳减排）

产出随 `handoff/dws`、`handoff/ads` 一并交付；实现须遵守《04》§3.4 对账约束。

## 5. 实验记录与模型报告

- 每次实验记录：数据 cutoff、特征版本、参数、验证/测试指标、结论。
- 模型报告包含：与基线对比表、1h/6h/24h 指标、典型站点拟合曲线图、局限说明（模拟数据、无真实天气）。

## 6. 排期（3 天，与前端并行）

| 日 | 任务 |
|---|---|
| D1 | 配合 #4 确认 `dwd_station_hourly` 生成口径（含温度/节假日特征真实性）；特征工程骨架 |
| D2 | 基线 + GBT 首版训练，出首份指标；#5 分担的 `dws_region_day`、`ads_gov_service` 完成 |
| D3 | 模型选择、物理约束、`ads_forecast_result` 落地；接入 Flask/大屏；模型报告定稿；晚间彩排 |

## 7. 验收标准（选做口径）

1. 1h/6h/24h 三个 horizon 的 MAE/RMSE/WAPE 指标完整，并与 seasonal-naive 基线对比。
2. 预测结果满足物理约束（非负、占用 ≤ 总桩数、无 NaN）。
3. 预测批次经 ADS 表 → API → 大屏完整可见；无结果时大屏显示「暂无预测」。
4. 训练/预测作业运行于本机伪分布式 Spark on Hadoop，可出示 YARN 记录。

## 8. 风险与降级

| 风险 | 应对 |
|---|---|
| GBT 不优于基线 | 部署基线并在报告与大屏如实标注 |
| 进度不足 | 砍桩级预测与自动调参，保站点级 1h/6h/24h 主链路 |
| 模拟数据周期性强导致指标「过于好看」 | 报告中如实说明数据为项目生成，避免夸大结论 |
