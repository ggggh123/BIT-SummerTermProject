"""Ridge regression model fitting and honest champion selection.

Phase 2: the model is trained with Spark MLlib (see ``spark_model``).
The public API (``fit_ridge`` / ``predict_ridge`` / ``choose_champion`` /
``evaluate_model_on_split``) is unchanged, so downstream consumers
(features, forecast, pipeline, CLI, tests) are unaffected.
"""

from __future__ import annotations

import numpy as np
import pandas as pd

from .metrics import evaluate_predictions
from .spark_model import (
    CATEGORICAL_COLS,
    FEATURE_COLUMNS,
    NUMERIC_COLS,
    SparkRidgeModel,
    fit_spark_ridge,
)

# FEATURE_COLUMNS / CATEGORICAL_COLS / NUMERIC_COLS are re-exported here for
# backward compatibility: forecast.py and tests import them from evml.ridge.


def fit_ridge(train: pd.DataFrame, target: str = "load_kw") -> SparkRidgeModel:
    """Fit a Ridge regression model (Spark MLlib LinearRegression, L2).

    Parameters
    ----------
    train
        Training split with feature columns and a target column
        (``target_load_kw`` or ``target_busy_count``).
    target
        ``"load_kw"`` or ``"busy_count"``.

    Returns
    -------
    A fitted :class:`SparkRidgeModel` exposing ``predict(pandas.DataFrame)``.
    """
    return fit_spark_ridge(train, target)


def predict_ridge(model: SparkRidgeModel, data: pd.DataFrame) -> np.ndarray:
    """Predict using a fitted Ridge model."""
    X = data[FEATURE_COLUMNS].copy()
    return model.predict(X)


def choose_champion(
    ridge_metrics: tuple,
    baseline_metrics: tuple,
    target: str,
) -> str:
    """Select the better model based on validation MAE.

    Compares average validation MAE across horizons 1/6/24.
    Does not use test metrics for selection.

    Returns
    -------
    ``"ridge"`` or ``"seasonal_naive"``.
    """
    ridge_avg = np.mean([m.mae for m in ridge_metrics])
    baseline_avg = np.mean([m.mae for m in baseline_metrics])
    return "ridge" if ridge_avg < baseline_avg else "seasonal_naive"


def evaluate_model_on_split(
    model: SparkRidgeModel,
    split: pd.DataFrame,
    target: str,
    model_name: str = "ridge",
) -> tuple:
    """Evaluate a fitted model on a data split.

    Returns
    -------
    Tuple of MetricRow for horizons 1, 6, 24.
    """
    predictions = predict_ridge(model, split)
    truth = split[f"target_{target}"].values
    horizon_col = split["horizon_h"].values
    return evaluate_predictions(
        truth=truth,
        predictions=predictions,
        model=model_name,
        target=target,
        horizon_col=horizon_col,
    )
