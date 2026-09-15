# 表 6-3 Spark MLlib 智能预测模块测试用例表

> 承担：#5 PE ｜ 分支 `feat/part2-ml`（已同步至 `dev`）
> 依据文档：《05-PE-SparkMLlib智能预测设计》§7 验收标准、《03-PRL-数据质量检测与清洗设计》未冻结事项「小时缺口策略」
> 运行环境：Ubuntu 22.04.5 LTS / JDK 17.0.20.1 / Hadoop 3.4.1 / Spark 3.5.7（伪分布式 YARN）
> 测试数据：真实 PRL 清洗产物 `dwd_station_hourly`（7 站 / 15,073 行 / 含 47 个缺失小时）；另用自测注入数据集做对照

本表使用黑盒测试方法，对本组的 Spark MLlib 智能预测模块进行测试，包括特征时间语义、小时缺口防护、
模型训练与基线对比、预测派生口径（高峰标记与物理约束）以及 ADS 交接包契约。
重点检查「行偏移特征在存在缺失小时时是否仍等价于时间偏移」「预测结果是否满足物理守恒」
「交接包字段能否被 ADS/Flask 直接消费」，并通过注入缺口与真实批次两种数据的对照，
保证异常情况被显式标记而不是静默产出错误结果。如表 6-3 所示。

| 编号 | 测试模块 | 测试操作 | 测试步骤 | 预期结果 | 实际结果 | 测试状态 |
|---|---|---|---|---|---|---|
| 1 | 特征工程（缺口防护） | 行偏移特征在缺失小时处必须停止 | 1. 取站点 1 的 4 行小时序列，人为删除 `02:00` 构造缺口；2. 直接构造 `lag_1h`；3. 再经 `align_hourly_grid()` 补齐为完整整点网格后重新构造。 | 未对齐时 `03:00` 的 `lag_1h` 会跨过缺口取到 `01:00` 的值（时间语义错位）；对齐后 `02:00` 行事实列为 NULL、`03:00` 的 `lag_1h` 为 NULL 并在训练时被丢弃。 | 与预期一致。未对齐：`lag_1h(03:00)=2.0`；对齐后：`lag_1h(02:00)=2.0`（真正的 1 小时前）、`lag_1h(03:00)=None`。真实批次训练日志输出 `[train] rows raw=15120`，等于 PRL 报告的 `expected_station_hours`（原始仅 15,073 行、缺 47 小时）。 | 通过 |
| 2 | 特征工程（标签语义） | 缺口不得导致预测标签错位 | 1. 在编号 1 的同一份缺口数据上构造直接式标签 `y_load_h1`（`lead(load_kw,1)`）；2. 分别观察对齐前后 `01:00` 的标签取值。 | 未对齐时 `01:00` 的 `y_load_h1` 会取到 `03:00` 的值（实为 2 小时后，标签错位）；对齐后 `00:00` 的标签应为真实的 1 小时后值，`01:00` 的标签因目标时刻为缺口而应为 NULL。 | 与预期一致。未对齐：`y_load_h1(01:00)=4.0`（错位）；对齐后：`y_load_h1(00:00)=2.0`、`y_load_h1(01:00)=None`。 | 通过 |
| 3 | 模型训练与评估 | 模型精度须优于 seasonal-naive 基线 | 1. 以真实 DWD `dwd_station_hourly` 为输入，按时间序 70/15/15 切分（右边界各回退 24h 防泄漏）；2. 对 horizon 1..24 × 目标（load/busy）分别训练 `GBTRegressor`，并在 h=1/6/24 训练 `RandomForestRegressor` 作对照；3. 在 test 集计算 MAE/RMSE/WAPE 并与昨日同时刻基线比较。 | 每个 horizon 选出验证集 MAE 最优者；若最优模型不优于基线则标记 `naive` 降级；报告给出 1h/6h/24h 的完整指标并与基线对比。 | 24 个步长的负荷模型全部选中 GBT。test 集负荷 MAE 1h=48.60 / 6h=47.13 / 24h=50.82，基线 79.32 / 79.35 / 80.63；WAPE 8.56% / 8.32% / 8.96% 对基线 13.93% / 13.96% / 14.17%；**24/24 个步长均优于基线**。作业以 Spark on YARN 运行（`application_1789433546680_0020`）。 | 通过 |
| 4 | 预测派生口径 | 高峰必须标记为连续两小时且不含尾部单点 | 1. 构造 `[1,9,9,1]` 与 `[5,5,0,9]` 两个站点的 4 步长负荷；2. 调用 `mark_peak_hours()` 标记高峰；3. 在真实 120 点预测结果上核对高峰行数。 | 每站只取唯一的最佳**完整两小时窗口**并标记窗口中两个点；h=4 的末端单点不构成完整窗口，不得被选中。 | 与预期一致。站点 1 标记 `[2,3]`；站点 2 标记 `[1,2]`（其末端单点 9 未被误选）。真实批次 5 个启用站共标记 `is_peak=10` 行（5 站 × 2），与 ADS 契约「每站恰好连续两个峰值小时」一致。 | 通过 |
| 5 | 预测物理约束 | 预测值须满足非负、占用守恒与负荷一致 | 1. 生成未来 24 小时 × 5 个启用站的预测；2. 对结果执行占用整数化（裁剪到 `[0, pile_count]`）与负荷按占用锚定（`busy=0 → load=0`；`busy>0 → load ≥ busy × 该站该整点历史单桩功率 P05`）；3. 统计违反约束的行数。 | 负荷与占用均非负；占用数不超过总桩数；`predicted_idle = pile_count − predicted_busy` 恒等；不存在「占用为 0 而负荷为正」或反之的行。 | 与预期一致。预测输出 `rows=120`、`is_peak=10`、`zero_load=0`，**`busy/load 不一致行 = 0`**；拥堵分级分布为 `high 13 / medium 49 / low 58`，均落在合法枚举内。合并 ADS 前的独立校验同样通过（`merge_ads` 的物理守恒与峰值连续性断言未报错）。 | 通过 |
| 6 | ADS 交接包契约 | 交接包须能被 ADS/Flask 直接消费 | 1. 执行 `publish.py` 导出预测交接包（JSON/CSV/SQLite/manifest）；2. 运行 `check_db.py` 对 `forecast.db` 做契约自检；3. 用 `export_ads_db.py --forecast-handoff` 合并进最终 ADS 库并核对批次标识。 | 交接库恰好包含 1 个非空 `run_id`、覆盖 `horizon 1..24`、站点集合与 `ads_station.forecast_enabled=1` 完全一致；三张表列集与契约一致；合并后 `is_baseline=0` 且大屏不再回落基线。 | 与预期一致。`check_db.py` 输出：`rows=120`、`distinct run_id=1`、`distinct station=5`、`horizon range=(1,24)`、`wape<baseline_wape 的步长数 = 24/24`；`manifest.json` 记录 `run_id=f-20260915-165742`、`model_version=gbt-3.5.7`、`is_baseline=0`；最终 `ads.db` 的三张预测表分别为 120/1/24 行。Flask `server/api/forecast.py` 的查询列与库结构逐一匹配。 | 通过 |

## 说明

1. **用例来源**：编号 1、2 对应 `part2/ml/tests/test_grid_align.py`；编号 3 对应训练产出的 `metrics.json` 与 YARN 记录；
   编号 4 对应 `part2/ml/tests/test_ml_contract.py`；编号 5 对应 `part2/ml/tests/test_merge_ads.py` 与预测日志；
   编号 6 对应 `part2/ml/tests/test_merge_ads.py`、`tmp/check_db.py` 与 `handoff/forecast/manifest.json`。
2. **对照数据**：为覆盖「上游存在缺失小时」这一真实情形，额外用 `tmp/make_gapped.py` 注入 6 个缺口
   （站点 1 连续 3 小时 + 3 个散点），并用 `ml/verify_coverage.py` 核验，
   结果为 `missing=6 / row_offset_safe=false`，与注入完全一致；连续数据上核验结果为 `missing=0 / row_offset_safe=true`。
3. **复现命令**：见 `part2/ml/MODEL_REPORT.md` §10 与 §12。
