"""预测端点（契约 §7）。

从 ``ads_forecast_batch`` 读取最近激活的预测批次；它可以是明确标识的基线，
也可以是 Spark MLlib 训练产物，``isBaseline`` 会如实透传给前端。**没有有效
批次时返回 ``code: 4041``**，前端据此显示「暂无预测」，绝不伪造曲线。
"""

from __future__ import annotations

from flask import Blueprint

from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("forecast", __name__)


def _active_batch() -> dict | None:
    return ads.query_one(
        "SELECT run_id, model_version, activated_at, source, horizon_h_max, is_baseline, note "
        "FROM ads_forecast_batch ORDER BY activated_at DESC LIMIT 1"
    )


@bp.get("/forecast/24h")
def forecast_24h():
    batch = _active_batch()
    if batch is None:
        return _no_forecast()
    rows = ads.query(
        "SELECT station_id, forecast_at, horizon_h, predicted_load_kw, predicted_busy_count, "
        "predicted_idle_count, congestion_level, is_peak FROM ads_forecast_24h "
        "WHERE run_id = ? ORDER BY station_id ASC, horizon_h ASC",
        (batch["run_id"],),
    )
    return ok({
        "runId": batch["run_id"],
        "modelVersion": batch["model_version"],
        "activatedAt": ads.ensure_iso(batch["activated_at"]),
        "isBaseline": bool(batch["is_baseline"]),
        "source": batch["source"],
        "note": batch["note"],
        "points": [
            {
                "stationId": row["station_id"],
                "forecastAt": ads.ensure_iso(row["forecast_at"]),
                "horizonH": int(row["horizon_h"]),
                "predictedLoadKw": round(float(row["predicted_load_kw"]), 1),
                "predictedBusyCount": int(row["predicted_busy_count"]),
                "predictedIdleCount": int(row["predicted_idle_count"]),
                "congestionLevel": row["congestion_level"],
                "isPeak": bool(row["is_peak"]),
            }
            for row in rows
        ],
    })


@bp.get("/forecast/metrics")
def forecast_metrics():
    batch = _active_batch()
    if batch is None:
        return _no_forecast()
    rows = ads.query(
        "SELECT horizon_h, mae, rmse, wape, baseline_wape FROM ads_forecast_metric "
        "ORDER BY horizon_h ASC"
    )
    return ok({
        "runId": batch["run_id"],
        "modelVersion": batch["model_version"],
        "horizons": [
            {
                "horizonH": int(row["horizon_h"]),
                "mae": float(row["mae"]),
                "rmse": float(row["rmse"]),
                "wape": float(row["wape"]),
                "baselineWape": float(row["baseline_wape"]),
            }
            for row in rows
        ],
    })


@bp.get("/forecast/recommend")
def forecast_recommend():
    """推荐空闲时段（契约 §4 选做）：取每站最近一个预测时刻的空闲桩与拥堵等级。"""
    batch = _active_batch()
    if batch is None:
        return _no_forecast()
    rows = ads.query(
        "SELECT f.station_id, s.name, f.predicted_idle_count, f.congestion_level "
        "FROM ads_forecast_24h f JOIN ads_station s ON s.station_id = f.station_id "
        "WHERE f.run_id = ? AND f.horizon_h = 1 ORDER BY f.predicted_idle_count DESC",
        (batch["run_id"],),
    )
    return ok([
        {
            "stationId": row["station_id"],
            "name": row["name"],
            "predictedIdleCount": int(row["predicted_idle_count"]),
            "congestionLevel": row["congestion_level"],
        }
        for row in rows
    ])


def _no_forecast():
    """无有效预测批次：按契约返回非 0 code，前端显示「暂无预测」。"""
    from services.envelope import fail

    return fail(4041, "no active forecast")
