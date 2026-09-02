"""MAE and WAPE metric computation."""

from __future__ import annotations

import numpy as np
import pandas as pd

from .types import MetricRow

REPORT_HORIZONS = (1, 6, 24)


def mae(actual: np.ndarray, predicted: np.ndarray) -> float:
    """Mean Absolute Error."""
    return float(np.mean(np.abs(np.asarray(actual) - np.asarray(predicted))))


def wape(actual: np.ndarray, predicted: np.ndarray) -> float:
    """Weighted Absolute Percentage Error.

    WAPE = sum(|actual - predicted|) / max(sum(|actual|), 1e-9)
    """
    a = np.asarray(actual, dtype=float)
    p = np.asarray(predicted, dtype=float)
    denom = max(float(np.sum(np.abs(a))), 1e-9)
    return float(np.sum(np.abs(a - p)) / denom)


def evaluate_predictions(
    truth: pd.Series | np.ndarray,
    predictions: pd.Series | np.ndarray,
    model: str,
    target: str = "load_kw",
    horizons: tuple[int, ...] = REPORT_HORIZONS,
    horizon_col: pd.Series | np.ndarray | None = None,
) -> tuple[MetricRow, ...]:
    """Evaluate predictions at horizons 1, 6, 24.

    Parameters
    ----------
    truth
        Actual values.
    predictions
        Predicted values.
    model
        Model name for the metric rows.
    target
        ``"load_kw"`` or ``"busy_count"``.
    horizons
        Which horizon values to report. Default (1, 6, 24).
    horizon_col
        Horizon label for each row. If None, all predictions are
        treated as a single group at horizon 1.

    Returns
    -------
    Tuple of MetricRow, one per horizon.
    """
    truth = np.asarray(truth, dtype=float)
    predictions = np.asarray(predictions, dtype=float)
    if horizon_col is None:
        horizon_col = np.ones(len(truth), dtype=int)
    else:
        horizon_col = np.asarray(horizon_col)

    results = []
    for h in horizons:
        mask = horizon_col == h
        if not mask.any():
            continue
        t = truth[mask]
        p = predictions[mask]
        m = mae(t, p)
        w = wape(t, p) if target == "load_kw" else None
        results.append(
            MetricRow(
                model=model,
                target=target,  # type: ignore[arg-type]
                horizon_h=h,  # type: ignore[arg-type]
                mae=m,
                wape=w,
            )
        )
    return tuple(results)
