"""conftest.py — shared pytest fixtures for ML tests."""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

# Ensure ml/src is on PYTHONPATH
ML_ROOT = Path(__file__).parent.parent
SRC_DIR = ML_ROOT / "src"
sys.path.insert(0, str(SRC_DIR))

from conftest_fixtures import generate_fixture_csv


@pytest.fixture(scope="session")
def fixture_csv(tmp_path_factory) -> Path:
    """Generate a 90-day fixture CSV in a session-scoped temp dir."""
    tmp = tmp_path_factory.mktemp("ml_fixtures")
    csv_path = tmp / "station_hourly_history.csv"
    generate_fixture_csv(csv_path)
    return csv_path


@pytest.fixture(scope="session")
def real_csv() -> Path:
    """Path to the real runtime CSV if it exists."""
    repo_root = ML_ROOT.parent
    csv_path = repo_root / "runtime" / "ml" / "station_hourly_history.csv"
    if csv_path.exists():
        return csv_path
    pytest.skip("Real CSV not found")
