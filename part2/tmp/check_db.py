"""校验 forecast.db 的三张预测契约表（#5 自测脚本，非交付物）。

列集依据 #4 的 part2/scml/warehouse/sql/ads_schema.sql §7。
"""
import sqlite3
import sys

db = sys.argv[1] if len(sys.argv) > 1 else "./handoff/forecast/forecast.db"
con = sqlite3.connect(db)

tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
print("TABLES =", tables)

EXPECT = {
    "ads_forecast_batch": ["run_id", "model_version", "activated_at", "source",
                           "horizon_h_max", "is_baseline", "note"],
    "ads_forecast_24h": ["run_id", "station_id", "forecast_at", "horizon_h", "predicted_load_kw",
                         "predicted_busy_count", "predicted_idle_count", "congestion_level", "is_peak"],
    "ads_forecast_metric": ["horizon_h", "mae", "rmse", "wape", "baseline_wape"],
}

for t, expected in EXPECT.items():
    if t not in tables:
        print(f"[MISSING] {t}")
        continue
    cols = [r[1] for r in con.execute(f"PRAGMA table_info({t})")]
    n = con.execute(f"SELECT COUNT(*) FROM {t}").fetchone()[0]
    ok = "OK " if cols == expected else "COLS_MISMATCH"
    print(f"[{ok}] {t}: rows={n}")
    print(f"     cols={cols}")
    if cols != expected:
        print(f"     expect={expected}")

print("--- ads_forecast_batch ---")
for row in con.execute("SELECT run_id, model_version, activated_at, source, horizon_h_max, "
                       "is_baseline, note FROM ads_forecast_batch"):
    print("   ", row)

print("--- ads_forecast_24h 样例 (station=1, h=1..6) ---")
for row in con.execute(
    "SELECT station_id, forecast_at, horizon_h, predicted_load_kw, predicted_busy_count,"
    " predicted_idle_count, congestion_level, is_peak"
    " FROM ads_forecast_24h WHERE station_id=1 ORDER BY horizon_h LIMIT 6"
):
    print("   ", row)

print("--- ads_forecast_24h 唯一性/一致性 ---")
print("  rows =", con.execute("SELECT COUNT(*) FROM ads_forecast_24h").fetchone()[0])
print("  distinct run_id =", con.execute("SELECT COUNT(DISTINCT run_id) FROM ads_forecast_24h").fetchone()[0])
print("  distinct station =", con.execute("SELECT COUNT(DISTINCT station_id) FROM ads_forecast_24h").fetchone()[0])
print("  horizon range =", con.execute("SELECT MIN(horizon_h), MAX(horizon_h) FROM ads_forecast_24h").fetchone())
print("  is_peak 分布 =", con.execute("SELECT is_peak, COUNT(*) FROM ads_forecast_24h GROUP BY is_peak").fetchall())
print("  congestion 分布 =", con.execute(
    "SELECT congestion_level, COUNT(*) FROM ads_forecast_24h GROUP BY congestion_level").fetchall())
print("  busy+idle<=pile 违例行数 =", con.execute(
    "SELECT COUNT(*) FROM ads_forecast_24h f JOIN ads_station s ON s.station_id=f.station_id"
    " WHERE f.predicted_busy_count + f.predicted_idle_count > s.pile_count"
).fetchone()[0] if "ads_station" in tables else "N/A(无 ads_station 表)")
print("  wape<baseline_wape 的步长数 =", con.execute(
    "SELECT COUNT(*) FROM ads_forecast_metric WHERE wape < baseline_wape").fetchone()[0],
    "/", con.execute("SELECT COUNT(*) FROM ads_forecast_metric").fetchone()[0])

print("--- ads_forecast_metric (h=1,6,24) ---")
for row in con.execute("SELECT horizon_h, mae, rmse, wape, baseline_wape FROM ads_forecast_metric"
                       " WHERE horizon_h IN (1,6,24) ORDER BY horizon_h"):
    print("   ", row)

con.close()
print("CHECK_DONE")
