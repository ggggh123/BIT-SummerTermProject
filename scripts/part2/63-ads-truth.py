"""63-ads-truth.py —— 导出 ADS 真值（抽验表的「ADS 真值」那一段）。

`part2-metric-checklist.md` 的抽验方法分三段取值，本脚本负责第一段：
直接从 `handoff/ads/ads.db` 取真值，避免"拿接口返回值去核接口返回的大屏"这种自证。

用法：
    python scripts/part2/63-ads-truth.py                     # 打印到 stdout
    python scripts/part2/63-ads-truth.py --out runtime/ads-truth.json
    python scripts/part2/63-ads-truth.py --db /path/to/ads.db --only kpis,monthly

段落：kpis / monthly / chargers / stations / districts / rfm / price /
      peak / quality / forecast（--only 按逗号选择，默认全部）
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DB = REPO_ROOT / "handoff" / "ads" / "ads.db"

SECTIONS = ("kpis", "monthly", "chargers", "stations", "districts",
            "rfm", "price", "peak", "quality", "forecast")


def connect(path: Path) -> sqlite3.Connection:
    if not path.is_file():
        raise SystemExit(f"找不到 ADS 交接库：{path}")
    con = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    con.row_factory = sqlite3.Row
    return con


def has_table(con: sqlite3.Connection, name: str) -> bool:
    row = con.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (name,)
    ).fetchone()
    return row is not None


def rows(con: sqlite3.Connection, sql: str) -> list[dict]:
    return [dict(r) for r in con.execute(sql)]


def section_kpis(con: sqlite3.Connection) -> dict:
    meta = dict(con.execute("SELECT key, value FROM ads_meta"))
    daily = con.execute(
        "SELECT COUNT(*) n, SUM(order_cnt) orders, SUM(revenue_fen) revenue_fen,"
        " SUM(energy_kwh) energy_kwh, MIN(dt) d0, MAX(dt) d1 FROM ads_daily"
    ).fetchone()
    last30 = con.execute(
        "SELECT SUM(order_cnt) orders, SUM(revenue_fen) revenue_fen,"
        " SUM(energy_kwh) energy_kwh FROM ads_daily"
        " WHERE dt >= (SELECT DATE(MAX(dt), '-29 day') FROM ads_daily)"
    ).fetchone()
    return {
        "runId": meta.get("runId"),
        "generatedAt": meta.get("generatedAt"),
        "sourceRunId": meta.get("sourceRunId"),
        "window": {"days": daily["n"], "start": daily["d0"], "end": daily["d1"]},
        "totals": {
            "orders": daily["orders"],
            "revenueFen": daily["revenue_fen"],
            "revenueYuan": round(daily["revenue_fen"] / 100.0, 2),
            "energyKwh": round(daily["energy_kwh"], 3),
        },
        "last30": {
            "orders": last30["orders"],
            "revenueFen": last30["revenue_fen"],
            "revenueYuan": round(last30["revenue_fen"] / 100.0, 2),
            "avgTicketFen": round(last30["revenue_fen"] / last30["orders"], 2),
            "avgTicketYuan": round(last30["revenue_fen"] / last30["orders"] / 100.0, 2),
        },
        "meta": {k: meta.get(k) for k in (
            "stationCount", "chargerCount", "userCount", "orderCount",
            "totalRevenueFen", "totalEnergyKwh", "forecastSource",
            "forecastIsBaseline", "cleaningSource")},
    }


def section_monthly(con: sqlite3.Connection) -> list[dict]:
    return rows(
        con,
        "SELECT SUBSTR(dt,1,7) month, SUM(order_cnt) orders,"
        " SUM(revenue_fen) revenue_fen, ROUND(SUM(energy_kwh),1) energy_kwh"
        " FROM ads_daily GROUP BY 1 ORDER BY 1",
    )


def section_chargers(con: sqlite3.Connection) -> dict:
    cols = [r[1] for r in con.execute("PRAGMA table_info(ads_station)")]
    states = [c for c in ("idle_cnt", "reserved_cnt", "charging_cnt",
                          "fault_cnt", "restarting_cnt") if c in cols]
    agg = ", ".join(f"SUM({c}) AS {c}" for c in states)
    row = con.execute(
        f"SELECT COUNT(*) stations, SUM(charger_cnt) total, {agg} FROM ads_station"
    ).fetchone()
    out = {k: row[k] for k in row.keys()}
    total = row["total"] or 0
    out["stateSum"] = sum(row[c] or 0 for c in states)
    out["stateSumEqualsTotal"] = out["stateSum"] == total
    out["onlineRatePct"] = round((total - (row["fault_cnt"] or 0)) / total * 100, 2) if total else None
    return out


def section_stations(con: sqlite3.Connection) -> dict:
    return {
        "ids": [r[0] for r in con.execute(
            "SELECT station_id FROM ads_station ORDER BY station_id")],
        "utilization": rows(
            con,
            "SELECT s.station_id, s.name, s.charger_cnt,"
            " ROUND(AVG(d.avg_utilization),2) util_avg,"
            " ROUND(MAX(d.avg_utilization),2) util_max"
            " FROM ads_station_day d JOIN ads_station s USING(station_id)"
            " GROUP BY 1,2,3 ORDER BY util_avg DESC",
        ),
        "revenueTop": rows(
            con,
            "SELECT s.station_id, s.name, SUM(d.revenue_fen) revenue_fen,"
            " SUM(d.order_cnt) orders FROM ads_station_day d"
            " JOIN ads_station s USING(station_id) GROUP BY 1,2"
            " ORDER BY revenue_fen DESC LIMIT 3",
        ),
        "idleTop": rows(
            con,
            "SELECT station_id, name, idle_cnt, charger_cnt,"
            " ROUND(100.0*idle_cnt/charger_cnt,1) idle_rate"
            " FROM ads_station ORDER BY idle_cnt DESC LIMIT 3",
        ),
        "forecastEnabledIds": [r[0] for r in con.execute(
            "SELECT station_id FROM ads_station WHERE forecast_enabled=1"
            " ORDER BY station_id")],
    }


def section_districts(con: sqlite3.Connection) -> dict:
    return {
        "rows": rows(
            con,
            "SELECT district, station_cnt, charger_cnt, population, order_cnt,"
            " utilization_rate, ROUND(energy_kwh,1) energy_kwh, revenue_fen,"
            " ROUND(co2_saved_kg,2) co2_saved_kg FROM ads_district"
            " ORDER BY utilization_rate DESC",
        ),
        "co2TotalKg": con.execute("SELECT SUM(co2_saved_kg) FROM ads_district").fetchone()[0],
    }


def section_rfm(con: sqlite3.Connection) -> list[dict]:
    return rows(
        con,
        "SELECT segment, user_cnt, revenue_fen,"
        " ROUND(avg_monetary_fen,1) avg_monetary_fen FROM ads_user_rfm"
        " ORDER BY user_cnt DESC",
    )


def section_price(con: sqlite3.Connection) -> dict:
    row = con.execute(
        "SELECT ROUND(AVG(price_fen_per_kwh),3) avg, MIN(price_fen_per_kwh) min,"
        " MAX(price_fen_per_kwh) max, COUNT(*) n FROM ads_station"
    ).fetchone()
    out = {k: row[k] for k in row.keys()}
    out["perStation"] = rows(
        con,
        "SELECT station_id, name, price_fen_per_kwh FROM ads_station ORDER BY station_id",
    )
    return out


def section_peak(con: sqlite3.Connection) -> dict:
    return {
        "stationHourTop": rows(
            con,
            "SELECT station_id, dt, hour, load_kw FROM ads_station_hourly"
            " ORDER BY load_kw DESC LIMIT 3",
        ),
        "cityHourTop": rows(
            con,
            "SELECT dt, hour, ROUND(SUM(load_kw),3) total_load_kw"
            " FROM ads_station_hourly GROUP BY dt, hour"
            " ORDER BY total_load_kw DESC LIMIT 3",
        ),
        "note": "接口 /api/gov/peak-load 只返回最近一天，大屏 KPI 因此是当日口径；"
                "窗口内最大是 cityHourTop[0]",
    }


def section_quality(con: sqlite3.Connection) -> dict:
    out: dict = {}
    if has_table(con, "ads_quality_issue"):
        out["issues"] = rows(con, "SELECT * FROM ads_quality_issue ORDER BY rule")
        out["injectedTotal"] = sum(r["injected"] or 0 for r in out["issues"])
        out["detectedTotal"] = sum(r["detected"] or 0 for r in out["issues"])
        out["zeroRecallRules"] = [r["rule"] for r in out["issues"] if not r["detected"]]
    if has_table(con, "ads_quality_table"):
        out["tables"] = rows(
            con,
            "SELECT name, rows_before, rows_after FROM ads_quality_table ORDER BY name",
        )
        out["note"] = ("rows_before/after 来自 export_ads_db.py 的 Python 旁路复算，"
                       "与业务表（PRL 清洗口径）不是同一套结果")
    return out


def section_forecast(con: sqlite3.Connection) -> dict:
    out: dict = {
        "meta": dict(con.execute(
            "SELECT key, value FROM ads_meta WHERE key LIKE 'forecast%'"))
    }
    for table in ("ads_forecast_batch", "ads_forecast_metric", "ads_forecast_24h"):
        if not has_table(con, table):
            out[table] = {"error": "表不存在"}
            continue
        entry: dict = {"rows": con.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]}
        if table == "ads_forecast_batch":
            entry["batch"] = rows(con, f"SELECT * FROM {table}")
        elif table == "ads_forecast_metric":
            entry["head"] = rows(
                con,
                f"SELECT * FROM {table} WHERE horizon_h IN (1,6,12,24)"
                " ORDER BY horizon_h",
            )
        else:
            entry["stations"] = [r[0] for r in con.execute(
                f"SELECT DISTINCT station_id FROM {table} ORDER BY station_id")]
            entry["peakRows"] = con.execute(
                f"SELECT COUNT(*) FROM {table} WHERE is_peak=1").fetchone()[0]
            entry["congestion"] = rows(
                con,
                f"SELECT congestion_level, COUNT(*) n FROM {table}"
                " GROUP BY 1 ORDER BY 1",
            )
        out[table] = entry
    return out


BUILDERS = {
    "kpis": section_kpis,
    "monthly": section_monthly,
    "chargers": section_chargers,
    "stations": section_stations,
    "districts": section_districts,
    "rfm": section_rfm,
    "price": section_price,
    "peak": section_peak,
    "quality": section_quality,
    "forecast": section_forecast,
}


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--db", type=Path, default=DEFAULT_DB, help="ADS 交接库路径")
    ap.add_argument("--out", type=Path, help="输出 JSON 路径（默认打印到 stdout）")
    ap.add_argument("--only", default="", help="只导出指定段落，逗号分隔")
    args = ap.parse_args()

    wanted = [s.strip() for s in args.only.split(",") if s.strip()] or list(SECTIONS)
    unknown = [s for s in wanted if s not in BUILDERS]
    if unknown:
        raise SystemExit(f"未知段落 {unknown}；可选：{', '.join(SECTIONS)}")

    con = connect(args.db)
    try:
        payload = {"db": str(args.db), "sections": {}}
        for name in wanted:
            payload["sections"][name] = BUILDERS[name](con)
    finally:
        con.close()

    text = json.dumps(payload, ensure_ascii=False, indent=1)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text + "\n", encoding="utf-8")
        print(f"[ads-truth] 已写出 {args.out}（段落：{', '.join(wanted)}）")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
