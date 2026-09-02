"""Command-line interface for the ML forecasting pipeline.

Usage:
    PYTHONPATH=ml/src python3 -m evml.cli run-all --history <csv> --cutoff <ts> --output-dir <dir>
    PYTHONPATH=ml/src python3 -m evml.cli publish --candidate <json> --host <h> --port <p> --last-good <path>
    PYTHONPATH=ml/src python3 -m evml.cli train --history <csv> --output-dir <dir>
    PYTHONPATH=ml/src python3 -m evml.cli forecast --history <csv> --cutoff <ts> --output-dir <dir>
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import pandas as pd

from .last_good import save_last_good
from .pipeline import run_pipeline
from .publisher import publish_forecast
from .types import ForecastRun


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="evml",
        description="EV charging station load forecasting CLI",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # run-all
    p_runall = sub.add_parser("run-all", help="Train models and generate forecast")
    p_runall.add_argument("--history", required=True, help="Path to station_hourly_history.csv")
    p_runall.add_argument("--cutoff", required=True, help="Data cutoff ISO timestamp")
    p_runall.add_argument("--generated-at", default=None, help="Model generation timestamp")
    p_runall.add_argument("--output-dir", required=True, help="Output directory for artifacts")
    p_runall.add_argument("--seed", type=int, default=20260901, help="Random seed")

    # train
    p_train = sub.add_parser("train", help="Train models only")
    p_train.add_argument("--history", required=True, help="Path to station_hourly_history.csv")
    p_train.add_argument("--output-dir", required=True, help="Output directory for artifacts")
    p_train.add_argument("--seed", type=int, default=20260901, help="Random seed")

    # forecast
    p_forecast = sub.add_parser("forecast", help="Generate forecast from existing models")
    p_forecast.add_argument("--history", required=True, help="Path to station_hourly_history.csv")
    p_forecast.add_argument("--cutoff", required=True, help="Data cutoff ISO timestamp")
    p_forecast.add_argument("--generated-at", default=None, help="Model generation timestamp")
    p_forecast.add_argument("--output-dir", required=True, help="Output directory with models")

    # publish
    p_publish = sub.add_parser("publish", help="Publish forecast to server")
    p_publish.add_argument("--candidate", required=True, help="Path to forecast_candidate.json")
    p_publish.add_argument("--host", required=True, help="Server host")
    p_publish.add_argument("--port", type=int, required=True, help="Server port")
    p_publish.add_argument("--token", default="", help="ML service token")
    p_publish.add_argument("--last-good", required=True, help="Path for last_good file")

    args = parser.parse_args(argv)

    if args.command == "run-all":
        return _cmd_run_all(args)
    elif args.command == "train":
        return _cmd_train(args)
    elif args.command == "forecast":
        return _cmd_forecast(args)
    elif args.command == "publish":
        return _cmd_publish(args)
    else:
        parser.print_help()
        return 1


def _parse_ts(s: str) -> pd.Timestamp:
    """Parse a timezone-aware ISO timestamp."""
    ts = pd.Timestamp(s)
    if ts.tz is None:
        ts = ts.tz_localize("+08:00")
    return ts


def _cmd_run_all(args) -> int:
    cutoff = _parse_ts(args.cutoff)
    generated_at = _parse_ts(args.generated_at) if args.generated_at else None
    run = run_pipeline(
        history_path=args.history,
        cutoff=cutoff,
        output_dir=args.output_dir,
        seed=args.seed,
        generated_at=generated_at,
    )
    print(f"run_id: {run.run_id}")
    print(f"records: {len(run.records)}")
    print(f"model_version: {run.model_version}")
    print(f"metrics: {len(run.metrics)} rows")
    return 0


def _cmd_train(args) -> int:
    """Train models only (using a default cutoff from data)."""
    from .repository import load_history
    from .features import build_supervised, split_supervised
    from .ridge import fit_ridge, evaluate_model_on_split
    from .metrics import evaluate_predictions
    from .pipeline import _atomic_write_joblib, _atomic_write_json, _metrics_to_json

    history = load_history(args.history)
    supervised = build_supervised(history)
    split = split_supervised(supervised)

    load_model = fit_ridge(split.train, "load_kw")
    busy_model = fit_ridge(split.train, "busy_count")

    load_val = evaluate_model_on_split(load_model, split.validation, "load_kw", "ridge")
    busy_val = evaluate_model_on_split(busy_model, split.validation, "busy_count", "ridge")

    load_baseline = evaluate_predictions(
        truth=split.validation["target_load_kw"].values,
        predictions=split.validation["seasonal_load_kw"].values,
        model="seasonal_naive",
        target="load_kw",
        horizon_col=split.validation["horizon_h"].values,
    )
    busy_baseline = evaluate_predictions(
        truth=split.validation["target_busy_count"].values,
        predictions=split.validation["seasonal_busy_count"].values,
        model="seasonal_naive",
        target="busy_count",
        horizon_col=split.validation["horizon_h"].values,
    )

    all_metrics = load_val + busy_val + load_baseline + busy_baseline

    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    _atomic_write_joblib(out / "model_load.joblib", load_model)
    _atomic_write_joblib(out / "model_busy.joblib", busy_model)
    _atomic_write_json(out / "metrics.json", _metrics_to_json(all_metrics))

    print("Models trained and saved.")
    return 0


def _cmd_forecast(args) -> int:
    """Generate forecast from existing models."""
    import joblib
    from .repository import load_history
    from .ridge import choose_champion
    from .forecast import build_forecast_run
    from .pipeline import _atomic_write_json, _run_to_candidate, _run_summary

    history = load_history(args.history)
    cutoff = _parse_ts(args.cutoff)
    generated_at = _parse_ts(args.generated_at) if args.generated_at else None

    out = Path(args.output_dir)
    load_model = joblib.load(out / "model_load.joblib")
    busy_model = joblib.load(out / "model_busy.joblib")

    # For simplicity, use ridge as champion (in production, check metrics)
    load_champion = "ridge"
    busy_champion = "ridge"

    run = build_forecast_run(
        history=history,
        cutoff=cutoff,
        load_model=load_model,
        busy_model=busy_model,
        load_champion=load_champion,
        busy_champion=busy_champion,
        generated_at=generated_at,
    )

    _atomic_write_json(out / "forecast_candidate.json", _run_to_candidate(run))
    _atomic_write_json(out / "run_summary.json", _run_summary(run, load_champion, busy_champion))

    print(f"Forecast generated: {run.run_id}, {len(run.records)} records")
    return 0


def _cmd_publish(args) -> int:
    """Publish a forecast candidate to the server."""
    candidate_path = Path(args.candidate)
    with open(candidate_path, "r", encoding="utf-8") as f:
        candidate = json.load(f)

    # Build a ForecastRun-like payload
    publish_payload = {
        "runId": candidate["runId"],
        "generatedAt": candidate["generatedAt"],
        "dataCutoff": candidate["dataCutoff"],
        "modelVersion": candidate["modelVersion"],
        "records": candidate["records"],
    }

    # Build a minimal ForecastRun for publish_forecast
    from .types import ForecastRecord, ForecastRun
    records = tuple(
        ForecastRecord(
            station_id=r["stationId"],
            forecast_at=r["forecastAt"],
            horizon_h=r["horizonH"],
            predicted_load_kw=r["predictedLoadKw"],
            predicted_busy_count=r["predictedBusyCount"],
            predicted_idle_count=r["predictedIdleCount"],
            congestion_level=r["congestionLevel"],
            is_peak=r["isPeak"],
        )
        for r in candidate["records"]
    )
    run = ForecastRun(
        run_id=candidate["runId"],
        generated_at=candidate["generatedAt"],
        data_cutoff=candidate["dataCutoff"],
        model_version=candidate["modelVersion"],
        records=records,
        metrics=(),
    )

    receipt = publish_forecast(
        host=args.host,
        port=args.port,
        token=args.token,
        run=run,
    )

    if receipt.ok and receipt.accepted_count == 144:
        # Save last-good atomically
        save_last_good(args.last_good, publish_payload)
        print(f"Published: runId={receipt.run_id}, accepted={receipt.accepted_count}, snapshotReady={receipt.snapshot_ready}")
        print(f"Last-good saved to: {args.last_good}")
        return 0
    else:
        print(f"Publish failed: ok={receipt.ok}, code={receipt.code}, message={receipt.message}, accepted={receipt.accepted_count}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
