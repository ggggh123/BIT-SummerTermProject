CREATE TABLE ads_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE ads_revenue_overview (
    dt TEXT PRIMARY KEY,
    revenue_fen INTEGER NOT NULL,
    energy_kwh REAL NOT NULL,
    order_cnt INTEGER NOT NULL,
    active_user_cnt INTEGER NOT NULL
);

CREATE TABLE ads_station_ranking (
    dt TEXT NOT NULL,
    station_id INTEGER NOT NULL,
    station_name TEXT NOT NULL,
    district TEXT NOT NULL,
    revenue_fen INTEGER NOT NULL,
    energy_kwh REAL NOT NULL,
    order_cnt INTEGER NOT NULL,
    avg_utilization REAL NOT NULL,
    idle_charger_cnt INTEGER NOT NULL,
    rank_no INTEGER NOT NULL,
    PRIMARY KEY (dt, station_id)
);

CREATE TABLE ads_user_profile_rfm (
    dt TEXT NOT NULL,
    segment TEXT NOT NULL,
    user_cnt INTEGER NOT NULL,
    avg_recency_days REAL NOT NULL,
    avg_frequency REAL NOT NULL,
    avg_monetary_fen REAL NOT NULL,
    PRIMARY KEY (dt, segment)
);

CREATE TABLE ads_peak_hour (
    dt TEXT NOT NULL,
    station_id INTEGER NOT NULL,
    hour INTEGER NOT NULL CHECK (hour BETWEEN 0 AND 23),
    busy_count INTEGER NOT NULL,
    pile_count INTEGER NOT NULL,
    load_kw REAL NOT NULL,
    utilization REAL NOT NULL,
    PRIMARY KEY (dt, station_id, hour)
);

CREATE TABLE ads_charger_health (
    dt TEXT NOT NULL,
    charger_id INTEGER NOT NULL,
    station_id INTEGER NOT NULL,
    charger_code TEXT NOT NULL,
    status TEXT NOT NULL,
    fault_cnt INTEGER NOT NULL,
    order_cnt INTEGER NOT NULL,
    energy_kwh REAL NOT NULL,
    charge_duration_sec INTEGER NOT NULL,
    PRIMARY KEY (dt, charger_id)
);

CREATE TABLE ads_gov_service (
    dt TEXT NOT NULL,
    district TEXT NOT NULL,
    station_cnt INTEGER NOT NULL,
    charger_cnt INTEGER NOT NULL,
    order_cnt INTEGER NOT NULL,
    energy_kwh REAL NOT NULL,
    revenue_fen INTEGER NOT NULL,
    served_user_cnt INTEGER NOT NULL,
    carbon_reduction_kg REAL NOT NULL,
    PRIMARY KEY (dt, district)
);
