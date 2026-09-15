from pathlib import Path

from helpers import (
    apply_schema,
    connect,
    expect_integrity_error,
    scalar,
    table_names,
)

SCHEMA = Path("database/schema.sql")
T = "2026-09-01T00:00:00+08:00"


def test_schema_has_required_tables_and_constraints(tmp_path):
    db = tmp_path / "schema.db"
    apply_schema(db, SCHEMA)

    assert table_names(db) >= {
        "schema_version", "admins", "users", "stations", "chargers",
        "orders", "telemetry", "station_hourly_history", "forecast_runs",
        "forecasts", "events", "request_log", "snapshot_meta",
    }
    assert scalar(db, "PRAGMA foreign_key_check") is None
    assert scalar(db, "SELECT version FROM schema_version") == 1
    conn = connect(db)
    columns = {row[1] for row in conn.execute("PRAGMA table_info(request_log)").fetchall()}
    conn.close()
    assert {"request_id", "action", "code", "response_json", "created_at"} <= columns


def _seed_base(db):
    conn = connect(db)
    conn.execute(
        "INSERT INTO stations (name,address,latitude,longitude,"
        "price_fen_per_kwh,forecast_enabled,created_at) "
        "VALUES ('s1','a',0,0,100,1,?)", (T,))
    conn.execute(
        "INSERT INTO users (mobile,nickname,avatar_path,balance_fen,status,"
        "registered_at) VALUES ('13800138000','u','',0,'active',?)", (T,))
    conn.execute(
        "INSERT INTO chargers (station_id,code,type,power_kw,status,"
        "charge_count,total_duration_sec,updated_at) "
        "VALUES (1,'1001','fast',60,'idle',0,0,?)", (T,))
    conn.execute(
        "INSERT INTO forecast_runs (run_id,generated_at,data_cutoff,"
        "activated_at,model_version,payload_hash,status) "
        "VALUES ('r1',?,?,NULL,'v','h','active')", (T, T))
    conn.execute(
        "INSERT INTO forecasts (run_id,station_id,forecast_at,horizon_h,"
        "predicted_load_kw,predicted_busy_count,predicted_idle_count,"
        "congestion_level,is_peak) "
        "VALUES ('r1',1,?,1,10,2,6,'low',0)", (T,))
    conn.commit()
    conn.close()


def test_schema_rejects_invalid_rows(tmp_path):
    db = tmp_path / "schema.db"
    apply_schema(db, SCHEMA)
    _seed_base(db)

    # duplicate mobile / charger code
    expect_integrity_error(
        db,
        "INSERT INTO users (mobile,nickname,avatar_path,balance_fen,status,"
        "registered_at) VALUES ('13800138000','u','',0,'active',?)", (T,))
    expect_integrity_error(
        db,
        "INSERT INTO chargers (station_id,code,type,power_kw,status,"
        "charge_count,total_duration_sec,updated_at) "
        "VALUES (1,'1001','slow',30,'idle',0,0,?)", (T,))

    # unknown station foreign key
    expect_integrity_error(
        db,
        "INSERT INTO chargers (station_id,code,type,power_kw,status,"
        "charge_count,total_duration_sec,updated_at) "
        "VALUES (999,'9999','fast',60,'idle',0,0,?)", (T,))

    # invalid user / charger / order / forecast-run status
    expect_integrity_error(
        db,
        "INSERT INTO users (mobile,nickname,avatar_path,balance_fen,status,"
        "registered_at) VALUES ('13900139000','u','',0,'weird',?)", (T,))
    expect_integrity_error(
        db,
        "INSERT INTO chargers (station_id,code,type,power_kw,status,"
        "charge_count,total_duration_sec,updated_at) "
        "VALUES (1,'1002','fast',60,'weird',0,0,?)", (T,))
    expect_integrity_error(
        db,
        "INSERT INTO orders (user_id,charger_id,status,reserved_at,started_at,"
        "ended_at,energy_kwh,amount_fen) "
        "VALUES (1,1,'weird',?,NULL,NULL,0,0)", (T,))
    expect_integrity_error(
        db,
        "INSERT INTO forecast_runs (run_id,generated_at,data_cutoff,"
        "activated_at,model_version,payload_hash,status) "
        "VALUES ('rX',?,?,NULL,'v','h','weird')", (T, T))

    # negative balance / energy / amount
    expect_integrity_error(
        db,
        "INSERT INTO users (mobile,nickname,avatar_path,balance_fen,status,"
        "registered_at) VALUES ('13900139001','u','',-1,'active',?)", (T,))
    expect_integrity_error(
        db,
        "INSERT INTO orders (user_id,charger_id,status,reserved_at,started_at,"
        "ended_at,energy_kwh,amount_fen) "
        "VALUES (1,1,'reserved',?,NULL,NULL,-1,0)", (T,))
    expect_integrity_error(
        db,
        "INSERT INTO orders (user_id,charger_id,status,reserved_at,started_at,"
        "ended_at,energy_kwh,amount_fen) "
        "VALUES (1,1,'reserved',?,NULL,NULL,0,-1)", (T,))

    # duplicate (run_id, station_id, horizon_h)
    expect_integrity_error(
        db,
        "INSERT INTO forecasts (run_id,station_id,forecast_at,horizon_h,"
        "predicted_load_kw,predicted_busy_count,predicted_idle_count,"
        "congestion_level,is_peak) "
        "VALUES ('r1',1,?,1,11,3,5,'low',0)", (T,))

    # a second active forecast run
    expect_integrity_error(
        db,
        "INSERT INTO forecast_runs (run_id,generated_at,data_cutoff,"
        "activated_at,model_version,payload_hash,status) "
        "VALUES ('r2',?,?,NULL,'v','h2','active')", (T, T))
