"""Ridge regression model fitting and honest champion selection."""

from __future__ import annotations

import numpy as np
import pandas as pd
from sklearn.compose import ColumnTransformer
from sklearn.linear_model import Ridge
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import OneHotEncoder, StandardScaler

from .metrics import evaluate_predictions

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


def fit_ridge(train: pd.DataFrame, target: str = "load_kw") -> Pipeline:
    """Fit a Ridge regression pipeline.

    Parameters
    ----------
    train
        Training split with feature columns and a target column
        (``target_load_kw`` or ``target_busy_count``).
    target
        ``"load_kw"`` or ``"busy_count"``.

    Returns
    -------
    Fitted sklearn Pipeline (ColumnTransformer + Ridge).
    """
    target_col = f"target_{target}"
    X = train[FEATURE_COLUMNS].copy()
    y = train[target_col].copy()

    preprocessor = ColumnTransformer(
        transformers=[
            ("cat", OneHotEncoder(drop=None, sparse_output=False), CATEGORICAL_COLS),
            ("num", StandardScaler(), NUMERIC_COLS),
        ]
    )

    pipeline = Pipeline(
        steps=[
            ("preprocessor", preprocessor),
            ("regressor", Ridge(alpha=1.0)),
        ]
    )
    pipeline.fit(X, y)
    return pipeline


def predict_ridge(model: Pipeline, data: pd.DataFrame) -> np.ndarray:
    """Predict using a fitted Ridge pipeline."""
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
    model: Pipeline,
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
