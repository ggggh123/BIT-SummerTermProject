-- EV Charging Platform — SQLite schema v1
-- Frozen v1 data contract. See docs/design/interface-contract.md and
-- docs/plans/2026-09-01-ev-charging-platform-design.md section 6.

-- schema_version: records the database version.
CREATE TABLE schema_version (
    version INTEGER NOT NULL CHECK (version >= 0),
    applied_at TEXT NOT NULL
);

-- snapshot_meta: single row (id=1); version increments with each committed
-- business transaction and drives the Web snapshot version.
CREATE TABLE snapshot_meta (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    version INTEGER NOT NULL CHECK (version >= 0)
);

CREATE TABLE admins (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    mobile TEXT NOT NULL UNIQUE,
    nickname TEXT NOT NULL,
    avatar_path TEXT NOT NULL DEFAULT '',
    balance_fen INTEGER NOT NULL CHECK (balance_fen >= 0),
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'frozen')),
    registered_at TEXT NOT NULL
);

CREATE TABLE stations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    address TEXT NOT NULL,
    latitude REAL NOT NULL,
    longitude REAL NOT NULL,
    price_fen_per_kwh INTEGER NOT NULL CHECK (price_fen_per_kwh >= 0),
    forecast_enabled INTEGER NOT NULL DEFAULT 0
        CHECK (forecast_enabled IN (0, 1)),
    created_at TEXT NOT NULL
);

CREATE TABLE chargers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id INTEGER NOT NULL REFERENCES stations(id),
    code TEXT NOT NULL UNIQUE,
    type TEXT NOT NULL CHECK (type IN ('fast', 'slow')),
    power_kw REAL NOT NULL CHECK (power_kw > 0),
    status TEXT NOT NULL DEFAULT 'idle'
        CHECK (status IN ('idle', 'reserved', 'charging', 'fault', 'restarting')),
    charge_count INTEGER NOT NULL DEFAULT 0 CHECK (charge_count >= 0),
    total_duration_sec INTEGER NOT NULL DEFAULT 0 CHECK (total_duration_sec >= 0),
    updated_at TEXT NOT NULL
);

CREATE TABLE orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id),
    charger_id INTEGER NOT NULL REFERENCES chargers(id),
    status TEXT NOT NULL
        CHECK (status IN ('reserved', 'charging', 'completed', 'cancelled')),
    reserved_at TEXT NOT NULL,
    started_at TEXT,
    ended_at TEXT,
    energy_kwh REAL NOT NULL DEFAULT 0 CHECK (energy_kwh >= 0),
    amount_fen INTEGER NOT NULL DEFAULT 0 CHECK (amount_fen >= 0)
);

-- A user may hold at most one active (reserved|charging) order.
CREATE UNIQUE INDEX idx_orders_one_active_per_user
    ON orders(user_id) WHERE status IN ('reserved', 'charging');

-- A charger may be referenced by at most one active (reserved|charging) order.
CREATE UNIQUE INDEX idx_orders_one_active_per_charger
    ON orders(charger_id) WHERE status IN ('reserved', 'charging');

CREATE TABLE telemetry (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    charger_id INTEGER NOT NULL REFERENCES chargers(id),
    recorded_at TEXT NOT NULL,
    power_kw REAL NOT NULL CHECK (power_kw >= 0),
    energy_increment_kwh REAL NOT NULL CHECK (energy_increment_kwh >= 0),
    event_type TEXT NOT NULL
);

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

CREATE TABLE forecast_runs (
    run_id TEXT PRIMARY KEY,
    generated_at TEXT NOT NULL,
    data_cutoff TEXT NOT NULL,
    activated_at TEXT,
    model_version TEXT NOT NULL,
    payload_hash TEXT NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('active', 'superseded'))
);

-- At most one active forecast run may exist.
CREATE UNIQUE INDEX idx_forecast_runs_single_active
    ON forecast_runs(status) WHERE status = 'active';

CREATE TABLE forecasts (
    run_id TEXT NOT NULL REFERENCES forecast_runs(run_id),
    station_id INTEGER NOT NULL REFERENCES stations(id),
    forecast_at TEXT NOT NULL,
    horizon_h INTEGER NOT NULL CHECK (horizon_h BETWEEN 1 AND 24),
    predicted_load_kw REAL NOT NULL CHECK (predicted_load_kw >= 0),
    predicted_busy_count INTEGER NOT NULL CHECK (predicted_busy_count >= 0),
    predicted_idle_count INTEGER NOT NULL CHECK (predicted_idle_count >= 0),
    congestion_level TEXT NOT NULL
        CHECK (congestion_level IN ('low', 'medium', 'high')),
    is_peak INTEGER NOT NULL CHECK (is_peak IN (0, 1)),
    PRIMARY KEY (run_id, station_id, horizon_h)
);

CREATE TABLE events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type TEXT NOT NULL,
    entity_type TEXT NOT NULL,
    entity_id INTEGER,
    message TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE request_log (
    request_id TEXT PRIMARY KEY,
    action TEXT NOT NULL,
    code TEXT NOT NULL,
    response_json TEXT NOT NULL,
    created_at TEXT NOT NULL
);

-- Version row and snapshot seed.
INSERT INTO schema_version (version, applied_at)
    VALUES (1, '2026-09-01T00:00:00+08:00');
INSERT INTO snapshot_meta (id, version) VALUES (1, 0);
