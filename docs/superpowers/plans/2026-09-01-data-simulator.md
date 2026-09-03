# SQLite Data and Device Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a versioned SQLite schema, deterministic demo/history data, safe golden-database generation, and a visible Qt simulator that submits charging telemetry and fault events through the frozen TCP protocol.

**Architecture:** Standard-library Python creates database artifacts from `schema.sql` with a fixed seed and cutoff. During runtime the simulator owns only an in-memory deterministic state machine; it never links Qt SQL or opens the database and sends every mutation to the Qt service using `telemetry.push` or `simulator.fault_set`.

**Tech Stack:** SQLite 3, Python 3 standard library plus pytest, C++17, CMake, Qt 6.2+ (`Core`, `Widgets`, `Network`, `Test`).

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- Run every Python command from the repository root with the globally installed system `python3`; do not create/activate a virtual environment and do not invoke pip. Tests use `python3 -m pytest`, and scripts use `python3 path/to/script.py`.
- Fixed seed is `20260901`; default cutoff is `2026-09-01T09:00:00+08:00` during development and is passed explicitly.
- Exactly 6 stations, 48 chargers, 30 users, 30 days of completed orders and 90 days × 24 hours × 6 stations of ML history are generated.
- Same seed/cutoff/schema must create the same canonical data hash.
- Money is integer fen; timestamps are ISO 8601 with `+08:00`; kWh and kW are finite nonnegative numbers.
- Simulator never opens SQLite and never submits authoritative order amount or wallet balance.
- All runtime events use the shared 4-byte big-endian frame and v1 JSON envelope.
- The simulator must have visible run/pause, fault/recover, reset-request status and event log controls for the demo.

---

## Planned File Map

- `database/schema.sql` — version 1 DDL, constraints and indexes.
- `database/seed_demo.py` — deterministic relational/history generator.
- `database/build_golden.py` — safe database builder and manifest writer.
- `database/finalize_golden.py` — validates/imports the approved 144-record run into the final golden DB.
- `database/export_ml_history.py` — stable read-only CSV snapshot.
- `database/tests/*` — schema, determinism, integrity and export tests.
- `simulator/src/main.cpp` — QApplication startup.
- `simulator/src/app/SimulatorConfig.*` — host/port/interval/seed.
- `simulator/src/core/TelemetryEngine.*` — deterministic pure state engine.
- `simulator/src/net/SimulatorClient.*` — shared framed protocol client.
- `simulator/src/ui/SimulatorWindow.*` — controls/status/log.
- `simulator/tests/*` — engine, protocol and fake-server tests.

### Task 1: Versioned schema with business constraints

**Files:**
- Create: `database/schema.sql`
- Create: `database/tests/test_schema.py`

**Interfaces:**
- Consumes: an empty SQLite database.
- Produces: schema version 1 with every table/index required by the server, Web snapshot and ML.

- [x] **Step 1: Write the failing schema test**

```python
def test_schema_has_required_tables_and_constraints(tmp_path):
    db = tmp_path / "schema.db"
    apply_schema(db, Path("database/schema.sql"))
    assert table_names(db) >= {
        "schema_version", "admins", "users", "stations", "chargers",
        "orders", "telemetry", "station_hourly_history", "forecast_runs",
        "forecasts", "events", "request_log",
        "snapshot_meta",
    }
    assert scalar(db, "PRAGMA foreign_key_check") is None
    assert scalar(db, "SELECT version FROM schema_version") == 1
```

Add negative inserts that must fail: duplicate mobile/code, unknown station foreign key, invalid user/charger/order/forecast-run status, negative balance/energy/amount, duplicate `(run_id,station_id,horizon_h)`, and a second `active` forecast run.

- [x] **Step 2: Run and verify failure**

Run: `python3 -m pytest database/tests/test_schema.py -v`

Expected: FAIL because `schema.sql` is missing.

- [x] **Step 3: Implement the DDL**

Use the exact tables/columns from design section 6 plus:

```sql
CREATE TABLE station_hourly_history (
  station_id INTEGER NOT NULL REFERENCES stations(id),
  observed_at TEXT NOT NULL,
  pile_count INTEGER NOT NULL CHECK (pile_count > 0),
  rated_power_kw REAL NOT NULL CHECK (rated_power_kw > 0),
  temperature_c REAL NOT NULL,
  is_holiday INTEGER NOT NULL CHECK (is_holiday IN (0,1)),
  busy_count INTEGER NOT NULL CHECK (busy_count >= 0 AND busy_count <= pile_count),
  load_kw REAL NOT NULL CHECK (load_kw >= 0 AND load_kw <= rated_power_kw),
  PRIMARY KEY (station_id, observed_at)
);
CREATE TABLE request_log (
  request_id TEXT PRIMARY KEY,
  response_json TEXT NOT NULL,
  created_at TEXT NOT NULL
);
```

Add partial unique indexes enforcing one active order per user and charger for statuses `reserved|charging`.
Define `forecast_runs` with `generated_at,data_cutoff,activated_at,model_version,payload_hash,status`; status is exactly `active|superseded`, and a partial unique index permits at most one row with `status='active'`. Seed `snapshot_meta(id=1,version=0)` and constrain version to a nonnegative integer. Add `stations.forecast_enabled` as Boolean-like `0|1`.

- [x] **Step 4: Run and pass**

Run: `python3 -m pytest database/tests/test_schema.py -v`

Expected: PASS.

- [x] **Step 5: Commit**

```bash
git add database/schema.sql database/tests/test_schema.py
git commit -m "feat(database): add constrained SQLite schema v1"
```

### Task 2: Deterministic seed and 90-day hourly history

**Files:**
- Create: `database/seed_demo.py`
- Create: `database/tests/test_seed_demo.py`

**Interfaces:**
- Consumes: `sqlite3.Connection`, integer seed and timezone-aware cutoff.
- Produces: `seed_database(connection, seed, cutoff) -> SeedSummary`.

- [x] **Step 1: Write failing count, range and determinism tests**

```python
summary = build_temp_db(tmp_path, seed=20260901,
                        cutoff="2026-09-01T09:00:00+08:00")
assert summary.station_count == 6
assert summary.charger_count == 48
assert summary.user_count == 30
assert summary.history_count == 6 * 90 * 24
assert summary.completed_order_count >= 360
assert canonical_hash(first_db) == canonical_hash(second_db)
```

Also assert: at least six deterministic idle chargers including charger `1001`, at least one deterministic fault charger, user `13800138000` has at least `20000` fen, every station has 8 chargers, all six seeded stations have `forecast_enabled=1`, and all completed orders have ended ≥ started and nonnegative kWh/amount.

- [x] **Step 2: Run and verify failure**

Run: `python3 -m pytest database/tests/test_seed_demo.py -v`

Expected: FAIL.

- [x] **Step 3: Implement fixed stations and relational seed**

Use `random.Random(seed)` only. Define six fixed station names/addresses/coordinates around one demo city with `forecast_enabled=1`, two price levels, charger IDs 1001–1048, 24 fast and 24 slow chargers, deterministic users and completed orders. Keep at least one known idle charger per station and one known fault charger so repeated rehearsals have capacity. Insert `admin` with lowercase SHA-256 of UTF-8 `123456`; the admin service must verify with the same algorithm.

- [x] **Step 4: Implement hourly demand generation**

For each station/hour, derive demand from morning/evening peaks, weekday/weekend, deterministic holiday, simulated temperature and station offset; clip busy count to `[0,8]` and load to station rated power. Never use the current clock.

- [x] **Step 5: Run and pass**

Run: `python3 -m pytest database/tests/test_seed_demo.py -v`

Expected: PASS and canonical hashes match.

- [x] **Step 6: Commit**

```bash
git add database/seed_demo.py database/tests/test_seed_demo.py
git commit -m "feat(database): generate deterministic demo and ML history"
```

### Task 3: Golden database builder and read-only ML export

**Files:**
- Create: `database/build_golden.py`
- Create: `database/finalize_golden.py`
- Create: `database/export_ml_history.py`
- Create: `database/tests/test_build_golden.py`
- Create: `database/tests/test_export_ml_history.py`
- Create: `database/tests/test_finalize_golden.py`
- Create: `database/README.md`

**Interfaces:**
- Consumes: schema, seed, output path, seed and cutoff.
- Produces: an immutable base SQLite file, stable ML CSV, and—after ML approval—a final golden SQLite file with one active 144-record forecast plus SHA-256 manifest.

- [x] **Step 1: Write failing safe-path and artifact tests**

Builder/finalizer must refuse `/`, existing directories, and outputs outside the explicitly supplied `--output-dir`. Base build must pass `PRAGMA integrity_check`, write `base.db` plus manifest, and repeat byte-stable canonical table hashes. Export selects only `stations.forecast_enabled=1` and produces 12,960 rows sorted by `station_id,observed_at`. Finalizer tests validate a candidate with exactly the six enabled station IDs × horizons 1–24, physical bounds and canonical payload hash; it copies the base through a sibling temp file, imports exactly one `active` run/144 records, clears `request_log`, checks integrity, and writes `demo.db` plus final hash. Invalid candidate leaves existing final files byte-identical.

- [x] **Step 2: Run and verify failure**

Run: `python3 -m pytest database/tests/test_build_golden.py database/tests/test_export_ml_history.py database/tests/test_finalize_golden.py -v`

Expected: FAIL.

- [x] **Step 3: Implement atomic artifact generation**

Build/finalize in temporary sibling files, use explicit transactions, run integrity/foreign-key checks, close SQLite, then `os.replace`. Manifests contain schema version, seed, cutoff, row counts, approved forecast run/payload hash where applicable, and SHA-256. Export opens URI `file:<path>?mode=ro` and writes CSV through a sibling temp file plus `os.replace`. `activated_at` in the artifact records import time only; the server updates it when the runtime copy becomes active.

- [x] **Step 4: Verify exact commands**

```bash
python3 database/build_golden.py \
  --output-dir runtime/golden --seed 20260901 \
  --cutoff 2026-09-01T09:00:00+08:00 --name base.db
python3 database/export_ml_history.py \
  --db runtime/golden/base.db --out runtime/ml/station_hourly_history.csv
python3 -m pytest database/tests -v
```

Expected: commands exit 0 and CSV has 12,960 data rows. Task 7 executes the finalizer after ML approval.

- [x] **Step 5: Commit**

```bash
git add database
git commit -m "feat(database): build golden database and ML snapshot"
```

### Task 4: Deterministic in-memory telemetry engine

**Files:**
- Create: `simulator/src/core/TelemetryEngine.h`
- Create: `simulator/src/core/TelemetryEngine.cpp`
- Create: `simulator/tests/tst_telemetryengine.cpp`
- Modify: `simulator/CMakeLists.txt`

**Interfaces:**
- Consumes: server-reported charger snapshots and fixed seed.
- Produces: `TelemetrySample{chargerId,recordedAt,powerKw,energyIncrementKwh,status}` and explicit fault/recovery intents.

- [x] **Step 1: Write failing deterministic tests**

Given seed `20260901`, fixed time and a charging 60 kW charger, three ticks at 3 seconds must produce the same sequence on repeated runs; every increment is finite and positive, idle/fault/restarting chargers produce zero charging energy, and simulated time advances exactly 3 seconds per tick.

- [x] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_simulator_engine`

Expected: compile failure.

- [x] **Step 3: Implement a pure, clock-injected engine**

```cpp
class TelemetryEngine {
public:
    TelemetryEngine(quint32 seed, QDateTime initialTime, int intervalMs);
    void replaceChargers(QList<ChargerSnapshot> chargers);
    QList<TelemetrySample> tick();
    bool requestFault(int chargerId);
    bool requestRecovery(int chargerId);
};
```

Inject time instead of calling `currentDateTime`; use QRandomGenerator seeded once. No Qt SQL includes or database path exist anywhere under `simulator/`.

- [x] **Step 4: Run and pass**

Run: `ctest --preset debug -R simulator_engine --output-on-failure`

Expected: PASS.

- [x] **Step 5: Commit**

```bash
git add simulator/src/core simulator/tests simulator/CMakeLists.txt
git commit -m "feat(simulator): add deterministic telemetry state engine"
```

### Task 5: Framed simulator client and fake-server tests

**Files:**
- Create: `simulator/src/net/SimulatorClient.h`
- Create: `simulator/src/net/SimulatorClient.cpp`
- Create: `simulator/tests/tst_simulatorclient.cpp`
- Modify: `simulator/CMakeLists.txt`

**Interfaces:**
- Consumes: `system.health`, `simulator.status`, shared protocol frames.
- Produces: `telemetry.push`, `simulator.fault_set`, bounded reconnect and acknowledgements.

- [x] **Step 1: Write failing fake-server tests**

Assert one telemetry sample sends:

```json
{"version":1,"requestId":"...","action":"telemetry.push","token":"...","payload":{"chargerId":1001,"recordedAt":"2026-09-01T09:00:03+08:00","powerKw":60.0,"energyIncrementKwh":0.05,"status":"charging"}}
```

Assert fault/recovery sends exact Boolean `fault`, duplicate samples keep request IDs stable for safe idempotent retry, disconnect queues at most 200 samples, and reconnect delays are 1/2/4 seconds.

- [x] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_simulator_client`

Expected: FAIL.

- [x] **Step 3: Implement client using `ev_protocol`**

On connect send `simulator.status` with state/time/count and feed its returned authoritative `chargers` into the engine. Send each telemetry event as a normal v1 action. Only retry an event with its original request ID; queue overflow discards the oldest sample and emits a visible warning signal.

- [x] **Step 4: Run and pass**

Run: `ctest --preset debug -R simulator_client --output-on-failure`

Expected: PASS.

- [x] **Step 5: Commit**

```bash
git add simulator/src/net simulator/tests simulator/CMakeLists.txt
git commit -m "feat(simulator): publish telemetry through frozen TCP protocol"
```

### Task 6: Visible simulator control panel and integration check

**Files:**
- Create: `simulator/src/app/SimulatorConfig.h`
- Create: `simulator/src/app/SimulatorConfig.cpp`
- Create: `simulator/src/ui/SimulatorWindow.h`
- Create: `simulator/src/ui/SimulatorWindow.cpp`
- Create: `simulator/src/main.cpp`
- Create: `simulator/README.md`
- Modify: `simulator/CMakeLists.txt`

**Interfaces:**
- Consumes: host, port, interval, seed and service token.
- Produces: `ev_charger_simulator` with visible connection/run/time/count/log state and controls.

- [x] **Step 1: Add an offscreen smoke test target**

Test constructs `SimulatorWindow` with a fake client and asserts Run changes to Pause, tick count increments, fault/recover actions are disabled without selection, and each visible log entry includes time, charger ID, action and server result.

- [x] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_simulator_window`

Expected: FAIL.

- [x] **Step 3: Implement the panel**

Provide connection badge, Run/Pause, simulated time, event count, charger table, Fault, Recover, Refresh and “Prepare for Reset” controls, plus newest-first event log. Prepare pauses telemetry and displays “请在管理端确认重置”; the simulator token never sends admin-only `demo.reset` and never touches a DB path. After reconnect/status refresh it shows that the reset state has been loaded.

- [ ] **Step 4: Run all data/simulator verification**

```bash
python3 -m pytest database/tests -v
cmake --build --preset debug --target ev_charger_simulator
ctest --preset debug -R "simulator_" --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 5s build/debug/simulator/ev_charger_simulator \
  --host 127.0.0.1 --port 9100 --seed 20260901 --interval-ms 3000
```

Expected: tests PASS; UI initializes; without server it shows a reconnecting state rather than crashing.

- [ ] **Step 5: Run the live contract integration**

Start admin/server with a copied golden DB, start simulator, start one charging order, observe three acknowledged telemetry frames, increased kWh/amount in server, visible simulator log and updated dashboard snapshot.

- [x] **Step 6: Commit**

```bash
git add simulator
git commit -m "feat(simulator): add visible controls and live telemetry demo"
```

### Task 7: Prepare release-time history, import the approved forecast, and seal the final golden database

**Files:**
- Modify: `runtime/golden/manifest.json` (generated release artifact, not hand-edited)
- Verify: `database/finalize_golden.py`
- Verify: `database/tests/test_finalize_golden.py`

**Interfaces:**
- Consumes: the frozen presentation forecast origin, schema/seed, then the acknowledged `runtime/ml/forecast_last_good.json` from ML Task 6.
- Produces: `runtime/golden/demo.db`, `demo.db.sha256`, and a manifest naming the run ID/payload hash/144-record count.

- [x] **Step 1: Freeze and record the presentation forecast window**

#1 records the expected Sep 10 presentation slot; #4 and #5 freeze one explicit `DEMO_FORECAST_ORIGIN` such that horizons 1–24 cover that slot. #4 rebuilds the release base/history with the same cutoff before #5's final ML run:

```bash
python3 database/build_golden.py \
  --output-dir runtime/golden --seed 20260901 \
  --cutoff "$DEMO_FORECAST_ORIGIN" --name base.db
python3 database/export_ml_history.py \
  --db runtime/golden/base.db --out runtime/ml/station_hourly_history.csv
```

If the timetable is not yet published, use a documented candidate time and regenerate these data artifacts—not source code—before the Sep 9 code freeze. #5 then executes ML Tasks 5–6 with that same cutoff and returns the acknowledged last-good file.

- [ ] **Step 2: After ML acknowledgement, run the finalizer and integrity checks**

```bash
python3 database/finalize_golden.py \
  --output-dir runtime/golden --base runtime/golden/base.db \
  --forecast runtime/ml/forecast_last_good.json --name demo.db
python3 -m pytest database/tests/test_finalize_golden.py -v
sqlite3 runtime/golden/demo.db "PRAGMA integrity_check; SELECT count(*) FROM forecast_runs WHERE status='active'; SELECT count(*) FROM forecasts;"
```

Expected: `ok`, one active run and 144 forecast rows; manifest hash matches the final file and `request_log` is empty.

- [x] **Step 3: Commit only reproducible inputs/scripts and record the artifact hash**

Follow the repository artifact policy decided in Foundation Task 1. If the DB is intentionally tracked for the classroom release, force-add only this explicit file and its manifest; otherwise package it through the release script and commit only the manifest. Never weaken the global `*.db` ignore broadly.
