"""Read-only CSV snapshot of the 90-day hourly ML history.

Usage::

    python3 database/export_ml_history.py \\
        --db runtime/golden/base.db \\
        --out runtime/ml/station_hourly_history.csv
"""
from __future__ import annotations

import argparse
import csv
import os
import sqlite3
import tempfile
from pathlib import Path

HEADER = [
    "station_id", "observed_at", "pile_count", "rated_power_kw",
    "temperature_c", "is_holiday", "busy_count", "load_kw",
]


def export_history(db_path, out_path):
    db_path = Path(db_path).resolve()
    out_path = Path(out_path).resolve()

    if not db_path.is_file():
        raise FileNotFoundError(db_path)
    if out_path == Path(out_path.anchor):
        raise ValueError("refusing to write to the filesystem root")
    if out_path.is_dir():
        raise ValueError("output path is an existing directory")

    conn = sqlite3.connect(db_path.as_uri() + "?mode=ro", uri=True)
    try:
        rows = conn.execute(
            "SELECT h.station_id, h.observed_at, h.pile_count, "
            "h.rated_power_kw, h.temperature_c, h.is_holiday, h.busy_count, "
            "h.load_kw "
            "FROM station_hourly_history h "
            "JOIN stations s ON s.id = h.station_id "
            "WHERE s.forecast_enabled = 1 "
            "ORDER BY h.station_id, h.observed_at").fetchall()
    finally:
        conn.close()

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=str(out_path.parent), prefix=".csv-", suffix=".tmp")
    with os.fdopen(fd, "w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(HEADER)
        writer.writerows(rows)
    os.replace(tmp, out_path)
    return len(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Export ML hourly history CSV.")
    parser.add_argument("--db", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    n = export_history(args.db, args.out)
    print(f"exported {n} rows to {args.out}")


if __name__ == "__main__":
    main()
