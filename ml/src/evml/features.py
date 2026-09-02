"""Chronological split and supervised horizon sample construction.

This module turns the raw 90-day hourly history into a leakage-safe
supervised dataset where each row represents a forecast origin and a
target horizon (1..24).  Feature columns derived from *target* time
describe known calendar only, never target measurements.
"""

from __future__ import annotations

from dataclasses import dataclass

import pandas as pd

HORIZONS = list(range(1, 25))  # 1..24
TRAIN_DAYS = 62
VALIDATION_DAYS = 14
TEST_DAYS = 14


@dataclass(frozen=True)
class TimeSplit:
    """Chronological train / validation / test split."""

    train: pd.DataFrame
    validation: pd.DataFrame
    test: pd.DataFrame


def chronological_split(history: pd.DataFrame) -> TimeSplit:
    """Split history into 62/14/14-day chronological segments.

    The split is based on ``target_at`` (the forecast target timestamp),
    not ``origin_at``, to ensure no target leakage across splits.
    """
    # Build supervised first to get target_at
    supervised = build_supervised(history)
    return split_supervised(supervised)


def split_supervised(supervised: pd.DataFrame) -> TimeSplit:
    """Split an already-supervised DataFrame chronologically by target_at."""
    supervised = supervised.sort_values("target_at").reset_index(drop=True)
    unique_target_days = supervised["target_at"].dt.tz_convert("+08:00").dt.date.unique()

    train_days = unique_target_days[:TRAIN_DAYS]
    val_days = unique_target_days[TRAIN_DAYS : TRAIN_DAYS + VALIDATION_DAYS]
    test_days = unique_target_days[TRAIN_DAYS + VALIDATION_DAYS :]

    train = supervised[supervised["target_at"].dt.tz_convert("+08:00").dt.date.isin(train_days)]
    val = supervised[supervised["target_at"].dt.tz_convert("+08:00").dt.date.isin(val_days)]
    test = supervised[supervised["target_at"].dt.tz_convert("+08:00").dt.date.isin(test_days)]

    return TimeSplit(train=train, validation=val, test=test)


def build_supervised(history: pd.DataFrame) -> pd.DataFrame:
    """Build supervised horizon samples from raw hourly history.

    For each station and each possible forecast origin timestamp ``t``,
    and for each horizon ``h`` in 1..24, we create a row with:
    - origin_at = t
    - target_at = t + h hours
    - target_load_kw, target_busy_count = the actual values at t+h
    - lag/rolling features computed only from data at or before t

    Parameters
    ----------
    history
        Raw history DataFrame from ``load_history``.

    Returns
    -------
    DataFrame with columns described in the plan, sorted by
    (station_id, target_at, horizon_h).
    """
    history = history.sort_values(["station_id", "observed_at"]).reset_index(drop=True)

    # For each station, build lag/rolling features on the raw time series
    rows = []
    for sid, grp in history.groupby("station_id"):
        g = grp.sort_values("observed_at").reset_index(drop=True).copy()

        # Lag features (per station, time-ordered)
        g["load_lag_1"] = g["load_kw"].shift(1)
        g["load_lag_24"] = g["load_kw"].shift(24)
        g["busy_lag_1"] = g["busy_count"].shift(1)
        g["busy_lag_24"] = g["busy_count"].shift(24)

        # Rolling features: compute after shift(1) to prevent leakage
        g["load_roll_6"] = g["load_kw"].shift(1).rolling(6).mean()
        g["load_roll_24"] = g["load_kw"].shift(1).rolling(24).mean()
        g["busy_roll_6"] = g["busy_count"].shift(1).rolling(6).mean()
        g["busy_roll_24"] = g["busy_count"].shift(1).rolling(24).mean()

        # Seasonal-naive: yesterday same hour (shift 24)
        g["seasonal_load_kw"] = g["load_kw"].shift(24)
        g["seasonal_busy_count"] = g["busy_count"].shift(24)

        # Source max timestamp: the latest observed_at used in features
        g["source_max_at"] = g["observed_at"]

        rows.append(g)

    enriched = pd.concat(rows, ignore_index=True)

    # Now build horizon samples: for each (station, origin, horizon) pair,
    # the target is the row at observed_at = origin + horizon hours
    supervised_rows = []
    for sid, grp in enriched.groupby("station_id"):
        g = grp.sort_values("observed_at").reset_index(drop=True).copy()
        ts_list = g["observed_at"].tolist()
        ts_to_idx = {ts: i for i, ts in enumerate(ts_list)}

        for i, origin_ts in enumerate(ts_list):
            # Need at least 24 prior rows for lags
            if i < 24:
                continue
            for h in HORIZONS:
                target_ts = origin_ts + pd.Timedelta(hours=h)
                if target_ts not in ts_to_idx:
                    continue
                j = ts_to_idx[target_ts]

                row = {
                    "station_id": int(sid),
                    "origin_at": origin_ts,
                    "target_at": target_ts,
                    "horizon_h": h,
                    "pile_count": int(g.loc[i, "pile_count"]),
                    "rated_power_kw": float(g.loc[i, "rated_power_kw"]),
                    # Target calendar features (known at forecast time)
                    "target_hour_sin": _hour_sin(target_ts.hour),
                    "target_hour_cos": _hour_cos(target_ts.hour),
                    "target_dow_sin": _dow_sin(target_ts.dayofweek),
                    "target_dow_cos": _dow_cos(target_ts.dayofweek),
                    "target_is_weekend": int(target_ts.dayofweek >= 5),
                    "target_is_holiday": int(g.loc[j, "is_holiday"]),
                    "target_temperature_c": float(g.loc[j, "temperature_c"]),
                    # Lag/rolling features from origin
                    "load_lag_1": float(g.loc[i, "load_lag_1"]),
                    "load_lag_24": float(g.loc[i, "load_lag_24"]),
                    "load_roll_6": float(g.loc[i, "load_roll_6"]),
                    "load_roll_24": float(g.loc[i, "load_roll_24"]),
                    "busy_lag_1": float(g.loc[i, "busy_lag_1"]),
                    "busy_lag_24": float(g.loc[i, "busy_lag_24"]),
                    "busy_roll_6": float(g.loc[i, "busy_roll_6"]),
                    "busy_roll_24": float(g.loc[i, "busy_roll_24"]),
                    # Seasonal baseline
                    "seasonal_load_kw": float(g.loc[i, "seasonal_load_kw"]),
                    "seasonal_busy_count": float(g.loc[i, "seasonal_busy_count"]),
                    # Targets
                    "target_load_kw": float(g.loc[j, "load_kw"]),
                    "target_busy_count": int(g.loc[j, "busy_count"]),
                    # Source tracking
                    "source_max_at": g.loc[i, "source_max_at"],
                }
                supervised_rows.append(row)

    supervised = pd.DataFrame(supervised_rows)
    supervised = supervised.sort_values(["station_id", "target_at", "horizon_h"]).reset_index(drop=True)
    return supervised


def _hour_sin(hour: int) -> float:
    import math
    return math.sin(2 * math.pi * hour / 24)


def _hour_cos(hour: int) -> float:
    import math
    return math.cos(2 * math.pi * hour / 24)


def _dow_sin(dow: int) -> float:
    import math
    return math.sin(2 * math.pi * dow / 7)


def _dow_cos(dow: int) -> float:
    import math
    return math.cos(2 * math.pi * dow / 7)
