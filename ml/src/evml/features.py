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
    # the target is the row at observed_at = origin + horizon hours.
    # Columns are converted to numpy arrays so per-row access is O(1);
    # repeated DataFrame .loc lookups made the naive version quadratic.
    supervised_rows = []
    horizon_deltas = [pd.Timedelta(hours=h) for h in HORIZONS]
    for sid, grp in enriched.groupby("station_id"):
        g = grp.sort_values("observed_at").reset_index(drop=True)

        ts_list = g["observed_at"].tolist()
        ts_to_idx = {ts: i for i, ts in enumerate(ts_list)}

        sid_int = int(sid)
        pile = g["pile_count"].to_numpy()
        rated = g["rated_power_kw"].to_numpy()
        is_holiday = g["is_holiday"].to_numpy()
        temperature = g["temperature_c"].to_numpy()
        load_lag_1 = g["load_lag_1"].to_numpy()
        load_lag_24 = g["load_lag_24"].to_numpy()
        load_roll_6 = g["load_roll_6"].to_numpy()
        load_roll_24 = g["load_roll_24"].to_numpy()
        busy_lag_1 = g["busy_lag_1"].to_numpy()
        busy_lag_24 = g["busy_lag_24"].to_numpy()
        busy_roll_6 = g["busy_roll_6"].to_numpy()
        busy_roll_24 = g["busy_roll_24"].to_numpy()
        seasonal_load = g["seasonal_load_kw"].to_numpy()
        seasonal_busy = g["seasonal_busy_count"].to_numpy()
        load_kw = g["load_kw"].to_numpy()
        busy_count = g["busy_count"].to_numpy()
        source_max_at = g["source_max_at"].to_numpy()

        for i in range(24, len(ts_list)):
            origin_ts = ts_list[i]
            for h, delta in zip(HORIZONS, horizon_deltas):
                target_ts = origin_ts + delta
                j = ts_to_idx.get(target_ts)
                if j is None:
                    continue

                row = {
                    "station_id": sid_int,
                    "origin_at": origin_ts,
                    "target_at": target_ts,
                    "horizon_h": h,
                    "pile_count": int(pile[i]),
                    "rated_power_kw": float(rated[i]),
                    # Target calendar features (known at forecast time)
                    "target_hour_sin": _hour_sin(target_ts.hour),
                    "target_hour_cos": _hour_cos(target_ts.hour),
                    "target_dow_sin": _dow_sin(target_ts.dayofweek),
                    "target_dow_cos": _dow_cos(target_ts.dayofweek),
                    "target_is_weekend": int(target_ts.dayofweek >= 5),
                    "target_is_holiday": int(is_holiday[j]),
                    "target_temperature_c": float(temperature[j]),
                    # Lag/rolling features from origin
                    "load_lag_1": float(load_lag_1[i]),
                    "load_lag_24": float(load_lag_24[i]),
                    "load_roll_6": float(load_roll_6[i]),
                    "load_roll_24": float(load_roll_24[i]),
                    "busy_lag_1": float(busy_lag_1[i]),
                    "busy_lag_24": float(busy_lag_24[i]),
                    "busy_roll_6": float(busy_roll_6[i]),
                    "busy_roll_24": float(busy_roll_24[i]),
                    # Seasonal baseline
                    "seasonal_load_kw": float(seasonal_load[i]),
                    "seasonal_busy_count": float(seasonal_busy[i]),
                    # Targets
                    "target_load_kw": float(load_kw[j]),
                    "target_busy_count": int(busy_count[j]),
                    # Source tracking
                    "source_max_at": source_max_at[i],
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
