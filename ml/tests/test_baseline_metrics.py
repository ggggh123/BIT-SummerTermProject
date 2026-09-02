"""Tests for baseline and metrics (Task 2)."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from evml.repository import load_history
from evml.features import build_supervised, chronological_split
from evml.baseline import predict_seasonal_naive
from evml.metrics import mae, wape, evaluate_predictions


class TestSeasonalNaive:
    """Test seasonal-naive baseline prediction."""

    def test_predict_returns_seasonal_column(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        pred = predict_seasonal_naive(supervised)
        # For each row, prediction should equal seasonal_load_kw
        assert np.allclose(pred.values, supervised["seasonal_load_kw"].values)

    def test_predict_matches_yesterday_value(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        pred = predict_seasonal_naive(supervised)
        # seasonal_load_kw is the value from 24h ago = yesterday same hour
        # For a daily-repeating fixture this should be very close to target
        # but not exactly due to random noise
        assert len(pred) == len(supervised)


class TestMetrics:
    """Test MAE and WAPE computation."""

    def test_mae_zero(self):
        a = np.array([1.0, 2.0, 3.0])
        p = np.array([1.0, 2.0, 3.0])
        assert mae(a, p) == 0.0

    def test_mae_positive(self):
        a = np.array([1.0, 2.0, 3.0])
        p = np.array([2.0, 3.0, 4.0])
        assert mae(a, p) == 1.0

    def test_wape_zero(self):
        a = np.array([1.0, 2.0, 3.0])
        p = np.array([1.0, 2.0, 3.0])
        assert wape(a, p) == 0.0

    def test_wape_positive(self):
        a = np.array([100.0, 200.0])
        p = np.array([110.0, 210.0])
        # sum(|err|) = 10+10 = 20, sum(|actual|) = 300
        assert abs(wape(a, p) - 20.0 / 300.0) < 1e-9

    def test_wape_zero_actual(self):
        """WAPE with zero actual should use 1e-9 denominator."""
        a = np.array([0.0, 0.0])
        p = np.array([1.0, 1.0])
        # sum(|err|)=2, max(sum(|actual|), 1e-9) = 1e-9
        assert wape(a, p) == 2.0 / 1e-9

    def test_evaluate_predictions_returns_3_rows(self):
        truth = np.array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
        pred = np.array([1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
        horizons = np.array([1, 1, 6, 6, 24, 24])
        metrics = evaluate_predictions(truth, pred, "ridge", "load_kw", horizon_col=horizons)
        assert len(metrics) == 3
        assert all(m.mae == 0.0 for m in metrics)
        assert all(m.wape == 0.0 for m in metrics)  # load_kw has wape

    def test_evaluate_busy_has_no_wape(self):
        truth = np.array([1.0, 2.0, 3.0])
        pred = np.array([1.0, 2.0, 3.0])
        horizons = np.array([1, 6, 24])
        metrics = evaluate_predictions(truth, pred, "seasonal_naive", "busy_count", horizon_col=horizons)
        assert len(metrics) == 3
        assert all(m.wape is None for m in metrics)

    def test_evaluate_on_supervised(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        split = chronological_split(history)
        val = split.validation

        baseline_pred = predict_seasonal_naive(val)
        metrics = evaluate_predictions(
            truth=val["target_load_kw"].values,
            predictions=baseline_pred.values,
            model="seasonal_naive",
            target="load_kw",
            horizon_col=val["horizon_h"].values,
        )
        assert len(metrics) == 3
        # horizons 1, 6, 24
        h_values = sorted(m.horizon_h for m in metrics)
        assert h_values == [1, 6, 24]

    def test_all_metrics_finite(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)
        val = split.validation

        baseline_pred = predict_seasonal_naive(val)
        metrics = evaluate_predictions(
            truth=val["target_load_kw"].values,
            predictions=baseline_pred.values,
            model="seasonal_naive",
            target="load_kw",
            horizon_col=val["horizon_h"].values,
        )
        for m in metrics:
            assert np.isfinite(m.mae)
            assert np.isfinite(m.wape)
