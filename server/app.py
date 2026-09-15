"""第二阶段 Flask 后端入口 —— 单进程同时提供 `/api/*` 与托管前端 `web/dist`。

契约：`docs/design/part2-api-contract.md`（23 个端点，全部 GET、全部只读）。

设计依据《02-TL-Hadoop平台与Flask后端设计》§3.2 / §3.3：
    ADS 以 `handoff/ads/ads.db`（SQLite 单文件）结果物化，Flask 用标准库 `sqlite3`
    只读打开；进程内不起 Spark、不连 MySQL、不用 JDBC，因此集成机上只需 Python + Flask。

本地起服务：
    python server/app.py                     # 默认 0.0.0.0:5000
    PORT=8000 python server/app.py           # 换端口
    ADS_DB=/path/to/ads.db python server/app.py

前端切换真实接口（组件代码不改）：
    VITE_USE_MOCK=false VITE_API_BASE=http://localhost:5000/api npm run dev
    或在 web/.env.local 里写 VITE_USE_MOCK=false
"""

from __future__ import annotations

import sys
from pathlib import Path

# 允许 `python server/app.py` 与 `python -m server.app` 两种启动方式
sys.path.insert(0, str(Path(__file__).resolve().parent))

from flask import Flask, jsonify, request, send_from_directory  # noqa: E402
from flask_cors import CORS  # noqa: E402

from api import enterprise, forecast, gov, overview, quality, station, user  # noqa: E402
from services import ads_reader as ads  # noqa: E402
from services.envelope import fail  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
DIST_DIR = REPO_ROOT / "web" / "dist"

BLUEPRINTS = (overview, quality, enterprise, user, station, gov, forecast)

# 契约 §2–§7 的端点总数（含选做 /forecast/recommend）：
#   §2 概览+质量 6、§3 企业 5、§4 用户 4、§5 站点 4、§6 政府 5、§7 预测 2，另加选做 1。
# 前端 mock 侧对应 23 个 JSON 文件（`station_detail.json` 一个文件覆盖站点三条端点）。
CONTRACT_ENDPOINT_COUNT = 27


def create_app() -> Flask:
    app = Flask(__name__, static_folder=None)
    # 中文直出，便于 curl / 答辩现场排错时肉眼核对字段
    app.json.ensure_ascii = False
    app.config["JSON_SORT_KEYS"] = False

    # 前端 dev server 与 Flask 分端口跑，开放 /api/* 跨域；只读接口无副作用
    CORS(app, resources={r"/api/*": {"origins": "*"}})

    for module in BLUEPRINTS:
        app.register_blueprint(module.bp, url_prefix="/api")

    app.teardown_appcontext(ads.close_connection)

    # ---- 错误统一走信封（契约 §1：失败不得返回 200 + 空 data） ----
    @app.errorhandler(ads.ApiError)
    def _api_error(err: ads.ApiError):
        return fail(err.code, err.message), err.http_status or 200

    @app.errorhandler(ads.AdsUnavailable)
    def _ads_unavailable(err: ads.AdsUnavailable):
        return fail(5001, str(err))

    @app.errorhandler(Exception)
    def _unexpected(err: Exception):
        app.logger.exception("未捕获异常")
        return fail(5000, f"服务内部错误：{type(err).__name__}: {err}")

    @app.errorhandler(404)
    def _not_found(_err):
        if request.path.startswith("/api/"):
            return fail(4004, f"接口不存在：{request.path}")
        return _spa_index()

    # ---- 联调自检端点（非契约端点，仅用于排障） ----
    @app.get("/api/health")
    def health():
        try:
            tables = {
                "ads_station": ads.table_count("ads_station"),
                "ads_charger": ads.table_count("ads_charger"),
                "ads_daily": ads.table_count("ads_daily"),
                "ads_station_hourly": ads.table_count("ads_station_hourly"),
                "ads_user_rfm": ads.table_count("ads_user_rfm"),
                "ads_district": ads.table_count("ads_district"),
                "ads_quality_issue": ads.table_count("ads_quality_issue"),
                "ads_forecast_24h": ads.table_count("ads_forecast_24h"),
                "ads_forecast_metric": ads.table_count("ads_forecast_metric"),
                "ads_event": ads.table_count("ads_event"),
            }
            meta = ads.meta_map()
        except ads.AdsUnavailable as err:
            return fail(5001, str(err))
        registered = sorted(
            rule.rule for rule in app.url_map.iter_rules() if str(rule).startswith("/api/")
        )
        return jsonify({
            "code": 0,
            "message": "ok",
            "data": {
                "status": "up",
                "dbPath": str(ads.db_path()),
                "dbExists": ads.db_path().is_file(),
                "runId": meta.get("runId", ""),
                "dataWindow": {
                    "start": meta.get("dataWindowStart", ""),
                    "end": meta.get("dataWindowEnd", ""),
                    "days": int(meta.get("windowDays", 0) or 0),
                },
                "tables": tables,
                "apiRouteCount": len(registered),
                "contractEndpointCount": CONTRACT_ENDPOINT_COUNT,
            },
            "generatedAt": ads.generated_at(),
        })

    # ---- 静态托管（契约 §8：Flask 直接托管 web/dist，无需 Node 运行时） ----
    @app.get("/")
    def root():
        return _spa_index()

    @app.get("/<path:filename>")
    def assets(filename: str):
        if request.path.startswith("/api/"):
            return fail(4004, f"接口不存在：{request.path}")
        target = DIST_DIR / filename
        if target.is_file():
            return send_from_directory(DIST_DIR, filename)
        # hash 路由：未知路径回落 index.html
        return _spa_index()

    return app


def _spa_index():
    index = DIST_DIR / "index.html"
    if not index.is_file():
        return (
            "<h1>前端产物未构建</h1>"
            f"<p>请先在 <code>web/</code> 下执行 <code>npm run build</code>，"
            f"产物将输出到 <code>{DIST_DIR}</code>。</p>"
            "<p>接口本身可用：<a href='/api/health'>/api/health</a></p>",
            200,
            {"Content-Type": "text/html; charset=utf-8"},
        )
    return send_from_directory(DIST_DIR, "index.html")


app = create_app()


if __name__ == "__main__":
    import os

    port = int(os.environ.get("PORT", "5000"))
    app.run(host=os.environ.get("HOST", "0.0.0.0"), port=port, threaded=True)
