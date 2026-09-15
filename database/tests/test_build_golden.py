import hashlib
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


def test_build_core_writes_named_manifest_checksum_and_preserves_demo_manifest(
        tmp_path):
    out = tmp_path / "golden"
    out.mkdir()
    demo_manifest = out / "manifest.json"
    demo_manifest.write_text('{"name":"demo.db"}\n', encoding="utf-8")

    build_base(out, seed=20260901, cutoff=CUTOFF, name="core.db")

    core = out / "core.db"
    digest = hashlib.sha256(core.read_bytes()).hexdigest()
    manifest = json.loads(
        (out / "core.manifest.json").read_text(encoding="utf-8"))
    assert manifest == {
        "name": "core.db",
        "schema_version": 1,
        "seed": 20260901,
        "cutoff": CUTOFF,
        "row_counts": {
            "admins": 1,
            "users": 30,
            "stations": 6,
            "chargers": 48,
            "orders": 431,
            "telemetry": 0,
            "station_hourly_history": 12960,
            "forecast_runs": 0,
            "forecasts": 0,
            "events": 0,
            "request_log": 0,
        },
        "sha256": digest,
    }
    assert (out / "core.db.sha256").read_text(encoding="utf-8").strip() == digest
    assert demo_manifest.read_text(encoding="utf-8") == '{"name":"demo.db"}\n'

    conn = sqlite3.connect(str(core))
    assert conn.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
    conn.close()


def test_build_core_refuses_to_overwrite_existing_artifacts(tmp_path):
    out = tmp_path / "golden"
    build_base(out, seed=20260901, cutoff=CUTOFF, name="core.db")
    original = {
        path.name: path.read_bytes()
        for path in (
            out / "core.db",
            out / "core.manifest.json",
            out / "core.db.sha256",
        )
    }

    with pytest.raises(FileExistsError):
        build_base(out, seed=20260901, cutoff=CUTOFF, name="core.db")

    assert {
        path.name: path.read_bytes()
        for path in (
            out / "core.db",
            out / "core.manifest.json",
            out / "core.db.sha256",
        )
    } == original
