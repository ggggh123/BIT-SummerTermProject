# database — SQLite schema, seed, and golden database

Responsible: #4 (SCML). Runtime SQLite writer is always the Qt admin/server;
these scripts only build artifacts offline.

## Files

- `schema.sql` — version 1 DDL with constraints and indexes.
- `seed_demo.py` — deterministic demo/history generator (`seed_database`).
- `build_golden.py` — builds the immutable base database `base.db` + manifest.
- `export_ml_history.py` — read-only CSV snapshot for the ML subsystem.
- `finalize_golden.py` — imports one approved 144-record forecast and seals
  the final `demo.db` + `demo.db.sha256` + manifest.

## Commands

```bash
# 1. Build the base golden database (schema + seed)
python3 database/build_golden.py \
  --output-dir runtime/golden --seed 20260901 \
  --cutoff 2026-09-01T09:00:00+08:00 --name base.db

# 2. Export the ML history CSV (12960 rows)
python3 database/export_ml_history.py \
  --db runtime/golden/base.db --out runtime/ml/station_hourly_history.csv

# 3. After ML approval, seal the final golden database
python3 database/finalize_golden.py \
  --output-dir runtime/golden --base runtime/golden/base.db \
  --forecast runtime/ml/forecast_last_good.json --name demo.db

# Tests
python3 -m pytest database/tests -v
```

## Determinism

- Fixed seed `20260901` and default cutoff `2026-09-01T09:00:00+08:00`.
- Same seed/cutoff/schema produce the same canonical data hash.
- Money is integer fen; timestamps are ISO 8601 with `+08:00`.
- `finalize_golden` records import time in `activated_at`; the server sets the
  runtime `activatedAt` when the copy becomes active.
