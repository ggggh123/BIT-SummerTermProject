"""Spark MLlib Ridge regression model — replaces scikit-learn Ridge.

This module is the drop-in Spark MLlib implementation for phase 2.  It
exposes the exact same public contract that ``forecast.build_forecast_run``
and ``ridge.predict_ridge`` rely on:

    model.predict(pandas.DataFrame) -> numpy.ndarray

so all downstream consumers (feature engineering, champion selection,
144-record forecast construction, TCP publish) stay unchanged.

scikit-learn -> Spark MLlib mapping
-----------------------------------
    Ridge(alpha=1.0)               -> LinearRegression(regParam=1.0,
                                                       elasticNetParam=0.0)
    OneHotEncoder(drop=None)       -> StringIndexer + OneHotEncoder(dropLast=False)
    StandardScaler()               -> StandardScaler(withMean=True, withStd=True)
    ColumnTransformer + Pipeline   -> VectorAssembler + Pipeline
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from pyspark.ml import Pipeline, PipelineModel
from pyspark.ml.feature import (
    OneHotEncoder,
    StandardScaler,
    StringIndexer,
    VectorAssembler,
)
from pyspark.ml.regression import LinearRegression
from pyspark.sql import SparkSession
from pyspark.sql.functions import col
from pyspark.sql.types import StringType

# Windows fixes — must be set before ``SparkSession`` is created (import time):
#   * ``PYSPARK_PYTHON`` + ``SPARK_LOCAL_IP``: pin the Python worker executable
#     and local bind address, otherwise the Spark JVM never receives the socket
#     connect-back from spawned Python workers
#     (``SocketTimeoutException: Accept timed out``).
#   * ``HADOOP_HOME``: point at an existing directory so Hadoop's ``Shell``
#     static init does not throw "HADOOP_HOME and hadoop.home.dir are unset".
#     ``winutils.exe`` is not required for local filesystem mode.
os.environ.setdefault("PYSPARK_PYTHON", sys.executable)
os.environ.setdefault("SPARK_LOCAL_IP", "127.0.0.1")
if os.name == "nt" and not os.environ.get("HADOOP_HOME"):
    os.environ["HADOOP_HOME"] = str(Path.home())

# Feature contract — single source of truth, re-exported by ridge.py.
FEATURE_COLUMNS = [
    "station_id",
    "horizon_h",
    "pile_count",
    "rated_power_kw",
    "target_hour_sin",
    "target_hour_cos",
    "target_dow_sin",
    "target_dow_cos",
    "target_is_weekend",
    "target_is_holiday",
    "target_temperature_c",
    "load_lag_1",
    "load_lag_24",
    "load_roll_6",
    "load_roll_24",
    "busy_lag_1",
    "busy_lag_24",
    "busy_roll_6",
    "busy_roll_24",
]

CATEGORICAL_COLS = ["station_id"]
NUMERIC_COLS = [c for c in FEATURE_COLUMNS if c not in CATEGORICAL_COLS]

# scikit-learn Ridge(alpha=1.0) == Spark LinearRegression(regParam=1.0,
# elasticNetParam=0.0).  elasticNetParam=0.0 gives pure L2 (Ridge).
RIDGE_REG_PARAM = 1.0
RIDGE_ELASTIC_NET = 0.0

# Internal column names used inside the Spark pipeline.
_STATION_STR_COL = "station_id_str"
_STATION_IDX_COL = "station_id_idx"
_STATION_VEC_COL = "station_id_vec"
_NUM_VEC_COL = "num_features"
_NUM_SCALED_COL = "num_scaled"
_LABEL_COL = "__label__"
_PREDICTION_COL = "prediction"

_spark: SparkSession | None = None


def get_spark() -> SparkSession:
    """Return a shared local SparkSession, created lazily on first use."""
    global _spark
    if _spark is None:
        _spark = (
            SparkSession.builder.master("local[2]")
            .appName("evml-ridge")
            .config("spark.ui.enabled", "false")
            .config("spark.sql.shuffle.partitions", "2")
            .config("spark.driver.memory", "1g")
            .config("spark.driver.bindAddress", "127.0.0.1")
            .config("spark.driver.host", "127.0.0.1")
            .config("spark.network.timeout", "120s")
            .getOrCreate()
        )
        _spark.sparkContext.setLogLevel("WARN")
    return _spark


def stop_spark() -> None:
    """Stop the shared SparkSession (used by tests / teardown)."""
    global _spark
    if _spark is not None:
        _spark.stop()
        _spark = None


class SparkRidgeModel:
    """Wrap a fitted Spark ``PipelineModel``.

    Exposes ``predict(pandas.DataFrame) -> numpy.ndarray`` so callers do
    not need to know anything about Spark.
    """

    def __init__(self, pipeline_model: PipelineModel, target: str):
        self._model = pipeline_model
        self.target = target

    def predict(self, data: pd.DataFrame) -> np.ndarray:
        """Predict on a DataFrame carrying ``FEATURE_COLUMNS``.

        Returns a 1-D float ndarray aligned with ``data`` rows.
        """
        X = data[FEATURE_COLUMNS]
        sdf = _to_spark_df(get_spark(), X)
        rows = self._model.transform(sdf).select(_PREDICTION_COL).collect()
        return np.array([float(r[_PREDICTION_COL]) for r in rows], dtype=float)


def _to_spark_df(spark: SparkSession, X: pd.DataFrame):
    """Convert a feature-only pandas DataFrame to a Spark DataFrame.

    Adds the string-cast ``station_id_str`` column required by
    ``StringIndexer`` (which needs a string input column).
    """
    sdf = spark.createDataFrame(X)
    return sdf.withColumn(_STATION_STR_COL, col("station_id").cast(StringType()))


def _build_pipeline() -> Pipeline:
    """Build the Spark ML pipeline equivalent to the sklearn Ridge pipeline.

    Layout mirrors the original ``ColumnTransformer``:
        categorical (station_id) -> OneHotEncoder (no drop)
        numeric                   -> StandardScaler
        concatenated              -> Ridge
    """
    indexer = StringIndexer(
        inputCol=_STATION_STR_COL,
        outputCol=_STATION_IDX_COL,
        handleInvalid="keep",
    )
    encoder = OneHotEncoder(
        inputCol=_STATION_IDX_COL,
        outputCol=_STATION_VEC_COL,
        dropLast=False,  # matches sklearn OneHotEncoder(drop=None)
    )
    num_assembler = VectorAssembler(inputCols=NUMERIC_COLS, outputCol=_NUM_VEC_COL)
    scaler = StandardScaler(
        inputCol=_NUM_VEC_COL,
        outputCol=_NUM_SCALED_COL,
        withMean=True,
        withStd=True,
    )
    final_assembler = VectorAssembler(
        inputCols=[_STATION_VEC_COL, _NUM_SCALED_COL],
        outputCol="features",
    )
    regressor = LinearRegression(
        featuresCol="features",
        labelCol=_LABEL_COL,
        predictionCol=_PREDICTION_COL,
        regParam=RIDGE_REG_PARAM,
        elasticNetParam=RIDGE_ELASTIC_NET,
        fitIntercept=True,
        standardization=False,  # numeric features already scaled above
    )
    return Pipeline(
        stages=[indexer, encoder, num_assembler, scaler, final_assembler, regressor]
    )


def fit_spark_ridge(
    train: pd.DataFrame,
    target: str = "load_kw",
    spark: SparkSession | None = None,
) -> SparkRidgeModel:
    """Fit a Spark Ridge model on a training split.

    Parameters
    ----------
    train
        Supervised split with feature columns and a target column
        (``target_load_kw`` or ``target_busy_count``).
    target
        ``"load_kw"`` or ``"busy_count"``.

    Returns
    -------
    A fitted :class:`SparkRidgeModel`.
    """
    spark = spark or get_spark()
    target_col = f"target_{target}"

    X = train[FEATURE_COLUMNS]
    pdf = X.copy()
    pdf[_LABEL_COL] = train[target_col].astype(float)

    sdf = spark.createDataFrame(pdf).withColumn(
        _STATION_STR_COL, col("station_id").cast(StringType())
    )

    pipeline = _build_pipeline()
    model = pipeline.fit(sdf)
    return SparkRidgeModel(model, target)


def save_spark_model(model: SparkRidgeModel, path: str | Path) -> None:
    """Persist a fitted Spark model to a directory (Spark-native format).

    Also writes a small sidecar file (``<path>.target``) so the target name
    survives a save/load round-trip.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    model._model.write().overwrite().save(str(path))
    Path(str(path) + ".target").write_text(model.target, encoding="utf-8")


def load_spark_model(
    path: str | Path,
    spark: SparkSession | None = None,
) -> SparkRidgeModel:
    """Load a previously saved Spark model."""
    spark = spark or get_spark()
    model = PipelineModel.load(str(path))
    target_file = Path(str(path) + ".target")
    target = (
        target_file.read_text(encoding="utf-8").strip()
        if target_file.exists()
        else "unknown"
    )
    return SparkRidgeModel(model, target=target)
