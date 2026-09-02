"""Immutable output types for the ML forecasting pipeline."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal


@dataclass(frozen=True)
class ForecastRecord:
    """One forecast row for a single station and horizon."""

    station_id: int
    forecast_at: str
    horizon_h: int
    predicted_load_kw: float
    predicted_busy_count: int
    predicted_idle_count: int
    congestion_level: Literal["low", "medium", "high"]
    is_peak: bool


@dataclass(frozen=True)
class MetricRow:
    """One metric evaluation row comparing model performance."""

    model: str
    target: Literal["load_kw", "busy_count"]
    horizon_h: Literal[1, 6, 24]
    mae: float
    wape: float | None


@dataclass(frozen=True)
class ForecastRun:
    """A complete forecast run with 144 records and evaluation metrics."""

    run_id: str
    generated_at: str
    data_cutoff: str
    model_version: str
    records: tuple[ForecastRecord, ...]
    metrics: tuple[MetricRow, ...]
