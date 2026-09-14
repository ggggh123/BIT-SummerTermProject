"""ADS 只读访问层 —— Flask 侧唯一的数据库入口。

设计依据《02-TL-Hadoop平台与Flask后端设计》§3.2：
    ADS 指标在 HDFS 以 Parquet 物化后**导出为 SQLite 单文件**（`handoff/ads/ads.db`），
    Flask 用标准库 `sqlite3` 只读打开、进程内不起 Spark、不连 MySQL / 不用 JDBC。
    这是刻意的「结果物化」性能取舍：大屏 5s 轮询只需回放已算好的指标。

安全与健壮约定（对齐 `docs/design/part2-api-contract.md` §1）：
  * 连接以 `file:...?mode=ro` + `PRAGMA query_only` 双重只读，杜绝大屏写入；
  * 所有 SQL 均参数化，排序 / 筛选字段走白名单映射（`SORTABLE`），不拼接前端入参；
  * 金额一律整数分、时间一律 `+08:00` ISO 8601、百分比为 0–100 数值。
"""

from __future__ import annotations

import math
import os
import sqlite3
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence

from flask import g

CN_TZ = timezone(timedelta(hours=8))

# 默认库位置：<repo>/handoff/ads/ads.db（可用环境变量 ADS_DB 覆盖）
DEFAULT_DB = Path(__file__).resolve().parents[2] / "handoff" / "ads" / "ads.db"

# 演示参考点：天安门（用户视角「距离」口径，见契约 §4）
TIANANMEN = {"lat": 39.9087, "lng": 116.3975}

# 全窗口天数（KPI 与 90 天趋势共用同一窗口，保证「总览 = 明细合计」可对账）
WINDOW_DAYS = 90

# RFM 八分层的展示顺序（前端按此顺序渲染图例，与 mock 一致）
RFM_SEGMENTS = [
    "重要价值客户",
    "重要保持客户",
    "重要发展客户",
    "重要挽留客户",
    "一般价值客户",
    "一般保持客户",
    "一般发展客户",
    "一般挽留客户",
]

# 排序字段白名单：{对外参数值: SQL 输出别名}，禁止把前端入参直接拼进 SQL。
# 当前前端（`web/src/api/endpoints.js`）不传 sort/order，保留白名单是为了
# 「即使有人加了参数也不会注入」的默认安全；未命中的值一律回落到默认列。
SORTABLE = {
    "station-ranking": {
        "revenueFen": "revenue_fen",
        "orderCount": "order_count",
        "utilizationRate": "avg_util",
        "idleCount": "idle_cnt",
        "chargerCount": "charger_cnt",
    },
}


class AdsUnavailable(RuntimeError):
    """ADS 数据库缺失或不可读。"""


class ApiError(RuntimeError):
    """业务错误：以非 0 code 返回，前端保留上次成功数据（契约 §1）。"""

    def __init__(self, code: int, message: str, http_status: int = 200):
        super().__init__(message)
        self.code = code
        self.message = message
        self.http_status = http_status


# --------------------------------------------------------------------------- #
# 连接（每请求一个，请求结束随应用上下文回收）
# --------------------------------------------------------------------------- #
def db_path() -> Path:
    return Path(os.environ.get("ADS_DB") or DEFAULT_DB)


def get_connection() -> sqlite3.Connection:
    conn = getattr(g, "_ads_conn", None)
    if conn is not None:
        return conn
    path = db_path()
    if not path.is_file():
        raise AdsUnavailable(
            f"ADS 数据库不存在：{path}；请先运行 "
            f"`python server/tools/build_ads_db.py --ods handoff/ods --out handoff/ads`"
        )
    # as_uri() 给出 file:///C:/... 形式；补 ?mode=ro 走只读 URI 通道
    conn = sqlite3.connect(f"{path.as_uri()}?mode=ro", uri=True, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA query_only = ON")
    g._ads_conn = conn
    return conn


def close_connection(_exc: BaseException | None = None) -> None:
    conn = getattr(g, "_ads_conn", None)
    if conn is not None:
        conn.close()
        g._ads_conn = None


def _fetchall(sql: str, params: Sequence[Any] = ()) -> list[sqlite3.Row]:
    return get_connection().execute(sql, params).fetchall()


def query(sql: str, params: Sequence[Any] = ()) -> list[dict]:
    return [dict(row) for row in _fetchall(sql, params)]


def query_one(sql: str, params: Sequence[Any] = ()) -> dict | None:
    row = get_connection().execute(sql, params).fetchone()
    return dict(row) if row is not None else None


def scalar(sql: str, params: Sequence[Any] = (), default: Any = 0) -> Any:
    row = get_connection().execute(sql, params).fetchone()
    if row is None or row[0] is None:
        return default
    return row[0]


def table_count(name: str) -> int:
    """表行数；表不存在时返回 0（契约容错用）。"""
    try:
        return int(scalar(f"SELECT COUNT(*) FROM {name}", (), 0))
    except sqlite3.Error:
        return 0


# --------------------------------------------------------------------------- #
# 元数据 / 口径说明
# --------------------------------------------------------------------------- #
def meta_map() -> dict[str, str]:
    cached = getattr(g, "_ads_meta", None)
    if cached is not None:
        return cached
    try:
        cached = {row["key"]: row["value"] for row in _fetchall("SELECT key, value FROM ads_meta")}
    except (sqlite3.Error, AdsUnavailable):
        cached = {}
    g._ads_meta = cached
    return cached


def meta(key: str, default: str | None = None) -> str | None:
    return meta_map().get(key, default)


def generated_at() -> str:
    """响应信封的 generatedAt：优先用 ADS 物化时间，保证与前端口径一致。"""
    cached = getattr(g, "_ads_generated_at", None)
    if cached:
        return cached
    value = meta("generatedAt")
    result = (
        ensure_iso(value)
        if value
        else datetime.now(CN_TZ).replace(microsecond=0).isoformat(timespec="seconds")
    )
    g._ads_generated_at = result
    return result


# --------------------------------------------------------------------------- #
# 口径工具
# --------------------------------------------------------------------------- #
def ensure_iso(value: Any) -> str:
    """把库里的时间去成 `+08:00` ISO 8601 字符串（契约 §1）。"""
    if value is None:
        return ""
    if isinstance(value, datetime):
        dt = value
    else:
        text = str(value).strip()
        if not text:
            return ""
        if len(text) == 10:  # 纯日期
            return f"{text}T00:00:00+08:00"
        try:
            dt = datetime.fromisoformat(text.replace("Z", "+00:00"))
        except ValueError:
            return text
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=CN_TZ)
    return dt.astimezone(CN_TZ).replace(microsecond=0).isoformat(timespec="seconds")


def haversine_km(lat1: float, lng1: float, lat2: float, lng2: float) -> float:
    """两点球面距离（km），保留 2 位小数。

    注意保留 2 位：前端 `userStationModels.test.mjs` / `viewModel.test.mjs`
    按 2 位小数校验距离排序，多给或少给都会红（《01-PM》§3.3 注）。
    """
    radius_km = 6371.0
    d_lat = math.radians(lat2 - lat1)
    d_lng = math.radians(lng2 - lng1)
    a = (
        math.sin(d_lat / 2) ** 2
        + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(d_lng / 2) ** 2
    )
    return round(2 * radius_km * math.asin(math.sqrt(a)), 2)


def round_half_up(value: float, digits: int = 1) -> float:
    return float(f"{value:.{digits}f}")


def pct(numerator: float, denominator: float, digits: int = 1) -> float:
    if not denominator:
        return 0.0
    return round_half_up(numerator / denominator * 100.0, digits)


def hour_label(hour: int) -> str:
    return f"{hour:02d}:00"


def latest_hourly_day() -> str | None:
    return scalar("SELECT MAX(dt) FROM ads_station_hourly", (), None)


def window_days() -> int:
    total = int(scalar("SELECT COUNT(*) FROM ads_daily", (), 0))
    return total or WINDOW_DAYS


def daily_window(days: int | None = None) -> list[dict]:
    """最近 N 天的日汇总，按 dt 升序返回（KPI 与趋势共用，保证可对账）。"""
    limit = days or WINDOW_DAYS
    rows = query(
        "SELECT dt, revenue_fen, energy_kwh, order_cnt, new_user_cnt, active_user_cnt "
        "FROM (SELECT * FROM ads_daily ORDER BY dt DESC LIMIT ?) ORDER BY dt ASC",
        (limit,),
    )
    return rows


def window_totals(days: int | None = None) -> dict:
    """窗口合计 —— KPI、碳减排、营收趋势共用**唯一**口径。

    电量刻意「先把逐日值取 1 位小数、再求和、最后取 1 位」：
    契约要求能量逐日保留 1 位，若改成「先求和再取 1 位」，KPI 会与前端
    「逐日值相加」的结果差 0.1，触发前端自校验（KPI = 趋势逐日汇总）红灯。
    """
    rows = daily_window(days)
    return {
        "days": len(rows),
        "revenueFen": sum(int(row["revenue_fen"]) for row in rows),
        "energyKwh": round(sum(round(float(row["energy_kwh"]), 1) for row in rows), 1),
        "orderCnt": sum(int(row["order_cnt"]) for row in rows),
    }


def station_utilization() -> dict[int, float]:
    """站点利用率：窗口内日均利用率（station_day.avg_utilization 的均值）。"""
    rows = query(
        "SELECT station_id, AVG(avg_utilization) AS util FROM ads_station_day GROUP BY station_id"
    )
    return {int(r["station_id"]): round_half_up(float(r["util"] or 0.0), 1) for r in rows}


def station_orders() -> dict[int, int]:
    rows = query(
        "SELECT station_id, SUM(order_cnt) AS cnt FROM ads_station_day GROUP BY station_id"
    )
    return {int(r["station_id"]): int(r["cnt"] or 0) for r in rows}


def station_revenue() -> dict[int, int]:
    rows = query(
        "SELECT station_id, SUM(revenue_fen) AS fen FROM ads_station_day GROUP BY station_id"
    )
    return {int(r["station_id"]): int(r["fen"] or 0) for r in rows}


def stations() -> list[dict]:
    return query(
        "SELECT station_id, name, district, longitude, latitude, price_fen_per_kwh, "
        "forecast_enabled, charger_cnt, fast_cnt, slow_cnt, fast_power_kw, slow_power_kw, "
        "idle_cnt, reserved_cnt, charging_cnt, fault_cnt, restarting_cnt, service_radius_km, "
        "coord_imputed FROM ads_station ORDER BY station_id"
    )


def station_or_404(station_id: int) -> dict:
    row = query_one("SELECT * FROM ads_station WHERE station_id = ?", (station_id,))
    if row is None:
        raise ApiError(4004, f"充电站不存在：{station_id}（有效范围 1–8）")
    return row


def charger_counts() -> dict[str, int]:
    row = query_one(
        "SELECT SUM(idle_cnt) AS idle, SUM(reserved_cnt) AS reserved, SUM(charging_cnt) AS charging, "
        "SUM(fault_cnt) AS fault, SUM(restarting_cnt) AS restarting, SUM(charger_cnt) AS total "
        "FROM ads_station"
    ) or {}
    return {key: int(row.get(key) or 0) for key in ("idle", "reserved", "charging", "fault",
                                                    "restarting", "total")}


# --------------------------------------------------------------------------- #
# 参数校验
# --------------------------------------------------------------------------- #
def parse_days(raw: str | None, default: int = 30, allowed: Iterable[int] = (7, 30, 90)) -> int:
    if raw is None or str(raw).strip() == "":
        return default
    try:
        value = int(str(raw))
    except ValueError:
        raise ApiError(4001, f"days 参数非法：{raw}（可选 {list(allowed)}）")
    if value not in set(allowed):
        raise ApiError(4001, f"days 参数非法：{value}（可选 {list(allowed)}）")
    return value


def parse_limit(raw: str | None, default: int, maximum: int = 50) -> int:
    if raw is None or str(raw).strip() == "":
        return default
    try:
        value = int(str(raw))
    except ValueError:
        raise ApiError(4001, f"limit 参数非法：{raw}")
    if value <= 0:
        raise ApiError(4001, f"limit 参数非法：{value}（应为正整数）")
    return min(value, maximum)


def parse_station_id(raw: Any) -> int:
    try:
        value = int(str(raw))
    except (TypeError, ValueError):
        raise ApiError(4001, f"stationId 参数非法：{raw}")
    if not 1 <= value <= 8:
        raise ApiError(4004, f"充电站不存在：{value}（有效范围 1–8）")
    return value


def sort_key(family: str, raw: str | None, default: str) -> str:
    """把前端排序参数映射到白名单列名，命中不了就回落到默认列。"""
    if not raw:
        return default
    return SORTABLE.get(family, {}).get(str(raw), default)


def sort_dir(raw: str | None, default: str = "desc") -> str:
    value = (raw or default).strip().lower()
    return "ASC" if value in ("asc", "up", "1") else "DESC"
