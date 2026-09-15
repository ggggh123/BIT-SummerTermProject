"""校验 forecast.db 的两张契约表（#5 自测脚本，非交付物）。"""
import sqlite3
import sys

db = sys.argv[1] if len(sys.argv) > 1 else "./handoff/forecast/forecast.db"
con = sqlite3.connect(db)
tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
print("TABLES =", tables)

for t in ("ads_forecast_24h", "ads_forecast_metric"):
    if t not in tables:
        print(f"[MISSING] {t}")
        continue
    cols = [r[1] for r in con.execute(f"PRAGMA table_info({t})")]
    n = con.execute(f"SELECT COUNT(*) FROM {t}").fetchone()[0]
    print(f"[OK] {t}: rows={n}")
    print(f"     cols={cols}")

print("--- ads_forecast_24h 样例（station 1）---")
for row in con.execute(
    "SELECT station_id, forecast_at, horizon_h, predicted_load_kw, predicted_busy_count,"
    " predicted_idle_count, congestion_level, is_peak"
    " FROM ads_forecast_24h WHERE station_id=1 ORDER BY horizon_h LIMIT 8"
):
    print("   ", row)

print("--- ads_forecast_metric 样例 ---")
for row in con.execute(
    "SELECT horizon_h, mae, rmse, wape, baseline_wape FROM ads_forecast_metric"
    " ORDER BY horizon_h LIMIT 6"
):
    print("   ", row)

print("--- is_peak 统计 ---")
print(con.execute("SELECT is_peak, COUNT(*) FROM ads_forecast_24h GROUP BY is_peak").fetchall())
print("--- run_id ---")
print(con.execute("SELECT DISTINCT run_id FROM ads_forecast_24h").fetchall())
con.close()
print("CHECK_DONE")
