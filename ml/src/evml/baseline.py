"""Seasonal-naive baseline predictions."""

from __future__ import annotations

import pandas as pd


def predict_seasonal_naive(frame: pd.DataFrame) -> pd.Series:
    """Return seasonal-naive predictions for each row in ``frame``.

    The seasonal-naive prediction is the value from 24 hours ago
    (yesterday same hour), stored in ``seasonal_load_kw`` or
    ``seasonal_busy_count`` columns.

    Parameters
    ----------
    frame
        Supervised DataFrame with ``seasonal_load_kw`` and
        ``seasonal_busy_count`` columns.

    Returns
    -------
    Series of predictions aligned with ``frame`` rows.
    """
    if "seasonal_load_kw" not in frame.columns:
        raise ValueError("frame missing seasonal_load_kw column")
    return frame["seasonal_load_kw"].copy()
