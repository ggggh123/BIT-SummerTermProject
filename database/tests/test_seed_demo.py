from helpers import build_temp_db, canonical_hash, connect, scalar


def test_seed_demo_counts_and_determinism(tmp_path):
    summary = build_temp_db(tmp_path, name="first.db")
    assert summary.station_count == 6
    assert summary.charger_count == 48
    assert summary.user_count == 30
    assert summary.history_count == 6 * 90 * 24
    assert summary.completed_order_count >= 360

    build_temp_db(tmp_path, name="second.db")
    assert canonical_hash(tmp_path / "first.db") == \
        canonical_hash(tmp_path / "second.db")


def test_seed_demo_content_constraints(tmp_path):
    build_temp_db(tmp_path)
    db = tmp_path / "demo.db"

    # at least six deterministic idle chargers, including charger 1001
    idle_codes = scalar(db, "SELECT group_concat(code) FROM chargers "
                            "WHERE status='idle'")
    idle_set = set(idle_codes.split(","))
    assert "1001" in idle_set
    assert len(idle_set) >= 6

    # at least one deterministic fault charger
    assert scalar(db, "SELECT count(*) FROM chargers WHERE status='fault'") >= 1

    # user 13800138000 has at least 20000 fen
    assert scalar(db, "SELECT balance_fen FROM users "
                      "WHERE mobile='13800138000'") >= 20000

    # every station has 8 chargers
    rows = connect(db).execute(
        "SELECT station_id, count(*) FROM chargers GROUP BY station_id").fetchall()
    assert len(rows) == 6
    assert all(count == 8 for _, count in rows)

    # all six seeded stations are forecast-enabled
    assert scalar(db, "SELECT count(*) FROM stations WHERE forecast_enabled=1") == 6

    # completed orders: ended >= started and nonnegative kWh/amount
    bad = scalar(db, "SELECT count(*) FROM orders WHERE "
                     "ended_at < started_at OR energy_kwh < 0 OR amount_fen < 0")
    assert bad == 0
