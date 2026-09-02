"""Validated CSV loading for station hourly history."""

from __future__ import annotations

from pathlib import Path

import pandas as pd

EXPECTED_COLUMNS = [
    "station_id",
    "observed_at",
    "pile_count",
    "rated_power_kw",
    "temperature_c",
    "is_holiday",
    "busy_count",
    "load_kw",
]


def load_history(path: str | Path) -> pd.DataFrame:
    """Load and validate the station hourly history CSV.

    Parameters
    ----------
    path
        Path to ``station_hourly_history.csv``.

    Returns
    -------
    DataFrame with columns:
        station_id:int, observed_at:datetime64[ns, +08:00],
        pile_count:int, rated_power_kw:float, temperature_c:float,
        is_holiday:int, busy_count:int, load_kw:float.

    Raises
    ------
    ValueError if any structural or physical constraint is violated.
    """
    df = pd.read_csv(path)

    # Column check
    missing = set(EXPECTED_COLUMNS) - set(df.columns)
    if missing:
        raise ValueError(f"Missing columns: {missing}")

    # observed_at must be timezone-aware +08:00
    observed = pd.to_datetime(df["observed_at"], utc=False)
    if observed.dt.tz is None:
        raise ValueError("observed_at timestamps must be timezone-aware (+08:00)")
    # Normalize to +08:00
    df["observed_at"] = observed.dt.tz_convert("+08:00")

    # Type coercion
    df["station_id"] = df["station_id"].astype(int)
    df["pile_count"] = df["pile_count"].astype(int)
    df["rated_power_kw"] = df["rated_power_kw"].astype(float)
    df["temperature_c"] = df["temperature_c"].astype(float)
    df["is_holiday"] = df["is_holiday"].astype(int)
    df["busy_count"] = df["busy_count"].astype(int)
    df["load_kw"] = df["load_kw"].astype(float)

    # No duplicate (station_id, observed_at)
    dup = df.duplicated(subset=["station_id", "observed_at"])
    if dup.any():
        raise ValueError("Duplicate (station_id, observed_at) rows found")

    # No gaps per station: consecutive hourly timestamps
    for sid, group in df.groupby("station_id"):
        ts = group["observed_at"].sort_values()
        diffs = ts.diff().dropna()
        if not diffs.empty and not (diffs == pd.Timedelta(hours=1)).all():
            raise ValueError(
                f"Station {sid} has non-hourly gaps in observed_at"
            )

    # Physical constraints
    if not df["load_kw"].apply(pd.notna).all():
        raise ValueError("load_kw contains NaN")
    if not df["busy_count"].between(0, df["pile_count"]).all():
        raise ValueError("busy_count outside [0, pile_count]")
    if not df["load_kw"].between(0, df["rated_power_kw"]).all():
        raise ValueError("load_kw outside [0, rated_power_kw]")
    if not df["temperature_c"].apply(pd.notna).all():
        raise ValueError("temperature_c contains NaN")

    df = df.sort_values(["station_id", "observed_at"]).reset_index(drop=True)
    return df
