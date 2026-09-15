"""Part2 智能预测 · 特征工程（#5 PE，分支 feat/part2-ml）。

数据源：DWD 层 dwd_station_hourly（Parquet 或本地 CSV 自测）
    station_id, observed_at, pile_count, rated_power_kw,
    temperature_c, is_holiday, busy_count, load_kw

设计约束（《05》§2.2、§3）：
  1. 直接式多 horizon（direct multi-horizon）：h ∈ {1, 6, 24} 各训练一个独立模型，
     标签直接取目标时刻 t+h 的真实值，不做递归式预测，避免误差累积。
  2. 滞后 / 滚动特征只能来自预测起点 t 及其之前的数据：
     lag 取严格早于当前行的记录，滚动窗口统一 rowsBetween(-N, -1)（不含当前行），
     从结构上杜绝未来数据泄漏。
  3. 时间列先按原样读入再显式解析为 timestamp（呼应《06》§8.1 的 PySpark 真实坑）。

用法：
    spark-submit train.py --dwd <path> --out <path>
"""

from __future__ import annotations

from typing import Iterable, Sequence

from pyspark.sql import DataFrame, SparkSession, Window
from pyspark.sql import functions as F

# --------------------------------------------------------------------------
# 常量：与《05》§2.2 冻结的口径保持一致
# --------------------------------------------------------------------------

#: 基础特征（《05》§2.2 清单）+ 占用侧历史特征（预测 busy_count 必需）
FEATURE_COLS: tuple[str, ...] = (
    "hour",
    "day_of_week",
    "is_weekend",
    "is_holiday_flag",
    "temperature_c",
    "lag_1h",
    "lag_24h",
    "roll_6h",
    "roll_24h",
    "busy_lag_1h",
    "busy_lag_24h",
    "busy_roll_6h",
    "busy_roll_24h",
)

#: 预测步长（小时）。ADS 契约表 `ads_forecast_24h` 要求 horizon 覆盖 1–24
#: （每站 24 个点，大屏 24h 曲线），故默认训练全部 24 个 horizon。
HORIZONS: tuple[int, ...] = tuple(range(1, 25))

#: 《05》验收与模型报告重点报告的口径
REPORT_HORIZONS: tuple[int, ...] = (1, 6, 24)

#: 标签列名模板
LOAD_LABEL = "y_load_h{h}"
BUSY_LABEL = "y_busy_h{h}"

#: 基线列：seasonal-naive（昨日同一时刻）
NAIVE_LOAD = "lag_24h"
NAIVE_BUSY = "busy_lag_24h"

REQUIRED_COLUMNS = ("station_id", "observed_at", "pile_count", "busy_count", "load_kw")


# --------------------------------------------------------------------------
# 读取
# --------------------------------------------------------------------------

def read_hourly(spark: SparkSession, path: str) -> DataFrame:
    """读取 dwd_station_hourly；``*.csv`` 走 CSV 分支（仅用于本地自测）。

    统一把字段显式 cast 到目标类型，避免上游 schema 漂移（《06》§8.1）。
    """
    if path.lower().endswith(".csv") or path.lower().endswith(".csv.gz"):
        raw = spark.read.option("header", True).option("inferSchema", True).csv(path)
    else:
        raw = spark.read.parquet(path)

    missing = [c for c in REQUIRED_COLUMNS if c not in raw.columns]
    if missing:
        raise ValueError(f"dwd_station_hourly 缺少必需字段: {missing}")

    if "rated_power_kw" not in raw.columns:
        raw = raw.withColumn("rated_power_kw", F.lit(None).cast("int"))
    if "temperature_c" not in raw.columns:
        raw = raw.withColumn("temperature_c", F.lit(None).cast("double"))
    if "is_holiday" not in raw.columns:
        raw = raw.withColumn("is_holiday", F.lit(False))

    df = (
        raw.withColumn("station_id", F.col("station_id").cast("int"))
        .withColumn("observed_at", F.to_timestamp("observed_at"))
        .withColumn("pile_count", F.col("pile_count").cast("int"))
        .withColumn("rated_power_kw", F.col("rated_power_kw").cast("int"))
        .withColumn("temperature_c", F.col("temperature_c").cast("double"))
        .withColumn("is_holiday", F.col("is_holiday").cast("boolean"))
        .withColumn("busy_count", F.col("busy_count").cast("int"))
        .withColumn("load_kw", F.col("load_kw").cast("double"))
    )
    return df.filter(F.col("observed_at").isNotNull())


# --------------------------------------------------------------------------
# 小时网格对齐与特征可用性（缺口防护）
# --------------------------------------------------------------------------

#: 站点级缓变维度：补齐网格时按站点常数回填，不参与逐小时的缺口语义
GRID_DIM_COLS: tuple[str, ...] = ("pile_count", "rated_power_kw")

#: 允许用站点统计量兜底填充的外生特征（缺失时上游未提供真实观测）
FILLABLE_COLS: tuple[str, ...] = ("temperature_c",)


def align_hourly_grid(
    df: DataFrame, dim_cols: Sequence[str] = GRID_DIM_COLS
) -> DataFrame:
    """把每个站点的序列补齐到**完整整点网格**，缺失小时的事实列置 NULL。

    为什么必须做
    ------------
    本模块的滞后 / 滚动 / 标签特征全部是**行偏移**语义（``lag(n)``、``lead(n)``、
    ``rowsBetween(-N, -1)``）。只有当站点序列逐小时连续时，行偏移才等价于时间偏移。
    上游 PRL 已明确 ``hourlyCoverage.rowOffsetMlSafe`` 可能为 false（正式批次
    ``expected=15120 / retained=15073 / missing=47``），若不补齐：

    * ``lag_24h`` 会静默取到「第 24 行之前」而不是「24 小时之前」的观测；
    * ``lead(h)`` 生成的标签同样错位 —— 指标会虚高，且不会抛出任何异常。

    补齐后每一行都对应一个真实整点，缺口行的 ``load_kw`` / ``busy_count`` /
    ``temperature_c`` 为 NULL，训练侧由 ``dropna`` 与 ``VectorAssembler`` 的
    ``handleInvalid="skip"`` 丢弃不可用样本，从而在结构上保证时间语义正确。

    实现要点：用 ``sequence`` 生成网格后 left join 事实列；桩数、额定功率属于
    站点缓变维度，按站点常数回填（它们不随小时变化，缺失不代表数据缺口）。
    """
    present_dims = [c for c in dim_cols if c in df.columns]
    span = df.groupBy("station_id").agg(
        F.min("observed_at").alias("_grid_first"),
        F.max("observed_at").alias("_grid_last"),
    )
    grid = span.select(
        F.col("station_id"),
        F.explode(
            F.sequence(
                F.col("_grid_first"), F.col("_grid_last"), F.expr("INTERVAL 1 HOUR")
            )
        ).alias("observed_at"),
    )
    facts = df.drop(*present_dims) if present_dims else df
    out = grid.join(facts, on=["station_id", "observed_at"], how="left")
    if present_dims:
        dims = df.groupBy("station_id").agg(*[F.max(c).alias(c) for c in present_dims])
        out = out.join(dims, on="station_id", how="left")
    return out


def fill_missing_features(
    df: DataFrame, cols: Sequence[str] = FILLABLE_COLS
) -> tuple[DataFrame, dict]:
    """对允许兜底的特征列做站点均值填充，返回 ``(df, 填充统计)``。

    仅针对外生且非因果的特征（当前为 ``temperature_c``）。保留 NULL 会让该站点
    无法成为预测起点，进而使预测站点集合与 ``forecast_enabled`` 不一致而被
    ``merge_ads`` 拒绝合并 —— 这是比填充更严重的交付失败。

    权衡说明：站点均值会用到期内全部观测（含未来），属于轻度信息泄漏，但该列是
    外部天气量、不参与自回归因果链，且填充比例与策略会原样写入报告供团队评审。
    """
    stats: dict = {}
    for col in cols:
        if col not in df.columns:
            continue
        total = df.count()
        missing = df.filter(F.col(col).isNull()).count()
        if missing == 0:
            stats[col] = {"missing_rows": 0, "total_rows": int(total), "strategy": "none"}
            continue
        overall = df.agg(F.avg(col)).first()[0]
        fallback = 0.0 if overall is None else float(overall)
        window = Window.partitionBy("station_id")
        df = df.withColumn(
            col,
            F.coalesce(F.col(col), F.avg(col).over(window), F.lit(fallback)).cast("double"),
        )
        stats[col] = {
            "missing_rows": int(missing),
            "total_rows": int(total),
            "missing_ratio": round(missing / total, 8) if total else None,
            "strategy": "station_mean_then_global_mean",
            "global_mean": round(fallback, 6),
        }
    return df, stats


def resolve_feature_cols(
    df: DataFrame, base: Sequence[str] = FEATURE_COLS
) -> tuple[list[str], list[str]]:
    """剔除在数据中**整列为空**的特征，返回 ``(可用特征, 被剔除特征)``。

    单列全空时若仍交给 ``VectorAssembler``，``handleInvalid="skip"`` 会跳过
    **全部**样本，最终写出 0 行预测且不报错。这里显式降级并把它记录进报告。
    """
    present = [c for c in base if c in df.columns]
    if not present:
        return [], []
    counts = df.agg(*[F.count(c).alias(c) for c in present]).first()
    dropped = [c for c in present if counts[c] == 0]
    return [c for c in present if c not in dropped], dropped


# --------------------------------------------------------------------------
# 特征与标签
# --------------------------------------------------------------------------

def build_features(df: DataFrame, horizons: Sequence[int] = HORIZONS) -> DataFrame:
    """构造时间特征、滞后/滚动特征，并为每个 horizon 追加直接式标签。"""
    w = Window.partitionBy("station_id").orderBy("observed_at")

    out = (
        df
        # 时间特征
        .withColumn("hour", F.hour("observed_at"))
        .withColumn("day_of_week", F.dayofweek("observed_at"))          # 1=周日, 7=周六
        .withColumn("is_weekend", F.when(F.dayofweek("observed_at").isin(1, 7), 1).otherwise(0))
        .withColumn("is_holiday_flag", F.when(F.col("is_holiday"), 1).otherwise(0))
        # 滞后特征：严格早于预测起点 t
        .withColumn("lag_1h", F.lag("load_kw", 1).over(w))
        .withColumn("lag_24h", F.lag("load_kw", 24).over(w))
        .withColumn("busy_lag_1h", F.lag("busy_count", 1).over(w))
        .withColumn("busy_lag_24h", F.lag("busy_count", 24).over(w))
        # 滚动统计：窗口 [-N, -1]，不含当前行
        .withColumn("roll_6h", F.avg("load_kw").over(w.rowsBetween(-6, -1)))
        .withColumn("roll_24h", F.avg("load_kw").over(w.rowsBetween(-24, -1)))
        .withColumn("busy_roll_6h", F.avg("busy_count").over(w.rowsBetween(-6, -1)))
        .withColumn("busy_roll_24h", F.avg("busy_count").over(w.rowsBetween(-24, -1)))
    )

    for h in horizons:
        out = out.withColumn(LOAD_LABEL.format(h=h), F.lead("load_kw", h).over(w))
        out = out.withColumn(BUSY_LABEL.format(h=h), F.lead("busy_count", h).over(w))

    return out


def split_by_time(
    df: DataFrame,
    horizons: Sequence[int] = HORIZONS,
    train_ratio: float = 0.70,
    valid_ratio: float = 0.15,
) -> tuple[DataFrame, DataFrame, DataFrame, dict]:
    """按预测起点的时间顺序切分 train / valid / test（禁止随机打乱）。

    为防止「训练样本的标签落在验证期内」这类隐性泄漏，
    train 与 valid 的右边界各自再向前回退 ``max(horizons)`` 小时。
    """
    stamps = [
        r[0]
        for r in df.select("observed_at").distinct().orderBy("observed_at").collect()
    ]
    if len(stamps) < 10:
        raise ValueError(f"时间点过少，无法切分：{len(stamps)}")

    n = len(stamps)
    t_train_end = stamps[int(n * train_ratio)]
    t_valid_end = stamps[int(n * (train_ratio + valid_ratio))]
    gap = F.expr(f"INTERVAL {max(horizons)} HOURS")

    train = df.filter(F.col("observed_at") < (F.lit(t_train_end) - gap))
    valid = df.filter(
        (F.col("observed_at") >= F.lit(t_train_end))
        & (F.col("observed_at") < (F.lit(t_valid_end) - gap))
    )
    test = df.filter(F.col("observed_at") >= F.lit(t_valid_end))

    bounds = {
        "train_end": t_train_end.strftime("%Y-%m-%d %H:%M:%S"),
        "valid_end": t_valid_end.strftime("%Y-%m-%d %H:%M:%S"),
        "gap_hours": max(horizons),
    }
    return train, valid, test, bounds


def add_naive_baseline(df: DataFrame, horizons: Sequence[int] = HORIZONS) -> DataFrame:
    """追加 seasonal-naive 基线列：目标时刻 t+h 的「昨日同一时刻」值。

    目标时刻为 t+h，其昨日同一时刻即 t+h-24，相对预测起点 t 而言等于 lag(24 - h)；
    当 h = 24 时退化为当前值。
    """
    w = Window.partitionBy("station_id").orderBy("observed_at")
    for h in horizons:
        lag_n = 24 - h
        load_naive = F.col("load_kw") if lag_n <= 0 else F.lag("load_kw", lag_n).over(w)
        busy_naive = F.col("busy_count") if lag_n <= 0 else F.lag("busy_count", lag_n).over(w)
        df = (
            df.withColumn(f"naive_load_h{h}", load_naive.cast("double"))
            .withColumn(f"naive_busy_h{h}", busy_naive.cast("double"))
        )
    return df


# --------------------------------------------------------------------------
# 评估与物理约束
# --------------------------------------------------------------------------

def evaluate(pred_df: DataFrame, label_col: str, pred_col: str) -> dict:
    """计算 MAE / RMSE / WAPE 与样本数。WAPE = Σ|err| / Σ|y|。"""
    err = F.col(pred_col) - F.col(label_col)
    row = pred_df.select(err.alias("err"), F.col(label_col).alias("y")).agg(
        F.mean(F.abs("err")).alias("mae"),
        F.sqrt(F.mean(F.col("err") * F.col("err"))).alias("rmse"),
        (F.sum(F.abs("err")) / F.sum(F.abs("y"))).alias("wape"),
        F.count(F.lit(1)).alias("n"),
    ).first()

    def _f(v):
        return None if v is None else round(float(v), 6)

    return {"mae": _f(row["mae"]), "rmse": _f(row["rmse"]), "wape": _f(row["wape"]), "n": int(row["n"])}


def clip_to_physical(
    df: DataFrame, pred_col: str, upper_col: str | None = None, lower: float = 0.0
) -> DataFrame:
    """物理约束：预测值非负；占用数不得超过总桩数（upper_col=pile_count）。"""
    col = F.greatest(F.col(pred_col), F.lit(lower))
    if upper_col is not None:
        col = F.least(col, F.col(upper_col).cast("double"))
    filled = F.when(col.isNull(), F.lit(lower)).otherwise(col)
    return df.withColumn(pred_col, filled)
