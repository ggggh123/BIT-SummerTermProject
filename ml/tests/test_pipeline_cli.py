"""Tests for pipeline and CLI (Task 5)."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import pandas as pd
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "src"))

from evml.pipeline import run_pipeline


class TestPipeline:
    """Test end-to-end pipeline."""

    def test_run_all_produces_artifacts(self, fixture_csv, tmp_path):
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run = run_pipeline(
            history_path=str(fixture_csv),
            cutoff=cutoff,
            output_dir=tmp_path,
        )
        # Check artifacts
        assert (tmp_path / "model_load.joblib").exists()
        assert (tmp_path / "model_busy.joblib").exists()
        assert (tmp_path / "metrics.json").exists()
        assert (tmp_path / "forecast_candidate.json").exists()
        assert (tmp_path / "run_summary.json").exists()

    def test_candidate_has_144_records(self, fixture_csv, tmp_path):
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run = run_pipeline(
            history_path=str(fixture_csv),
            cutoff=cutoff,
            output_dir=tmp_path,
        )
        with open(tmp_path / "forecast_candidate.json", "r", encoding="utf-8") as f:
            candidate = json.load(f)
        assert len(candidate["records"]) == 144

    def test_metrics_include_both_models(self, fixture_csv, tmp_path):
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run = run_pipeline(
            history_path=str(fixture_csv),
            cutoff=cutoff,
            output_dir=tmp_path,
        )
        with open(tmp_path / "metrics.json", "r", encoding="utf-8") as f:
            metrics = json.load(f)
        models = {m["model"] for m in metrics}
        assert "ridge" in models
        assert "seasonal_naive" in models
        targets = {m["target"] for m in metrics}
        assert "load_kw" in targets
        assert "busy_count" in targets

    def test_candidate_has_no_metrics(self, fixture_csv, tmp_path):
        """forecast_candidate.json must not contain metrics."""
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run = run_pipeline(
            history_path=str(fixture_csv),
            cutoff=cutoff,
            output_dir=tmp_path,
        )
        with open(tmp_path / "forecast_candidate.json", "r", encoding="utf-8") as f:
            candidate = json.load(f)
        assert "metrics" not in candidate

    def test_deterministic_repeat(self, fixture_csv, tmp_path):
        """Same input/cutoff produces identical metrics and forecast."""
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run1 = run_pipeline(
            history_path=str(fixture_csv),
            cutoff=cutoff,
            output_dir=tmp_path / "run1",
        )
        run2 = run_pipeline(
            history_path=str(fixture_csv),
            cutoff=cutoff,
            output_dir=tmp_path / "run2",
        )
        assert run1.run_id == run2.run_id
        # Compare records
        r1 = {r.station_id * 100 + r.horizon_h: r.predicted_load_kw for r in run1.records}
        r2 = {r.station_id * 100 + r.horizon_h: r.predicted_load_kw for r in run2.records}
        assert r1 == r2
        # Compare metrics
        m1 = {(m.model, m.target, m.horizon_h): m.mae for m in run1.metrics}
        m2 = {(m.model, m.target, m.horizon_h): m.mae for m in run2.metrics}
        assert m1 == m2

    def test_generated_at_used(self, fixture_csv, tmp_path):
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        generated_at = pd.Timestamp("2026-09-02T08:00:00+08:00")
        run = run_pipeline(
            history_path=str(fixture_csv),
            cutoff=cutoff,
            output_dir=tmp_path,
            generated_at=generated_at,
        )
        assert run.generated_at == "2026-09-02T08:00:00+08:00"

    def test_real_csv_pipeline(self, real_csv, tmp_path):
        """Run pipeline on real data if available."""
        cutoff = pd.Timestamp("2026-09-01T09:00:00+08:00")
        run = run_pipeline(
            history_path=str(real_csv),
            cutoff=cutoff,
            output_dir=tmp_path,
        )
        assert len(run.records) == 144
        with open(tmp_path / "forecast_candidate.json", "r", encoding="utf-8") as f:
            candidate = json.load(f)
        assert len(candidate["records"]) == 144


class TestCLI:
    """Test CLI run-all command."""

    def test_cli_run_all(self, fixture_csv, tmp_path):
        from evml.cli import main
        rc = main([
            "run-all",
            "--history", str(fixture_csv),
            "--cutoff", "2026-09-01T09:00:00+08:00",
            "--output-dir", str(tmp_path),
        ])
        assert rc == 0
        assert (tmp_path / "model_load.joblib").exists()
        assert (tmp_path / "model_busy.joblib").exists()
        assert (tmp_path / "metrics.json").exists()
        assert (tmp_path / "forecast_candidate.json").exists()
        assert (tmp_path / "run_summary.json").exists()
