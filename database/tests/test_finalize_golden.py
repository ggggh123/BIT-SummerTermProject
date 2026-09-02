import hashlib
import json
import sqlite3

import pytest

from finalize_golden import canonical_payload_hash, finalize_golden
from helpers import build_temp_db

CUTOFF = "2026-09-01T09:00:00+08:00"


def _records():
    records = []
    for sid in range(1, 7):
        for h in range(1, 25):
            busy = (sid + h) % 9  # 0..8
            records.append({
                "stationId": sid,
                "forecastAt": f"2026-09-01T{(9 + h):02d}:00:00+08:00",
                "horizonH": h,
                "predictedLoadKw": round(busy * 45.0, 3),
                "predictedBusyCount": busy,
                "predictedIdleCount": 8 - busy,
                "congestionLevel": ("high" if busy >= 7 else
                                    "medium" if busy >= 4 else "low"),
                "isPeak": (h == 18),
            })
    return records


def _candidate():
    return {
        "runId": "forecast-20260901-testrun",
        "generatedAt": CUTOFF,
        "dataCutoff": CUTOFF,
        "modelVersion": "load-ridge_busy-seasonal-v1",
        "records": _records(),
    }


def test_finalize_valid(tmp_path):
    build_temp_db(tmp_path, name="base.db")
    cand = tmp_path / "forecast.json"
    cand.write_text(json.dumps(_candidate()), encoding="utf-8")

    out = tmp_path / "golden"
    run_id = finalize_golden(out, tmp_path / "base.db", cand, name="demo.db")

    demo = out / "demo.db"
    assert demo.is_file()
    conn = sqlite3.connect(str(demo))
    conn.execute("PRAGMA foreign_keys = ON")
    assert conn.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
    assert conn.execute(
        "SELECT count(*) FROM forecast_runs WHERE status='active'").fetchone()[0] == 1
    assert conn.execute("SELECT count(*) FROM forecasts").fetchone()[0] == 144
    assert conn.execute("SELECT count(*) FROM request_log").fetchone()[0] == 0
    conn.close()

    manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["run_id"] == run_id
    assert manifest["payload_hash"] == canonical_payload_hash(_records())
    digest = hashlib.sha256(demo.read_bytes()).hexdigest()
    assert manifest["sha256"] == digest
    assert (out / "demo.db.sha256").read_text(encoding="utf-8").strip() == digest


def test_finalize_invalid_leaves_files_unchanged(tmp_path):
    build_temp_db(tmp_path, name="base.db")
    out = tmp_path / "golden"
    out.mkdir()
    demo = out / "demo.db"
    demo.write_bytes(b"EXISTING")

    # wrong record count
    bad = _candidate()
    bad["records"] = bad["records"][:100]
    cand = tmp_path / "bad.json"
    cand.write_text(json.dumps(bad), encoding="utf-8")
    with pytest.raises(ValueError):
        finalize_golden(out, tmp_path / "base.db", cand, name="demo.db")
    assert demo.read_bytes() == b"EXISTING"
    assert not (out / "demo.db.sha256").exists()

    # physical bound violation
    bad2 = _candidate()
    bad2["records"] = _records()
    bad2["records"][0]["predictedLoadKw"] = -1.0
    cand2 = tmp_path / "bad2.json"
    cand2.write_text(json.dumps(bad2), encoding="utf-8")
    with pytest.raises(ValueError):
        finalize_golden(out, tmp_path / "base.db", cand2, name="demo.db")
    assert demo.read_bytes() == b"EXISTING"
