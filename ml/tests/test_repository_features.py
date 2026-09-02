"""Tests for repository loading and feature/split construction (Task 1)."""

from __future__ import annotations

import math
import sys
from pathlib import Path

import pandas as pd
import pytest

# Ensure src is on path (conftest already does this, but be explicit)
sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from evml.repository import load_history, EXPECTED_COLUMNS
from evml.features import build_supervised, chronological_split, TimeSplit


class TestLoadHistory:
    """Test CSV loading and validation."""

    def test_loads_fixture(self, fixture_csv):
        history = load_history(fixture_csv)
        assert len(history) == 6 * 90 * 24  # 12960
        assert history["station_id"].nunique() == 6
        assert history.groupby("station_id")["observed_at"].nunique().eq(2160).all()

    def test_columns_present(self, fixture_csv):
        history = load_history(fixture_csv)
        for col in EXPECTED_COLUMNS:
            assert col in history.columns

    def test_observed_at_is_timezone_aware(self, fixture_csv):
        history = load_history(fixture_csv)
        ts = pd.to_datetime(history["observed_at"], utc=False)
        assert ts.dt.tz is not None

    def test_no_duplicates(self, fixture_csv):
        history = load_history(fixture_csv)
        dups = history.duplicated(subset=["station_id", "observed_at"])
        assert not dups.any()

    def test_busy_within_capacity(self, fixture_csv):
        history = load_history(fixture_csv)
        assert (history["busy_count"] <= history["pile_count"]).all()
        assert (history["busy_count"] >= 0).all()

    def test_load_within_rated_power(self, fixture_csv):
        history = load_history(fixture_csv)
        assert (history["load_kw"] <= history["rated_power_kw"]).all()
        assert (history["load_kw"] >= 0).all()

    def test_rejects_duplicate_rows(self, tmp_path):
        csv = tmp_path / "dup.csv"
        csv.write_text(
            "station_id,observed_at,pile_count,rated_power_kw,temperature_c,is_holiday,busy_count,load_kw\n"
            "1,2026-06-03T09:00:00+08:00,8,360.0,20.0,0,3,100.0\n"
            "1,2026-06-03T09:00:00+08:00,8,360.0,20.0,0,3,100.0\n"
        )
        with pytest.raises(ValueError, match="Duplicate"):
            load_history(csv)

    def test_rejects_tz_naive(self, tmp_path):
        csv = tmp_path / "naive.csv"
        csv.write_text(
            "station_id,observed_at,pile_count,rated_power_kw,temperature_c,is_holiday,busy_count,load_kw\n"
            "1,2026-06-03T09:00:00,8,360.0,20.0,0,3,100.0\n"
        )
        with pytest.raises(ValueError, match="timezone"):
            load_history(csv)

    def test_rejects_busy_out_of_range(self, tmp_path):
        csv = tmp_path / "bad_busy.csv"
        csv.write_text(
            "station_id,observed_at,pile_count,rated_power_kw,temperature_c,is_holiday,busy_count,load_kw\n"
            "1,2026-06-03T09:00:00+08:00,8,360.0,20.0,0,9,100.0\n"
            "2,2026-06-03T09:00:00+08:00,8,360.0,20.0,0,3,100.0\n"
        )
        with pytest.raises(ValueError, match="busy_count"):
            load_history(csv)

    def test_rejects_load_out_of_range(self, tmp_path):
        csv = tmp_path / "bad_load.csv"
        csv.write_text(
            "station_id,observed_at,pile_count,rated_power_kw,temperature_c,is_holiday,busy_count,load_kw\n"
            "1,2026-06-03T09:00:00+08:00,8,360.0,20.0,0,3,400.0\n"
            "2,2026-06-03T09:00:00+08:00,8,360.0,20.0,0,3,100.0\n"
        )
        with pytest.raises(ValueError, match="load_kw"):
            load_history(csv)

    def test_real_csv(self, real_csv):
        """If the real runtime CSV exists, validate it too."""
        history = load_history(real_csv)
        assert len(history) == 12960
        assert history["station_id"].nunique() == 6
        assert history.groupby("station_id")["observed_at"].nunique().eq(2160).all()


class TestBuildSupervised:
    """Test supervised sample construction."""

    def test_horizons_1_to_24(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        assert supervised["horizon_h"].between(1, 24).all()

    def test_no_future_leakage(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        # source_max_at <= origin_at (all features come from at or before origin)
        assert (supervised["source_max_at"] <= supervised["origin_at"]).all()

    def test_has_required_columns(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        required = {
            "station_id", "origin_at", "target_at", "horizon_h",
            "pile_count", "rated_power_kw",
            "target_hour_sin", "target_hour_cos",
            "target_dow_sin", "target_dow_cos",
            "target_is_weekend", "target_is_holiday", "target_temperature_c",
            "load_lag_1", "load_lag_24", "load_roll_6", "load_roll_24",
            "busy_lag_1", "busy_lag_24", "busy_roll_6", "busy_roll_24",
            "seasonal_load_kw", "seasonal_busy_count",
            "target_load_kw", "target_busy_count",
            "source_max_at",
        }
        assert required.issubset(set(supervised.columns))

    def test_target_at_equals_origin_plus_horizon(self, fixture_csv):
        history = load_history(fixture_csv)
        supervised = build_supervised(history)
        diff = (supervised["target_at"] - supervised["origin_at"]).dt.total_seconds() / 3600
        assert (diff == supervised["horizon_h"]).all()


class TestChronologicalSplit:
    """Test chronological 62/14/14 split."""

    def test_split_is_chronological(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)
        assert isinstance(split, TimeSplit)
        assert split.train["target_at"].max() < split.validation["target_at"].min()
        assert split.validation["target_at"].max() < split.test["target_at"].min()

    def test_split_sizes(self, fixture_csv):
        history = load_history(fixture_csv)
        split = chronological_split(history)
        # 62 days train, 14 days validation, 14 days test
        train_days = split.train["target_at"].dt.tz_convert("+08:00").dt.date.nunique()
        val_days = split.validation["target_at"].dt.tz_convert("+08:00").dt.date.nunique()
        test_days = split.test["target_at"].dt.tz_convert("+08:00").dt.date.nunique()
        assert train_days == 62
        assert val_days == 14
        assert test_days == 14
