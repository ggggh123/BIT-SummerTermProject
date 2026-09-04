import json
import sqlite3

import pytest

from build_golden import _resolve_output, build_base
from helpers import canonical_hash

CUTOFF = "2026-09-01T09:00:00+08:00"


def test_resolve_output_refuses_unsafe_targets(tmp_path):
    with pytest.raises(ValueError):
        _resolve_output("/", "base.db")
    with pytest.raises(ValueError):
        _resolve_output(str(tmp_path), "../base.db")
    existing = tmp_path / "existing"
    existing.mkdir()
    with pytest.raises(ValueError):
        _resolve_output(str(tmp_path), "existing")


def test_build_base_writes_db_manifest_and_is_deterministic(tmp_path):
    out1 = tmp_path / "golden1"
    build_base(out1, seed=20260901, cutoff=CUTOFF, name="base.db")
    base1 = out1 / "base.db"
    assert base1.is_file()

    manifest = json.loads((out1 / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["schema_version"] == 1
    assert manifest["seed"] == 20260901
    assert manifest["cutoff"] == CUTOFF
    assert manifest["row_counts"]["station_hourly_history"] == 6 * 90 * 24
    assert manifest["sha256"]

    conn = sqlite3.connect(str(base1))
    assert conn.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
    conn.close()

    out2 = tmp_path / "golden2"
    build_base(out2, seed=20260901, cutoff=CUTOFF, name="base.db")
    assert canonical_hash(base1) == canonical_hash(out2 / "base.db")
