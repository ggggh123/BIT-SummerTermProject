"""Tests for Ridge model training and champion selection (Task 3)."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from evml.repository import load_history
from evml.features import build_supervised, chronological_split
from evml.ridge import (
    fit_ridge,
    predict_ridge,
    choose_champion,
    evaluate_model_on_split,
)
from evml.metrics import evaluate_predictions, MetricRow


class TestRidgeTraining:
    """Test Ridge model fitting and prediction."""

    def test_fit_load_model(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        split = chronological_split(history)
        model = fit_ridge(split.train, target="load_kw")
        assert model is not None

    def test_fit_busy_model(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        split = chronological_split(history)
        model = fit_ridge(split.train, target="busy_count")
        assert model is not None

    def test_predict_finite(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)
        model = fit_ridge(split.train, target="load_kw")
        predictions = predict_ridge(model, split.validation)
        assert np.isfinite(predictions).all()

    def test_predict_busy_finite(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)
        model = fit_ridge(split.train, target="busy_count")
        predictions = predict_ridge(model, split.validation)
        assert np.isfinite(predictions).all()

    def test_evaluate_model_on_validation(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)
        model = fit_ridge(split.train, target="load_kw")
        metrics = evaluate_model_on_split(model, split.validation, "load_kw", "ridge")
        assert len(metrics) == 3  # horizons 1, 6, 24
        for m in metrics:
            assert np.isfinite(m.mae)
            assert np.isfinite(m.wape)

    def test_metrics_both_targets(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)

        load_metrics = evaluate_model_on_split(
            fit_ridge(split.train, "load_kw"), split.validation, "load_kw", "ridge"
        )
        busy_metrics = evaluate_model_on_split(
            fit_ridge(split.train, "busy_count"), split.validation, "busy_count", "ridge"
        )
        assert len(load_metrics) == 3
        assert len(busy_metrics) == 3
        # load has wape, busy doesn't
        assert all(m.wape is not None for m in load_metrics)
        assert all(m.wape is None for m in busy_metrics)


class TestChampionSelection:
    """Test honest champion selection."""

    def test_ridge_chosen_when_better(self):
        # Ridge MAE < baseline MAE
        ridge_metrics = (
            MetricRow(model="ridge", target="load_kw", horizon_h=1, mae=10.0, wape=0.05),
            MetricRow(model="ridge", target="load_kw", horizon_h=6, mae=15.0, wape=0.08),
            MetricRow(model="ridge", target="load_kw", horizon_h=24, mae=20.0, wape=0.10),
        )
        baseline_metrics = (
            MetricRow(model="seasonal_naive", target="load_kw", horizon_h=1, mae=20.0, wape=0.15),
            MetricRow(model="seasonal_naive", target="load_kw", horizon_h=6, mae=25.0, wape=0.18),
            MetricRow(model="seasonal_naive", target="load_kw", horizon_h=24, mae=30.0, wape=0.20),
        )
        champion = choose_champion(ridge_metrics, baseline_metrics, "load_kw")
        assert champion == "ridge"

    def test_baseline_chosen_when_ridge_worse(self):
        # Ridge MAE > baseline MAE
        ridge_metrics = (
            MetricRow(model="ridge", target="load_kw", horizon_h=1, mae=30.0, wape=0.20),
            MetricRow(model="ridge", target="load_kw", horizon_h=6, mae=35.0, wape=0.25),
            MetricRow(model="ridge", target="load_kw", horizon_h=24, mae=40.0, wape=0.30),
        )
        baseline_metrics = (
            MetricRow(model="seasonal_naive", target="load_kw", horizon_h=1, mae=10.0, wape=0.05),
            MetricRow(model="seasonal_naive", target="load_kw", horizon_h=6, mae=15.0, wape=0.08),
            MetricRow(model="seasonal_naive", target="load_kw", horizon_h=24, mae=20.0, wape=0.10),
        )
        champion = choose_champion(ridge_metrics, baseline_metrics, "load_kw")
        assert champion == "seasonal_naive"

    def test_champion_is_one_of_two(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)

        ridge_metrics = evaluate_model_on_split(
            fit_ridge(split.train, "load_kw"), split.validation, "load_kw", "ridge"
        )
        baseline_metrics = evaluate_predictions(
            truth=split.validation["target_load_kw"].values,
            predictions=split.validation["seasonal_load_kw"].values,
            model="seasonal_naive",
            target="load_kw",
            horizon_col=split.validation["horizon_h"].values,
        )
        champion = choose_champion(ridge_metrics, baseline_metrics, "load_kw")
        assert champion in {"ridge", "seasonal_naive"}


class TestQualityReport:
    """Test that metrics are finite on the real data."""

    def test_real_data_metrics_finite(self, real_csv):
        history = load_history(real_csv)
        split = chronological_split(history)

        load_metrics = evaluate_model_on_split(
            fit_ridge(split.train, "load_kw"), split.validation, "load_kw", "ridge"
        )
        busy_metrics = evaluate_model_on_split(
            fit_ridge(split.train, "busy_count"), split.validation, "busy_count", "ridge"
        )
        for m in load_metrics + busy_metrics:
            assert np.isfinite(m.mae)
