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

#: 预测步长（小时）
HORIZONS: tuple[int, ...] = (1, 6, 24)

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
