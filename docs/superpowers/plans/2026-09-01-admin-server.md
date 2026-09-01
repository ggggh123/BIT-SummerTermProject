# Qt Admin and Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Qt administrator application and authoritative TCP business server that serializes all SQLite writes, enforces charging rules, exposes management operations, accepts simulator/ML data, and publishes the Web snapshot.

**Architecture:** The UI stays on the main Qt thread. Each accepted socket is parsed by a connection worker, while a dedicated `DatabaseWorker` thread owns the only runtime `QSqlDatabase` connection and serially executes business commands; successful commits trigger an atomic dashboard snapshot. Admin pages call the same command dispatcher as network requests so business rules have one implementation.

**Tech Stack:** C++17, CMake, Qt 6.2+ (`Core`, `Gui`, `Widgets`, `Network`, `Sql`, `Charts`, `Concurrent`, `Test`), SQLite.

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- Consume `ev_contracts` and `ev_protocol` from `2026-09-01-platform-foundation.md`; never duplicate frame, action, status or error literals.
- Consume `database/schema.sql` and deterministic seed artifacts from `2026-09-01-data-simulator.md`.
- This process is the only runtime SQLite writer; enable foreign keys, WAL and a 3000 ms busy timeout.
- UI is the main thread, socket parsing occurs in connection workers, and the SQLite connection lives only in `DatabaseWorker`'s thread.
- Money is integer fen; charging amount is computed by the server from accumulated kWh and station price.
- A user may have at most one `reserved` or `charging` order; a charger may have at most one active order.
- Every request returns the same `requestId`; successful mutations commit before a success response is emitted.
- Every successful mutation atomically replaces `dashboard/runtime/dashboard_snapshot.json`.

---

## Planned File Map

- `apps/admin-server/src/main.cpp` — QApplication startup.
- `apps/admin-server/src/app/AppConfig.*` — validated CLI/config values.
- `apps/admin-server/src/app/ServerController.*` — thread ownership and request routing.
- `apps/admin-server/src/net/TcpServer.*` — listener and worker lifecycle.
- `apps/admin-server/src/net/ConnectionWorker.*` — socket/frame decode/encode.
- `apps/admin-server/src/db/DatabaseWorker.*` — sole SQLite connection and queued command execution.
- `apps/admin-server/src/db/SchemaInstaller.*` — schema version check and test DB setup.
- `apps/admin-server/src/services/AuthService.*` — user/admin authentication.
- `apps/admin-server/src/services/UserService.*` — token-scoped profile and wallet operations.
- `apps/admin-server/src/services/StationService.*` — stations and chargers.
- `apps/admin-server/src/services/ChargeService.*` — charging state machine and settlement.
- `apps/admin-server/src/services/AdminService.*` — KPIs, station creation, restart, freeze/search.
- `apps/admin-server/src/services/TelemetryService.*` — simulator events.
- `apps/admin-server/src/services/ForecastService.*` — batch validation and transaction publish.
- `apps/admin-server/src/services/DemoResetService.*` — verified golden-data reset inside the running server.
- `apps/admin-server/src/dashboard/SnapshotWriter.*` — atomic Web JSON snapshot.
- `apps/admin-server/src/ui/*` — login, dashboard, charger, station, user and forecast pages.
- `apps/admin-server/tests/*` — Qt Test units and local TCP/SQLite integration tests.

### Task 1: App configuration, SQLite connection, and schema verification

**Files:**
- Create: `apps/admin-server/src/app/AppConfig.h`
- Create: `apps/admin-server/src/app/AppConfig.cpp`
- Create: `apps/admin-server/src/db/SchemaInstaller.h`
- Create: `apps/admin-server/src/db/SchemaInstaller.cpp`
- Create: `apps/admin-server/src/db/DatabaseWorker.h`
- Create: `apps/admin-server/src/db/DatabaseWorker.cpp`
- Create: `apps/admin-server/tests/tst_database.cpp`
- Modify: `apps/admin-server/CMakeLists.txt`

**Interfaces:**
- Consumes: CLI `--db`, `--host`, `--port`, `--snapshot`; `database/schema.sql`.
- Produces: `AppConfig::fromArguments(...)`, `DatabaseWorker::open()`, `DatabaseWorker::handle(RequestEnvelope, SessionContext)`.

- [ ] **Step 1: Write the failing database test**

```cpp
void DatabaseTest::opensWithRequiredPragmas() {
    QTemporaryDir dir;
    const QString path = dir.filePath("demo.db");
    installSchema(path, QStringLiteral("database/schema.sql"));
    DatabaseWorker worker(path);
    QVERIFY(worker.open());
    QCOMPARE(worker.scalar("PRAGMA foreign_keys").toInt(), 1);
    QCOMPARE(worker.scalar("PRAGMA journal_mode").toString().toLower(), "wal");
    QCOMPARE(worker.scalar("PRAGMA busy_timeout").toInt(), 3000);
    QCOMPARE(worker.scalar("SELECT version FROM schema_version").toInt(), 1);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_admin_database`

Expected: compile failure for missing database classes.

- [ ] **Step 3: Implement config and thread-bound database opening**

`AppConfig` validates port 1–65535, existing schema file, writable DB parent, and a snapshot path under `dashboard/runtime/`. `DatabaseWorker::open()` creates a uniquely named Qt SQL connection in its current thread, sets the three PRAGMAs, and verifies schema version `1`; it never exposes `QSqlDatabase` outside the class.

- [ ] **Step 4: Wire CMake and pass the test**

Link `Qt6::Core`, `Qt6::Sql`, `Qt6::Test`, `ev_protocol`; register `admin_database` with CTest.

Run: `cmake --build --preset debug --target tst_admin_database && ctest --preset debug -R admin_database --output-on-failure`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add apps/admin-server
git commit -m "feat(server): open versioned SQLite on dedicated worker"
```

### Task 2: Multithreaded QTcpServer and framed request routing

**Files:**
- Create: `apps/admin-server/src/net/TcpServer.h`
- Create: `apps/admin-server/src/net/TcpServer.cpp`
- Create: `apps/admin-server/src/net/ConnectionWorker.h`
- Create: `apps/admin-server/src/net/ConnectionWorker.cpp`
- Create: `apps/admin-server/src/app/ServerController.h`
- Create: `apps/admin-server/src/app/ServerController.cpp`
- Create: `apps/admin-server/tests/tst_tcpserver.cpp`
- Modify: `apps/admin-server/CMakeLists.txt`

**Interfaces:**
- Consumes: `ev::protocol::FrameDecoder` and valid v1 envelopes.
- Produces: `TcpServer::listen(QHostAddress, quint16)`, `ConnectionWorker::requestReady(RequestEnvelope, QString connectionId)`, cached `HealthState{status,schemaVersion,snapshotVersion,forecastRunId,serverTime}`, and framed `ResponseEnvelope` replies.

- [ ] **Step 1: Write failing local TCP tests**

The test starts on port `0`, sends anonymous health in two chunks, sends two requests in one write, and asserts one response per request with the same IDs plus all frozen health fields. It also sends length `0`, length `1'048'577`, invalid JSON and version `2`; the server must stay alive and return `INVALID_REQUEST` or `UNSUPPORTED_VERSION`.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_admin_tcpserver`

Expected: compile failure for missing server classes.

- [ ] **Step 3: Implement worker ownership**

`TcpServer::incomingConnection(qintptr descriptor)` creates one `QThread` and `ConnectionWorker`; the worker constructs its `QTcpSocket` from the descriptor inside that thread. Parsed requests cross to `ServerController` with queued connections. Controller answers `system.health` from immutable cached state published by the DB worker/SnapshotWriter, so each call touches no SQL yet accurately reports `starting|degraded|ready`; all other requests are queued to `DatabaseWorker`.

- [ ] **Step 4: Implement bounded connection cleanup**

On disconnect: ask worker to delete its socket, quit/wait at most 1000 ms, and delete the thread. Cap simultaneous demo connections at 16 and return `SERVER_BUSY` to additional sockets.

- [ ] **Step 5: Run and pass**

Run: `cmake --build --preset debug --target tst_admin_tcpserver && ctest --preset debug -R admin_tcpserver --output-on-failure`

Expected: PASS and test proves requests run outside the UI thread while DB work runs on the DB thread.

- [ ] **Step 6: Commit**

```bash
git add apps/admin-server/src/net apps/admin-server/src/app apps/admin-server/tests
git commit -m "feat(server): add framed multithreaded TCP server"
```

### Task 3: Authentication, user profile/wallet, and station/order APIs

**Files:**
- Create: `apps/admin-server/src/services/AuthService.h`
- Create: `apps/admin-server/src/services/AuthService.cpp`
- Create: `apps/admin-server/src/services/UserService.h`
- Create: `apps/admin-server/src/services/UserService.cpp`
- Create: `apps/admin-server/src/services/StationService.h`
- Create: `apps/admin-server/src/services/StationService.cpp`
- Create: `apps/admin-server/tests/tst_auth_station.cpp`
- Modify: `apps/admin-server/src/db/DatabaseWorker.cpp`

**Interfaces:**
- Consumes: `auth.user_login`, `admin.login`, `user.get`, `user.update`, `wallet.recharge`, `station.list`, `station.detail`, `charger.list`, `order.current`, `order.list`.
- Produces: opaque in-memory demo tokens mapped to `SessionContext{actorType, actorId, userStatus}` and stable response JSON.

- [ ] **Step 1: Write failing behavior tests**

Tests assert: existing phone returns its user; unseen valid phone creates nickname `用户8000`; invalid phone returns `INVALID_PHONE`; `admin/123456` succeeds and a wrong password returns `INVALID_CREDENTIALS`; `user.get/update` can touch only the token owner; nickname is validated; recharge accepts positive integer fen, is idempotent and rejects a frozen user; station list is sorted by Haversine distance and includes price/total/idle/distance/forecastEnabled; `station.detail`/`charger.list` return only the requested station's data; `order.current` returns at most one active order; `order.list` paginates newest-first and never exposes another user's orders.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_auth_station`

Expected: FAIL because services are absent.

- [ ] **Step 3: Implement services with prepared SQL**

User login validates `^1[3-9][0-9]{9}$`, creates a user inside a transaction when absent, and returns camelCase fields. Admin login compares lowercase SHA-256 of the UTF-8 password with `admins.password_hash`. `UserService` derives user ID from `SessionContext`, never from payload; update supports nickname only and recharge updates integer fen in one transaction. Station distance is computed in C++ from stored coordinates; SQL uses placeholders exclusively. Tokens are random UUIDs stored in memory and invalidated on process exit.

- [ ] **Step 4: Add authorization gates**

The dispatcher consumes the canonical `Permissions.h` matrix. Missing credentials return `AUTH_REQUIRED`; a valid but disallowed actor returns `FORBIDDEN`, both before SQL. Explicit tests cover anonymous health/login, user/admin shared reads, user ownership, simulator-only telemetry/status, ML-only publish and admin-only `demo.reset`.

- [ ] **Step 5: Run and pass**

Run: `ctest --preset debug -R auth_station --output-on-failure`

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add apps/admin-server/src/services apps/admin-server/src/db apps/admin-server/tests
git commit -m "feat(server): add authentication and station queries"
```

### Task 4: Transactional reservation, charging, metering, and settlement

**Files:**
- Create: `apps/admin-server/src/services/ChargeService.h`
- Create: `apps/admin-server/src/services/ChargeService.cpp`
- Create: `apps/admin-server/src/services/TelemetryService.h`
- Create: `apps/admin-server/src/services/TelemetryService.cpp`
- Create: `apps/admin-server/tests/tst_charge_service.cpp`
- Modify: `apps/admin-server/src/db/DatabaseWorker.cpp`

**Interfaces:**
- Consumes: `charge.reserve`, `charge.start`, `charge.stop`, `charge.settle`, `order.cancel`; `TelemetryService` is the sole `telemetry.push` handler and calls `ChargeService::applyEnergyIncrement(...)` on the DB worker.
- Produces: authoritative order, charger and balance values after each committed transition.

- [ ] **Step 1: Write failing state-machine tests**

Use a fresh seeded temp DB and assert:

```text
idle + active user -> reserve => charger reserved, order reserved
reserved + same user -> start => charger charging, order charging
TelemetryService telemetry 0.25 kWh -> ChargeService energy += 0.25 and amountFen = rounded energy * price
stop -> endedAt set and further telemetry ignored
settle with sufficient balance -> order completed, balance debited, charger idle
cancel reserved -> order cancelled, charger idle
```

Also assert unknown charger, non-increasing timestamp, negative/nonfinite energy, and a status mismatch are rejected by the one TelemetryService handler. `USER_FROZEN`, `ACTIVE_ORDER_EXISTS`, `CHARGER_NOT_AVAILABLE`, `ORDER_STATE_CONFLICT`, and `INSUFFICIENT_BALANCE` leave the database unchanged.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_charge_service`

Expected: FAIL because `ChargeService` is absent.

- [ ] **Step 3: Implement each transition as one SQLite transaction**

Use `BEGIN IMMEDIATE`; re-read user/order/charger rows inside the transaction; update only with a matching previous state; require one affected row. `charge.stop` stores `ended_at` while order remains `charging`; `charge.settle` requires non-null `ended_at` and changes status to `completed`. Compute:

```cpp
const qint64 amountFen = qRound64(energyKwh * priceFenPerKwh);
```

The client never submits amount or post-settlement balance.

- [ ] **Step 4: Add idempotency for request IDs**

Persist completed mutation responses in `request_log(request_id PRIMARY KEY, response_json, created_at)`. Replaying the same mutation request returns the stored response instead of double charging.

- [ ] **Step 5: Run and pass**

Run: `ctest --preset debug -R charge_service --output-on-failure`

Expected: every success and rollback case PASS.

- [ ] **Step 6: Commit**

```bash
git add apps/admin-server/src/services/ChargeService.* apps/admin-server/src/services/TelemetryService.* apps/admin-server/src/db apps/admin-server/tests
git commit -m "feat(server): enforce transactional charging lifecycle"
```

### Task 5: Administrator UI and management operations

**Files:**
- Create: `apps/admin-server/src/services/AdminService.h`
- Create: `apps/admin-server/src/services/AdminService.cpp`
- Create: `apps/admin-server/src/ui/MainWindow.h`
- Create: `apps/admin-server/src/ui/MainWindow.cpp`
- Create: `apps/admin-server/src/ui/LoginDialog.h`
- Create: `apps/admin-server/src/ui/LoginDialog.cpp`
- Create: `apps/admin-server/src/ui/DashboardPage.*`
- Create: `apps/admin-server/src/ui/ChargersPage.*`
- Create: `apps/admin-server/src/ui/StationsPage.*`
- Create: `apps/admin-server/src/ui/UsersPage.*`
- Create: `apps/admin-server/src/ui/ForecastPage.*`
- Create: `apps/admin-server/tests/tst_admin_service.cpp`
- Modify: `apps/admin-server/src/main.cpp`
- Modify: `apps/admin-server/CMakeLists.txt`

**Interfaces:**
- Consumes: `admin.dashboard`, `admin.station_create`, `admin.charger_restart`, `admin.user_list`, `admin.user_set_status`.
- Produces: today/month/total revenue, 7/30-day series, charger status counts, station/charger/user tables and forecast alerts.

- [ ] **Step 1: Write failing service tests**

Tests assert seeded KPI totals equal independent SQL sums; time range switch returns 7 or 30 points with missing dates filled as zero; adding a station creates the requested number of idle chargers with `forecast_enabled=0`, increases dynamic `chargerStatus.total`, and does not change the six-station/144-record prediction scope; restart accepts only fault chargers and enters `restarting`; mobile search is parameterized; freezing a user prevents the next reserve.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_admin_service`

Expected: FAIL.

- [ ] **Step 3: Implement services and responsive pages**

Use `QTableView` models, `QChartView` line series, three revenue labels, a state count/percentage view, explicit loading/empty/error states, and confirmation dialogs for restart/freeze. UI calls `ServerController::executeLocal(RequestEnvelope)` so it exercises the same services and transactions as TCP callers.

- [ ] **Step 4: Add remote restart completion**

When restart is accepted, schedule a deterministic 1500 ms completion through the DB worker, then set charger `idle` and write an event. The timer must not update SQLite from the UI thread.

- [ ] **Step 5: Run service tests and an offscreen UI smoke**

Run:

```bash
cmake --build --preset debug --target ev_admin_server tst_admin_service
ctest --preset debug -R admin_service --output-on-failure
QT_QPA_PLATFORM=offscreen timeout 5s build/debug/apps/admin-server/ev_admin_server \
  --db runtime/demo.db --port 9100 --snapshot dashboard/runtime/dashboard_snapshot.json
```

Expected: tests PASS; app initializes without crash.

- [ ] **Step 6: Commit**

```bash
git add apps/admin-server/src/ui apps/admin-server/src/services/AdminService.* apps/admin-server/tests
git commit -m "feat(admin): add operations dashboard and management pages"
```

### Task 6: Simulator telemetry, forecast batches, and atomic Web snapshot

**Files:**
- Modify: `apps/admin-server/src/services/TelemetryService.h`
- Modify: `apps/admin-server/src/services/TelemetryService.cpp`
- Create: `apps/admin-server/src/services/ForecastService.h`
- Create: `apps/admin-server/src/services/ForecastService.cpp`
- Create: `apps/admin-server/src/dashboard/SnapshotWriter.h`
- Create: `apps/admin-server/src/dashboard/SnapshotWriter.cpp`
- Create: `apps/admin-server/tests/tst_forecast_snapshot.cpp`
- Modify: `apps/admin-server/src/db/DatabaseWorker.cpp`

**Interfaces:**
- Consumes: existing `telemetry.push` plus `simulator.fault_set`, `simulator.status`, `forecast.publish`, `forecast.latest`.
- Produces: validated telemetry/events, one active forecast run and the exact dashboard schema from design section 9.

- [ ] **Step 1: Write failing telemetry and forecast tests**

Assert fault/recovery requires the simulator role and valid charger state; `simulator.status` validates `running|paused`, timestamp and nonnegative event count, then returns the authoritative charger snapshots needed after reconnect. A forecast batch must contain exactly 144 records: the 6 `forecast_enabled=1` stations × horizons 1–24, one run ID, no duplicate tuple, no NaN, busy/idle counts within station capacity. Invalid batches leave the previous run active.

- [ ] **Step 2: Write the failing atomic snapshot test**

After a known transaction, build a snapshot and assert `schemaVersion == 1`, positive `snapshotVersion`, all required arrays/objects exist, Web KPI values equal independent SQL, status counts sum to dynamic `total`, actual load has 144 records, and after a valid publish the forecast array has 144 records. Inject a write failure and assert the old snapshot bytes remain unchanged while the DB keeps the committed newer version for retry.

- [ ] **Step 3: Run and verify failure**

Run: `cmake --build --preset debug --target tst_forecast_snapshot`

Expected: FAIL.

- [ ] **Step 4: Implement services and transaction semantics**

Forecast publish validates all records and a canonical payload hash in memory, then executes this exact transaction: `BEGIN IMMEDIATE` → old `active` run to `superseded` → insert the new run as `active` with server-owned `activated_at` → insert 144 records → store the idempotent response → increment `snapshot_meta.version` → `COMMIT`. Any failure rolls back the old run to active. Reusing a run ID with the same hash returns the stored ACK; a different hash returns `FORECAST_INVALID`. Metrics remain ML artifacts and are not accepted in the payload. `forecast.latest` returns `{forecastRun,records}`; it returns stale records with `ok=true`, and returns `{null,[]}` only when no active run exists. Staleness uses `activated_at`. Telemetry applies only to known chargers; for charging orders it increments energy and recomputes server-side amount.

- [ ] **Step 5: Implement atomic snapshot replacement**

Serialize compact UTF-8 JSON to a `QSaveFile` in `dashboard/runtime/`; call `commit()` only after the complete document is written. Build snapshots after successful mutations on the DB worker and retry a failed version. A five-second heartbeat atomically refreshes top-level `generatedAt` without changing `snapshotVersion` or business data. Emit `snapshotPublished(snapshotVersion,generatedAt)`. The publish response contains `snapshotReady`; SQLite remains authoritative if it is false.

- [ ] **Step 6: Run and pass**

Run: `ctest --preset debug -R forecast_snapshot --output-on-failure`

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add apps/admin-server/src/services apps/admin-server/src/dashboard apps/admin-server/tests
git commit -m "feat(server): ingest telemetry forecasts and publish dashboard snapshot"
```

### Task 7: End-to-end server hardening and delivery verification

**Files:**
- Create: `apps/admin-server/src/services/DemoResetService.h`
- Create: `apps/admin-server/src/services/DemoResetService.cpp`
- Create: `apps/admin-server/tests/tst_server_e2e.cpp`
- Create: `apps/admin-server/tests/action_coverage.json`
- Create: `apps/admin-server/README.md`
- Modify: `apps/admin-server/src/app/ServerController.cpp`
- Modify: `apps/admin-server/src/net/ConnectionWorker.cpp`
- Modify: `apps/admin-server/CMakeLists.txt`

**Interfaces:**
- Consumes: all frozen actions including `demo.reset`, database schema, fixed golden DB path and expected SHA-256.
- Produces: a demo-ready `ev_admin_server` executable and written operating procedure.

- [ ] **Step 1: Write the complete local TCP scenario test**

Sequence: health → user auto-register → profile update → recharge → station list → reserve → start → three telemetry increments → stop → settle → dashboard → freeze user → rejected reserve → simulator status/fault → restart → forecast publish → forecast latest → snapshot verification → demo reset. `action_coverage.json` maps every value in `ev::actions::all()` to at least one concrete test function across Tasks 2–7; the E2E test fails when the sets differ, so all 27 handlers and permission branches remain accounted for. Assert request IDs, permissions, order/charger states, balance, revenue, event entries, `snapshotVersion` and 144 forecasts after every relevant step. Reset tests must also prove a non-admin caller is forbidden and that a bad hash or interrupted copy leaves the runtime DB and previous snapshot unchanged.

- [ ] **Step 2: Run and verify failure or missing integration behavior**

Run: `cmake --build --preset debug --target tst_server_e2e && ctest --preset debug -R server_e2e --output-on-failure`

Expected before hardening: at least one timeout, cleanup or response consistency failure.

- [ ] **Step 3: Implement the verified in-process reset service**

`DemoResetService` accepts only an admin session, the exact confirmation `RESET_DEMO` and configured golden path. It verifies SHA-256/`PRAGMA integrity_check`, attaches the golden DB read-only, and rejects other mutations while reset is active. In one `BEGIN IMMEDIATE` transaction it deletes child-to-parent and inserts parent-to-child using the explicit table set `admins,users,stations,chargers,orders,telemetry,station_hourly_history,forecast_runs,forecasts,events`; it validates but does not copy `schema_version`, clears `request_log`, reactivates the imported forecast with server time, stores this reset response, and increments `snapshot_meta.version`. It rebuilds the snapshot only after commit. File-level replacement remains a stopped-server operation and is not used by `demo.reset`.

On every normal startup, before `system.health` reports `status=ready`, verify the active run, set its `activated_at` to server time in a DB-worker transaction, increment `snapshot_meta.version`, and rebuild the snapshot from the current DB. If no active run exists, return `status=degraded`, `forecastRunId=null`, and a valid snapshot with `forecastRun=null`/`forecast24h=[]`; the release gate rejects that state. Add an admin UI confirmation button for online demo reset; the simulator cannot invoke this privileged action.

- [ ] **Step 4: Fix only evidenced failures and add operational logging**

Log one line per request with timestamp, thread ID, connection ID, request ID, action, result code and elapsed milliseconds; never log tokens, passwords or Tencent keys. Ensure shutdown drains DB work, closes sockets, quits worker threads and closes the SQL connection.

- [ ] **Step 5: Document exact startup and recovery**

README includes build command, CLI flags, default demo credentials, thread model, health request, DB/snapshot ownership, graceful stop, and DB restore prerequisite.

- [ ] **Step 6: Run the full server verification**

```bash
cmake --build --preset debug
ctest --preset debug -R "admin_|server_" --output-on-failure
```

Expected: all server/admin tests PASS with zero leaked QThreads or SQL connection warnings.

- [ ] **Step 7: Commit**

```bash
git add apps/admin-server
git commit -m "test(server): harden complete admin and charging service"
```
