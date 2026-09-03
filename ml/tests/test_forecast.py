"""Tests for forecast run construction (Task 4)."""

from __future__ import annotations

import sys
from pathlib import Path

import pandas as pd
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from evml.repository import load_history
from evml.ridge import fit_ridge
from evml.forecast import build_forecast_run, StationCapacity, SIX_STATIONS
from evml.types import ForecastRun, ForecastRecord


class TestForecastRunCompleteness:
    """Test 144-record completeness and uniqueness."""

    def test_144_records(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        load_model = fit_ridge(build_supervised_split_train(fixture_csv), "load_kw")
        busy_model = fit_ridge(build_supervised_split_train(fixture_csv), "busy_count")
        run = build_forecast_run(
            history, cutoff, load_model, busy_model, "ridge", "ridge"
        )
        assert len(run.records) == 144

    def test_unique_station_horizon_pairs(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        load_model = fit_ridge(train, "load_kw")
        busy_model = fit_ridge(train, "busy_count")
        run = build_forecast_run(
            history, cutoff, load_model, busy_model, "ridge", "ridge"
        )
        pairs = {(r.station_id, r.horizon_h) for r in run.records}
        expected = {(s, h) for s in SIX_STATIONS for h in range(1, 25)}
        assert pairs == expected

    def test_records_sorted(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        for i in range(len(run.records) - 1):
            r1, r2 = run.records[i], run.records[i + 1]
            assert (r1.station_id, r1.horizon_h) < (r2.station_id, r2.horizon_h)


class TestForecastBounds:
    """Test physical bounds on forecast records."""

    def test_load_within_rated_power(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        capacities = _get_capacities(history)
        for r in run.records:
            cap = capacities[r.station_id]
            assert 0 <= r.predicted_load_kw <= cap.rated_power_kw

    def test_busy_within_pile_count(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        capacities = _get_capacities(history)
        for r in run.records:
            cap = capacities[r.station_id]
            assert 0 <= r.predicted_busy_count <= cap.pile_count

    def test_idle_equals_pile_minus_busy(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        capacities = _get_capacities(history)
        for r in run.records:
            cap = capacities[r.station_id]
            assert r.predicted_idle_count == cap.pile_count - r.predicted_busy_count

    def test_no_nan(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        for r in run.records:
            import math
            assert math.isfinite(r.predicted_load_kw)
            assert isinstance(r.predicted_busy_count, int)
            assert isinstance(r.predicted_idle_count, int)


class TestCongestionAndPeak:
    """Test congestion level and peak marking."""

    def test_congestion_levels_valid(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        for r in run.records:
            assert r.congestion_level in ("low", "medium", "high")

    def test_peak_is_consecutive_pair(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        # Each station should have exactly 2 peak horizons
        from collections import Counter
        peak_counts = Counter()
        peak_horizons: dict[int, list[int]] = {}
        for r in run.records:
            if r.is_peak:
                peak_counts[r.station_id] += 1
                peak_horizons.setdefault(r.station_id, []).append(r.horizon_h)
        for sid in SIX_STATIONS:
            assert peak_counts[sid] == 2, f"Station {sid} has {peak_counts[sid]} peaks"
            # Must be consecutive
            hs = sorted(peak_horizons[sid])
            assert hs[1] == hs[0] + 1, f"Station {sid} peak horizons {hs} not consecutive"

    def test_congestion_thresholds(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        capacities = _get_capacities(history)
        for r in run.records:
            cap = capacities[r.station_id]
            ratio = r.predicted_busy_count / cap.pile_count
            if ratio >= 0.8:
                assert r.congestion_level == "high"
            elif ratio >= 0.5:
                assert r.congestion_level == "medium"
            else:
                assert r.congestion_level == "low"


class TestRunIdentity:
    """Test deterministic run identity."""

    def test_same_input_same_run_id(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run1 = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        run2 = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "ridge",
        )
        assert run1.run_id == run2.run_id

    def test_model_version_records_champions(self, fixture_csv):
        history = load_history(fixture_csv)
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        train = build_supervised_split_train(fixture_csv)
        run = build_forecast_run(
            history, cutoff,
            fit_ridge(train, "load_kw"), fit_ridge(train, "busy_count"),
            "ridge", "seasonal_naive",
        )
        assert "load-ridge" in run.model_version
        assert "busy-seasonal_naive" in run.model_version


# Helpers
def build_supervised_split_train(csv_path):
    from evml.features import chronological_split
    history = load_history(csv_path)
    split = chronological_split(history)
    return split.train

def _get_capacities(history):
    caps = {}
    for sid in SIX_STATIONS:
        grp = history[history["station_id"] == sid]
        caps[sid] = StationCapacity(
            station_id=sid,
            pile_count=int(grp["pile_count"].iloc[0]),
            rated_power_kw=float(grp["rated_power_kw"].iloc[0]),
        )
    return caps
