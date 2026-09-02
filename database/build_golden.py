"""Build the immutable base golden database.

Usage::

    python3 database/build_golden.py \\
        --output-dir runtime/golden --seed 20260901 \\
        --cutoff 2026-09-01T09:00:00+08:00 --name base.db
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sqlite3
import tempfile
from pathlib import Path

from seed_demo import DEFAULT_CUTOFF, FIXED_SEED, seed_database

SCHEMA_PATH = Path(__file__).resolve().parent / "schema.sql"

ROW_TABLES = [
    "admins", "users", "stations", "chargers", "orders", "telemetry",
    "station_hourly_history", "forecast_runs", "forecasts", "events",
    "request_log",
]


def _resolve_output(output_dir, name):
    """Return (target_path, output_dir) after safety checks."""
    out = Path(output_dir).resolve()
    if out == Path(out.anchor) or out.parts == (out.anchor,):
        raise ValueError("refusing to write to the filesystem root")
    if name in ("", ".", "..") or Path(name).name != name:
        raise ValueError("invalid output name: " + repr(name))
    target = (out / name).resolve()
    if target.parent != out:
        raise ValueError("output escapes the supplied output directory")
    if target.is_dir():
        raise ValueError("output path is an existing directory")
    return target, out


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _open_ro(path):
    return sqlite3.connect(Path(path).resolve().as_uri() + "?mode=ro", uri=True)


def _row_counts(conn):
    return {
        table: conn.execute(f'SELECT count(*) FROM "{table}"').fetchone()[0]
        for table in ROW_TABLES
    }


def build_base(output_dir, seed=FIXED_SEED, cutoff=DEFAULT_CUTOFF, name="base.db"):
    target, out_dir = _resolve_output(output_dir, name)
    out_dir.mkdir(parents=True, exist_ok=True)

    fd, tmp = tempfile.mkstemp(dir=str(out_dir), prefix=".base-", suffix=".db")
    os.close(fd)
    tmp_path = Path(tmp)
    conn = None
    try:
        conn = sqlite3.connect(str(tmp_path))
        conn.execute("PRAGMA foreign_keys = ON")
        conn.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))
        summary = seed_database(conn, seed=seed, cutoff=cutoff)
        conn.commit()
        conn.execute("PRAGMA wal_checkpoint(FULL)")
        ok = conn.execute("PRAGMA integrity_check").fetchone()[0]
        if ok != "ok":
            raise RuntimeError("integrity_check failed: " + str(ok))
        counts = _row_counts(conn)
        conn.commit()
        conn.close()
        conn = None

        os.replace(tmp_path, target)

        manifest = {
            "name": name,
            "schema_version": 1,
            "seed": seed,
            "cutoff": cutoff,
            "row_counts": counts,
            "sha256": _sha256(target),
        }
        _write_manifest(out_dir, manifest)
        return summary
    finally:
        if conn is not None:
            conn.close()
        if tmp_path.exists():
            tmp_path.unlink(missing_ok=True)


def _write_manifest(out_dir, manifest):
    fd, tmp = tempfile.mkstemp(dir=str(out_dir), prefix=".manifest-", suffix=".json")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, ensure_ascii=False, indent=2)
        fh.write("\n")
    os.replace(tmp, out_dir / "manifest.json")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build the base golden database.")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--seed", type=int, default=FIXED_SEED)
    parser.add_argument("--cutoff", default=DEFAULT_CUTOFF)
    parser.add_argument("--name", default="base.db")
    args = parser.parse_args(argv)
    build_base(args.output_dir, seed=args.seed, cutoff=args.cutoff, name=args.name)


if __name__ == "__main__":
    main()
