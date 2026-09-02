"""Build a physically bounded 144-record forecast run."""

from __future__ import annotations

import hashlib
import math
from dataclasses import dataclass

import numpy as np
import pandas as pd

from .baseline import predict_seasonal_naive
from .ridge import predict_ridge
from .types import ForecastRecord, ForecastRun

SIX_STATIONS = (1, 2, 3, 4, 5, 6)
ALL_HORIZONS = list(range(1, 25))  # 1..24


@dataclass(frozen=True)
class StationCapacity:
    """Physical bounds for one station."""

    station_id: int
    pile_count: int
    rated_power_kw: float


def _congestion_level(busy: int, pile_count: int) -> str:
    """Classify congestion based on busy ratio."""
    ratio = busy / pile_count if pile_count > 0 else 0
    if ratio >= 0.8:
        return "high"
    if ratio >= 0.5:
        return "medium"
    return "low"


def _compact_ts(ts: pd.Timestamp) -> str:
    """Compact timestamp for run_id: strip separators."""
    s = ts.strftime("%Y%m%dT%H%M%S")
    return s


def _run_id(cutoff: pd.Timestamp, input_hash: str) -> str:
    """Deterministic run ID: forecast-<cutoff compact>-<hash8>."""
    return f"forecast-{_compact_ts(cutoff)}-{input_hash[:8]}"


def _hash_input(history: pd.DataFrame) -> str:
    """SHA-256 of the sorted CSV content for deterministic identity."""
    # Use station_id, observed_at, load_kw, busy_count as identity
    cols = ["station_id", "observed_at", "load_kw", "busy_count"]
    h = hashlib.sha256()
    for _, row in history[cols].sort_values(["station_id", "observed_at"]).iterrows():
        line = f"{row['station_id']},{row['observed_at']},{row['load_kw']},{row['busy_count']}"
        h.update(line.encode("utf-8"))
    return h.hexdigest()


def build_forecast_run(
    history: pd.DataFrame,
    cutoff: pd.Timestamp,
    load_model,
    busy_model,
    load_champion: str,
    busy_champion: str,
    generated_at: pd.Timestamp | None = None,
) -> ForecastRun:
    """Build a 144-record forecast run with physical bounds.

    Parameters
    ----------
    history
        Raw history DataFrame.
    cutoff
        Data cutoff timestamp (forecast origin). Forecasts cover
        cutoff+1h .. cutoff+24h.
    load_model
        Fitted Ridge pipeline for load, or None if using baseline.
    busy_model
        Fitted Ridge pipeline for busy, or None if using baseline.
    load_champion
        ``"ridge"`` or ``"seasonal_naive"``.
    busy_champion
        ``"ridge"`` or ``"seasonal_naive"``.
    generated_at
        Optional generation timestamp. If None, uses cutoff.

    Returns
    -------
    ForecastRun with exactly 144 records.
    """
    if generated_at is None:
        generated_at = cutoff

    # Ensure cutoff is tz-aware +08:00
    if cutoff.tz is None:
        cutoff = cutoff.tz_localize("+08:00")
    else:
        cutoff = cutoff.tz_convert("+08:00")

    if generated_at.tz is None:
        generated_at = generated_at.tz_localize("+08:00")
    else:
        generated_at = generated_at.tz_convert("+08:00")

    # Build per-station capacities
    capacities: dict[int, StationCapacity] = {}
    for sid in SIX_STATIONS:
        grp = history[history["station_id"] == sid]
        capacities[sid] = StationCapacity(
            station_id=sid,
            pile_count=int(grp["pile_count"].iloc[0]),
            rated_power_kw=float(grp["rated_power_kw"].iloc[0]),
        )

    # Get last 24h of history before cutoff for lags
    # For recursive prediction, we need the most recent observed values
    history_sorted = history.sort_values(["station_id", "observed_at"]).copy()
    history_sorted["observed_at"] = pd.to_datetime(history_sorted["observed_at"], utc=True).dt.tz_convert("+08:00")

    # Build per-station recent windows (last 24 rows before cutoff)
    recent: dict[int, pd.DataFrame] = {}
    for sid in SIX_STATIONS:
        grp = history_sorted[history_sorted["station_id"] == sid]
        mask = grp["observed_at"] <= cutoff
        recent[sid] = grp[mask].tail(24).copy()

    # Recursive horizon generation
    # We maintain a rolling window per station of the last 24 values
    # (observed + predicted), used to compute lags and rolling means.
    records: list[ForecastRecord] = []
    # Track per-station load/busy series (extended with predictions)
    station_series: dict[int, dict] = {}
    for sid in SIX_STATIONS:
        r = recent[sid]
        station_series[sid] = {
            "load_kw": r["load_kw"].tolist(),
            "busy_count": r["busy_count"].tolist(),
            "observed_at": r["observed_at"].tolist(),
        }

    # For each horizon 1..24, predict all 6 stations
    horizon_predictions: dict[int, dict[int, dict]] = {}  # h -> sid -> {load, busy}
    for h in ALL_HORIZONS:
        target_ts = cutoff + pd.Timedelta(hours=h)
        horizon_predictions[h] = {}

        for sid in SIX_STATIONS:
            s = station_series[sid]
            n = len(s["load_kw"])

            # Compute lags from the extended series
            load_lag_1 = float(s["load_kw"][-1]) if n >= 1 else 0.0
            load_lag_24 = float(s["load_kw"][-24]) if n >= 24 else float(s["load_kw"][0])
            busy_lag_1 = float(s["busy_count"][-1]) if n >= 1 else 0.0
            busy_lag_24 = float(s["busy_count"][-24]) if n >= 24 else float(s["busy_count"][0])

            # Rolling means from extended series (after shift(1) semantics)
            load_vals = [float(x) for x in s["load_kw"]]
            busy_vals = [float(x) for x in s["busy_count"]]

            load_roll_6 = float(np.mean(load_vals[-6:])) if len(load_vals) >= 6 else float(np.mean(load_vals))
            load_roll_24 = float(np.mean(load_vals[-24:])) if len(load_vals) >= 24 else float(np.mean(load_vals))
            busy_roll_6 = float(np.mean(busy_vals[-6:])) if len(busy_vals) >= 6 else float(np.mean(busy_vals))
            busy_roll_24 = float(np.mean(busy_vals[-24:])) if len(busy_vals) >= 24 else float(np.mean(busy_vals))

            cap = capacities[sid]

            # Seasonal naive: value from 24 hours ago
            if len(load_vals) >= 24:
                seasonal_load = float(load_vals[-24])
                seasonal_busy = float(busy_vals[-24])
            else:
                seasonal_load = float(np.mean(load_vals))
                seasonal_busy = float(np.mean(busy_vals))

            # Predict load
            if load_champion == "ridge" and load_model is not None:
                # Build feature row
                feature_row = _build_feature_row(
                    sid, h, target_ts, cap,
                    load_lag_1, load_lag_24, load_roll_6, load_roll_24,
                    busy_lag_1, busy_lag_24, busy_roll_6, busy_roll_24,
                )
                feat_df = pd.DataFrame([feature_row])
                from .ridge import FEATURE_COLUMNS
                pred_load = float(load_model.predict(feat_df[FEATURE_COLUMNS])[0])
            else:
                pred_load = seasonal_load

            # Predict busy
            if busy_champion == "ridge" and busy_model is not None:
                feature_row = _build_feature_row(
                    sid, h, target_ts, cap,
                    load_lag_1, load_lag_24, load_roll_6, load_roll_24,
                    busy_lag_1, busy_lag_24, busy_roll_6, busy_roll_24,
                )
                feat_df = pd.DataFrame([feature_row])
                pred_busy = float(busy_model.predict(feat_df[FEATURE_COLUMNS])[0])
            else:
                pred_busy = seasonal_busy

            # Physical bounds
            pred_load = max(0.0, min(pred_load, cap.rated_power_kw))
            pred_busy = max(0, min(int(round(pred_busy)), cap.pile_count))
            pred_idle = cap.pile_count - pred_busy

            # Reject nonfinite
            if not math.isfinite(pred_load):
                pred_load = 0.0
            if not math.isfinite(pred_busy):
                pred_busy = 0

            horizon_predictions[h][sid] = {
                "load": pred_load,
                "busy": pred_busy,
                "idle": pred_idle,
                "target_ts": target_ts,
            }

            # Extend the series with predictions for recursive lags
            s["load_kw"].append(pred_load)
            s["busy_count"].append(pred_busy)
            s["observed_at"].append(target_ts)

    # Mark peak: largest consecutive 2-hour window per station
    peak_windows: dict[int, set[int]] = {}  # sid -> set of horizon_h that are peak
    for sid in SIX_STATIONS:
        loads_by_h = {h: horizon_predictions[h][sid]["load"] for h in ALL_HORIZONS}
        # Find the consecutive pair with maximum total load
        best_pair_sum = -1.0
        best_pair = (1, 2)
        for h in range(1, 24):  # h, h+1 are consecutive
            pair_sum = loads_by_h[h] + loads_by_h[h + 1]
            if pair_sum > best_pair_sum:
                best_pair_sum = pair_sum
                best_pair = (h, h + 1)
        peak_windows[sid] = set(best_pair)

    # Build records
    for sid in SIX_STATIONS:
        cap = capacities[sid]
        for h in ALL_HORIZONS:
            p = horizon_predictions[h][sid]
            busy = p["busy"]
            idle = cap.pile_count - busy
            cong = _congestion_level(busy, cap.pile_count)
            is_peak = h in peak_windows[sid]
            records.append(
                ForecastRecord(
                    station_id=sid,
                    forecast_at=p["target_ts"].strftime("%Y-%m-%dT%H:%M:%S+08:00"),
                    horizon_h=h,
                    predicted_load_kw=round(p["load"], 3),
                    predicted_busy_count=busy,
                    predicted_idle_count=idle,
                    congestion_level=cong,  # type: ignore[arg-type]
                    is_peak=is_peak,
                )
            )

    # Sort by station_id, then horizon_h
    records.sort(key=lambda r: (r.station_id, r.horizon_h))

    # Build run identity
    input_hash = _hash_input(history)
    rid = _run_id(cutoff, input_hash)
    model_version = f"load-{load_champion}_busy-{busy_champion}-v1"

    return ForecastRun(
        run_id=rid,
        generated_at=generated_at.strftime("%Y-%m-%dT%H:%M:%S+08:00"),
        data_cutoff=cutoff.strftime("%Y-%m-%dT%H:%M:%S+08:00"),
        model_version=model_version,
        records=tuple(records),
        metrics=(),
    )


def _build_feature_row(
    sid: int,
    horizon: int,
    target_ts: pd.Timestamp,
    cap: StationCapacity,
    load_lag_1: float,
    load_lag_24: float,
    load_roll_6: float,
    load_roll_24: float,
    busy_lag_1: float,
    busy_lag_24: float,
    busy_roll_6: float,
    busy_roll_24: float,
) -> dict:
    """Build a feature dict for a single horizon prediction."""
    return {
        "station_id": sid,
        "horizon_h": horizon,
        "pile_count": cap.pile_count,
        "rated_power_kw": cap.rated_power_kw,
        "target_hour_sin": _hour_sin(target_ts.hour),
        "target_hour_cos": _hour_cos(target_ts.hour),
        "target_dow_sin": _dow_sin(target_ts.dayofweek),
        "target_dow_cos": _dow_cos(target_ts.dayofweek),
        "target_is_weekend": int(target_ts.dayofweek >= 5),
        "target_is_holiday": 0,  # Unknown future, default to non-holiday
        "target_temperature_c": 20.0,  # Unknown future, use reasonable default
        "load_lag_1": load_lag_1,
        "load_lag_24": load_lag_24,
        "load_roll_6": load_roll_6,
        "load_roll_24": load_roll_24,
        "busy_lag_1": busy_lag_1,
        "busy_lag_24": busy_lag_24,
        "busy_roll_6": busy_roll_6,
        "busy_roll_24": busy_roll_24,
    }


def _hour_sin(hour: int) -> float:
    return math.sin(2 * math.pi * hour / 24)


def _hour_cos(hour: int) -> float:
    return math.cos(2 * math.pi * hour / 24)


def _dow_sin(dow: int) -> float:
    return math.sin(2 * math.pi * dow / 7)


def _dow_cos(dow: int) -> float:
    return math.cos(2 * math.pi * dow / 7)
