"""Seal the final golden database with one approved 144-record forecast run.

Usage::

    python3 database/finalize_golden.py \\
        --output-dir runtime/golden --base runtime/golden/base.db \\
        --forecast runtime/ml/forecast_last_good.json --name demo.db
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import sqlite3
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from build_golden import (
    ROW_TABLES,
    _open_ro,
    _resolve_output,
    _row_counts,
    _sha256,
    _write_manifest,
)

TZ = timezone.utc
HORIZONS = set(range(1, 25))
CONGESTION = {"low", "medium", "high"}


def _station_capacities(base):
    conn = _open_ro(base)
    try:
        rows = conn.execute(
            "SELECT s.id, count(c.id), "
            "       sum(CASE WHEN c.type='fast' THEN 60 ELSE 30 END) "
            "FROM stations s LEFT JOIN chargers c ON c.station_id = s.id "
            "WHERE s.forecast_enabled = 1 "
            "GROUP BY s.id ORDER BY s.id").fetchall()
    finally:
        conn.close()
    return {sid: (pile_count, rated) for sid, pile_count, rated in rows}


def canonical_payload_hash(records):
    canon = []
    for r in sorted(records, key=lambda r: (r["stationId"], r["horizonH"])):
        canon.append(json.dumps({
            "stationId": r["stationId"],
            "forecastAt": r["forecastAt"],
            "horizonH": r["horizonH"],
            "predictedLoadKw": r["predictedLoadKw"],
            "predictedBusyCount": r["predictedBusyCount"],
            "predictedIdleCount": r["predictedIdleCount"],
            "congestionLevel": r["congestionLevel"],
            "isPeak": int(r["isPeak"]),
        }, sort_keys=True, separators=(",", ":")))
    return hashlib.sha256("\n".join(canon).encode("utf-8")).hexdigest()


def _validate_candidate(data, capacities):
    run_id = data.get("runId")
    generated_at = data.get("generatedAt")
    data_cutoff = data.get("dataCutoff")
    model_version = data.get("modelVersion")
    records = data.get("records")

    if not isinstance(run_id, str) or not run_id.strip():
        raise ValueError("runId must be a nonblank string")
    if not isinstance(model_version, str) or not model_version.strip():
        raise ValueError("modelVersion must be a nonblank string")
    parsed_times = {}
    for field in ("generatedAt", "dataCutoff"):
        v = data.get(field)
        if not isinstance(v, str):
            raise ValueError(f"{field} must be a string")
        try:
            dt = datetime.fromisoformat(v)
        except ValueError:
            raise ValueError(f"{field} is not a valid timestamp") from None
        if dt.tzinfo is None:
            raise ValueError(f"{field} must carry a timezone")
        parsed_times[field] = dt
    if parsed_times["generatedAt"] < parsed_times["dataCutoff"]:
        raise ValueError("generatedAt must be >= dataCutoff")

    if not isinstance(records, list) or len(records) != 144:
        raise ValueError("records must contain exactly 144 entries")

    expected_stations = set(capacities)
    seen = set()
    for r in records:
        sid = r.get("stationId")
        if sid not in expected_stations:
            raise ValueError("record references an unknown station: " + repr(sid))
        horizon = r.get("horizonH")
        if horizon not in HORIZONS:
            raise ValueError("horizonH must be in 1..24")
        key = (sid, horizon)
        if key in seen:
            raise ValueError("duplicate (stationId, horizonH): " + repr(key))
        seen.add(key)

        pile_count, rated = capacities[sid]
        load = r.get("predictedLoadKw")
        busy = r.get("predictedBusyCount")
        idle = r.get("predictedIdleCount")
        congestion = r.get("congestionLevel")
        is_peak = r.get("isPeak")

        for label, value in (("predictedLoadKw", load),
                             ("predictedBusyCount", busy),
                             ("predictedIdleCount", idle)):
            if not isinstance(value, (int, float)) or isinstance(value, bool) \
                    or not math.isfinite(value):
                raise ValueError(f"{label} must be a finite number")
        if not (0 <= load <= rated):
            raise ValueError("predictedLoadKw out of physical bounds")
        if not (0 <= busy <= pile_count):
            raise ValueError("predictedBusyCount out of physical bounds")
        if idle != pile_count - busy:
            raise ValueError("predictedIdleCount must equal pile_count - busy")
        if congestion not in CONGESTION:
            raise ValueError("invalid congestionLevel")
        if not isinstance(is_peak, bool):
            raise ValueError("isPeak must be a boolean")

    if seen != {(sid, h) for sid in expected_stations for h in HORIZONS}:
        raise ValueError("records must cover every enabled station x horizon 1..24")

    return run_id, generated_at, data_cutoff, model_version, records


def finalize_golden(output_dir, base, forecast, name="demo.db"):
    target, out_dir = _resolve_output(output_dir, name)
    base = Path(base).resolve()
    forecast = Path(forecast).resolve()

    if not base.is_file():
        raise FileNotFoundError(base)
    if not forecast.is_file():
        raise FileNotFoundError(forecast)

    data = json.loads(forecast.read_text(encoding="utf-8"))
    capacities = _station_capacities(base)
    run_id, generated_at, data_cutoff, model_version, records = \
        _validate_candidate(data, capacities)
    payload_hash = canonical_payload_hash(records)

    out_dir.mkdir(parents=True, exist_ok=True)

    fd, tmp = tempfile.mkstemp(dir=str(out_dir), prefix=".demo-", suffix=".db")
    os.close(fd)
    tmp_path = Path(tmp)
    conn = None
    try:
        shutil.copy2(base, tmp_path)
        conn = sqlite3.connect(str(tmp_path))
        conn.execute("PRAGMA foreign_keys = ON")

        conn.execute(
            "INSERT INTO forecast_runs (run_id, generated_at, data_cutoff, "
            "activated_at, model_version, payload_hash, status) "
            "VALUES (?, ?, ?, ?, ?, ?, 'active')",
            (run_id, generated_at, data_cutoff,
             datetime.now(timezone.utc).astimezone().isoformat(),
             model_version, payload_hash),
        )
        conn.executemany(
            "INSERT INTO forecasts (run_id, station_id, forecast_at, horizon_h, "
            "predicted_load_kw, predicted_busy_count, predicted_idle_count, "
            "congestion_level, is_peak) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            [(run_id, r["stationId"], r["forecastAt"], r["horizonH"],
              r["predictedLoadKw"], r["predictedBusyCount"],
              r["predictedIdleCount"], r["congestionLevel"],
              int(r["isPeak"])) for r in records],
        )
        conn.execute("DELETE FROM request_log")

        ok = conn.execute("PRAGMA integrity_check").fetchone()[0]
        if ok != "ok":
            raise RuntimeError("integrity_check failed: " + str(ok))
        fk = conn.execute("PRAGMA foreign_key_check").fetchall()
        if fk:
            raise RuntimeError("foreign_key_check violations: " + repr(fk))

        counts = _row_counts(conn)
        conn.commit()
        conn.close()
        conn = None

        os.replace(tmp_path, target)
        digest = _sha256(target)
        (out_dir / (name + ".sha256")).write_text(digest + "\n", encoding="utf-8")

        manifest = {
            "name": name,
            "schema_version": 1,
            "run_id": run_id,
            "payload_hash": payload_hash,
            "model_version": model_version,
            "generated_at": generated_at,
            "data_cutoff": data_cutoff,
            "row_counts": counts,
            "sha256": digest,
        }
        _write_manifest(out_dir, manifest)
        return run_id
    finally:
        if conn is not None:
            conn.close()
        if tmp_path.exists():
            tmp_path.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Seal the final golden database.")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--forecast", required=True)
    parser.add_argument("--name", default="demo.db")
    args = parser.parse_args(argv)
    run_id = finalize_golden(args.output_dir, args.base, args.forecast,
                             name=args.name)
    print(f"finalized {args.name} with run {run_id}")


if __name__ == "__main__":
    main()
