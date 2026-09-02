"""Reproducible end-to-end training and forecast pipeline."""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path

import joblib
import numpy as np
import pandas as pd

from .baseline import predict_seasonal_naive
from .features import build_supervised, split_supervised
from .forecast import build_forecast_run
from .metrics import evaluate_predictions
from .repository import load_history
from .ridge import choose_champion, evaluate_model_on_split, fit_ridge, predict_ridge
from .types import ForecastRun, MetricRow


def run_pipeline(
    history_path: str | Path,
    cutoff: pd.Timestamp,
    output_dir: str | Path,
    seed: int = 20260901,
    generated_at: pd.Timestamp | None = None,
) -> ForecastRun:
    """Run the full training and forecast pipeline.

    Steps:
    1. Load and validate history CSV.
    2. Build supervised horizon samples.
    3. Chronological 62/14/14 split.
    4. Train Ridge models for load_kw and busy_count.
    5. Evaluate Ridge and seasonal-naive baseline on validation set.
    6. Choose champion per target (honest fallback to baseline).
    7. Evaluate champion on test set.
    8. Build 144-record forecast run.
    9. Persist models, metrics, candidate forecast and run summary.

    Parameters
    ----------
    history_path
        Path to station_hourly_history.csv.
    cutoff
        Data cutoff timestamp (forecast origin).
    output_dir
        Directory for artifacts.
    seed
        Random seed (unused by Ridge, kept for interface compatibility).
    generated_at
        Optional model generation timestamp.

    Returns
    -------
    ForecastRun with 144 records and metrics.
    """
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # 1. Load history
    history = load_history(history_path)

    # 2. Build supervised samples
    supervised = build_supervised(history)

    # 3. Chronological split
    split = split_supervised(supervised)

    # 4. Train Ridge models
    load_model = fit_ridge(split.train, target="load_kw")
    busy_model = fit_ridge(split.train, target="busy_count")

    # 5. Evaluate on validation set
    # Ridge metrics
    load_ridge_val = evaluate_model_on_split(load_model, split.validation, "load_kw", "ridge")
    busy_ridge_val = evaluate_model_on_split(busy_model, split.validation, "busy_count", "ridge")

    # Baseline (seasonal naive) metrics on validation
    load_baseline_val = evaluate_predictions(
        truth=split.validation["target_load_kw"].values,
        predictions=split.validation["seasonal_load_kw"].values,
        model="seasonal_naive",
        target="load_kw",
        horizon_col=split.validation["horizon_h"].values,
    )
    busy_baseline_val = evaluate_predictions(
        truth=split.validation["target_busy_count"].values,
        predictions=split.validation["seasonal_busy_count"].values,
        model="seasonal_naive",
        target="busy_count",
        horizon_col=split.validation["horizon_h"].values,
    )

    # 6. Choose champion
    load_champion = choose_champion(load_ridge_val, load_baseline_val, "load_kw")
    busy_champion = choose_champion(busy_ridge_val, busy_baseline_val, "busy_count")

    # 7. Evaluate champion on test set
    if load_champion == "ridge":
        load_test_metrics = evaluate_model_on_split(load_model, split.test, "load_kw", "ridge")
    else:
        load_test_metrics = evaluate_predictions(
            truth=split.test["target_load_kw"].values,
            predictions=split.test["seasonal_load_kw"].values,
            model="seasonal_naive",
            target="load_kw",
            horizon_col=split.test["horizon_h"].values,
        )

    if busy_champion == "ridge":
        busy_test_metrics = evaluate_model_on_split(busy_model, split.test, "busy_count", "ridge")
    else:
        busy_test_metrics = evaluate_predictions(
            truth=split.test["target_busy_count"].values,
            predictions=split.test["seasonal_busy_count"].values,
            model="seasonal_naive",
            target="busy_count",
            horizon_col=split.test["horizon_h"].values,
        )

    # Also evaluate baseline on test for comparison
    load_baseline_test = evaluate_predictions(
        truth=split.test["target_load_kw"].values,
        predictions=split.test["seasonal_load_kw"].values,
        model="seasonal_naive",
        target="load_kw",
        horizon_col=split.test["horizon_h"].values,
    )
    busy_baseline_test = evaluate_predictions(
        truth=split.test["target_busy_count"].values,
        predictions=split.test["seasonal_busy_count"].values,
        model="seasonal_naive",
        target="busy_count",
        horizon_col=split.test["horizon_h"].values,
    )

    all_metrics = (
        load_ridge_val + busy_ridge_val
        + load_baseline_val + busy_baseline_val
        + load_test_metrics + busy_test_metrics
        + load_baseline_test + busy_baseline_test
    )

    # 8. Build forecast run
    run = build_forecast_run(
        history=history,
        cutoff=cutoff,
        load_model=load_model if load_champion == "ridge" else None,
        busy_model=busy_model if busy_champion == "ridge" else None,
        load_champion=load_champion,
        busy_champion=busy_champion,
        generated_at=generated_at,
    )

    run = ForecastRun(
        run_id=run.run_id,
        generated_at=run.generated_at,
        data_cutoff=run.data_cutoff,
        model_version=run.model_version,
        records=run.records,
        metrics=all_metrics,
    )

    # 9. Persist artifacts atomically
    _atomic_write_json(output_dir / "metrics.json", _metrics_to_json(all_metrics))
    _atomic_write_json(output_dir / "forecast_candidate.json", _run_to_candidate(run))
    _atomic_write_json(output_dir / "run_summary.json", _run_summary(run, load_champion, busy_champion))
    _atomic_write_joblib(output_dir / "model_load.joblib", load_model)
    _atomic_write_joblib(output_dir / "model_busy.joblib", busy_model)

    return run


def _atomic_write_json(path: Path, data: dict | list) -> None:
    """Write JSON atomically via temp file + os.replace."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, sort_keys=True, indent=2)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def _atomic_write_joblib(path: Path, obj) -> None:
    """Write joblib atomically via temp file + os.replace."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    joblib.dump(obj, tmp)
    os.replace(tmp, path)


def _metrics_to_json(metrics: tuple[MetricRow, ...]) -> list[dict]:
    return [
        {
            "model": m.model,
            "target": m.target,
            "horizon_h": m.horizon_h,
            "mae": round(m.mae, 6),
            "wape": round(m.wape, 6) if m.wape is not None else None,
        }
        for m in metrics
    ]


def _run_to_candidate(run: ForecastRun) -> dict:
    """Build the publishable candidate JSON (no metrics)."""
    return {
        "runId": run.run_id,
        "generatedAt": run.generated_at,
        "dataCutoff": run.data_cutoff,
        "modelVersion": run.model_version,
        "records": [
            {
                "stationId": r.station_id,
                "forecastAt": r.forecast_at,
                "horizonH": r.horizon_h,
                "predictedLoadKw": r.predicted_load_kw,
                "predictedBusyCount": r.predicted_busy_count,
                "predictedIdleCount": r.predicted_idle_count,
                "congestionLevel": r.congestion_level,
                "isPeak": r.is_peak,
            }
            for r in run.records
        ],
    }


def _run_summary(run: ForecastRun, load_champion: str, busy_champion: str) -> dict:
    return {
        "runId": run.run_id,
        "generatedAt": run.generated_at,
        "dataCutoff": run.data_cutoff,
        "modelVersion": run.model_version,
        "recordCount": len(run.records),
        "loadChampion": load_champion,
        "busyChampion": busy_champion,
        "metrics": _metrics_to_json(run.metrics),
    }
