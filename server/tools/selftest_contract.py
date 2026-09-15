"""契约一致性自测 —— 交付前必跑。

    python server/tools/selftest_contract.py

覆盖三类检查（对应 `docs/design/part2-api-contract.md`）：
  1. **信封与可访问性**：契约 §2–§7 的 27 条路由全部返回 `code=0`、`data` 非空、
     `generatedAt` 为 `+08:00` ISO 8601；参数越界 / 站点越界 / 未知路径返回非 0 code。
  2. **口径自洽**（前端会自校验的硬约束）：
       * KPI 总营收 / 总订单 / 总电量 = 90 天趋势逐日汇总
       * co2SavedTon = 电量/1000 × factorTonPerMwh
       * 五状态之和 = 总桩数；快慢桩数之和 = 该站总桩数
       * KPI 营收 = 各站营收之和
  3. **格式约定**：金额为整数分、时间为 `+08:00`、距离 2 位小数、排序单调、矩阵维度。

退出码：0 全通过；1 存在失败项。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app import app  # noqa: E402

ISO_RE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+08:00$")

_results: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    _results.append((name, bool(ok), detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  — {detail}" if detail else ""))


def get(client, path: str, **params):
    response = client.get(path, query_string=params)
    try:
        body = response.get_json()
    except Exception:  # noqa: BLE001
        body = None
    return body, response.status_code


def is_int(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


# --------------------------------------------------------------------------- #
# 1. 信封与可访问性
# --------------------------------------------------------------------------- #
BASE_CONTRACT_ROUTES = [
    ("/api/overview/kpis", {}),
    ("/api/overview/stations", {}),
    ("/api/overview/charger-status", {}),
    ("/api/overview/load-24h", {}),
    ("/api/overview/load-24h", {"stationId": 3}),
    ("/api/overview/events", {"limit": 10}),
    ("/api/quality/summary", {}),
    ("/api/enterprise/revenue-trend", {"days": 7}),
    ("/api/enterprise/revenue-trend", {"days": 30}),
    ("/api/enterprise/revenue-trend", {"days": 90}),
    ("/api/enterprise/station-ranking", {}),
    ("/api/enterprise/user-growth", {"days": 30}),
    ("/api/enterprise/user-rfm", {}),
    ("/api/enterprise/monthly", {}),
    ("/api/user/price-compare", {}),
    ("/api/user/price-distance", {}),
    ("/api/user/idle-ranking", {}),
    ("/api/user/peak-heatmap", {}),
    ("/api/station/coverage", {}),
    ("/api/gov/coverage", {}),
    ("/api/gov/service-stats", {}),
    ("/api/gov/carbon", {}),
    ("/api/gov/peak-load", {}),
    ("/api/gov/utilization", {}),
    ("/api/forecast/24h", {}),
    ("/api/forecast/metrics", {}),
    ("/api/forecast/recommend", {}),
]


def contract_routes(station_ids: list[int]) -> list[tuple[str, dict]]:
    """按当前 ADS 中真实存活站点构造明细路由，避免把演示夹具的 1–8 写死。"""
    station_routes = [
        (f"/api/station/{sid}/{suffix}", {})
        for sid in station_ids
        for suffix in ("utilization", "mix", "health")
    ]
    forecast_index = next(
        index for index, (path, _params) in enumerate(BASE_CONTRACT_ROUTES)
        if path == "/api/gov/coverage"
    )
    return (
        BASE_CONTRACT_ROUTES[:forecast_index]
        + station_routes
        + BASE_CONTRACT_ROUTES[forecast_index:]
    )


def discover_station_ids(client) -> list[int]:
    body, _status = get(client, "/api/overview/stations")
    if not body or body.get("code") != 0 or not isinstance(body.get("data"), list):
        return []
    return sorted(int(item["stationId"]) for item in body["data"])


def check_envelope(client, routes: list[tuple[str, dict]]) -> dict[str, dict]:
    payloads: dict[str, dict] = {}
    bad: list[str] = []
    for path, params in routes:
        body, status = get(client, path, **params)
        key = f"{path}?{params}" if params else path
        if body is None:
            bad.append(f"{key} 非 JSON")
            continue
        if body.get("code") != 0:
            bad.append(f"{key} code={body.get('code')} {body.get('message')}")
            continue
        if body.get("data") is None:
            bad.append(f"{key} data 为空")
            continue
        if not ISO_RE.match(str(body.get("generatedAt", ""))):
            bad.append(f"{key} generatedAt={body.get('generatedAt')!r}")
            continue
        if "message" not in body or status != 200:
            bad.append(f"{key} 信封字段缺失/HTTP {status}")
            continue
        payloads[key] = body["data"]
    check(
        f"信封与可访问性：{len(routes)} 条契约路由全部 code=0 / data 非空 / +08:00",
        not bad,
        "; ".join(bad[:4]),
    )
    return payloads


def check_errors(client, station_ids: list[int]) -> None:
    invalid_station_id = max(station_ids, default=0) + 100000
    cases = [
        ("days 越界 -> 4001", "/api/enterprise/revenue-trend", {"days": 15}, 4001),
        ("limit 非法 -> 4001", "/api/overview/events", {"limit": 0}, 4001),
        ("站点越界 -> 4004", f"/api/station/{invalid_station_id}/utilization", {}, 4004),
        ("站点非数字 -> 4004", "/api/station/abc/utilization", {}, 4004),
        ("未知接口 -> 4004", "/api/not-exist", {}, 4004),
    ]
    for label, path, params, expected in cases:
        body, _status = get(client, path, **params)
        code = (body or {}).get("code")
        check(f"错误码：{label}", code == expected, f"实际 code={code}")


# --------------------------------------------------------------------------- #
# 2. 口径自洽
# --------------------------------------------------------------------------- #
def check_consistency(payloads: dict[str, dict], station_ids: list[int]) -> None:
    kpis = payloads["/api/overview/kpis"]
    trend = payloads["/api/enterprise/revenue-trend?{'days': 90}"]
    points = trend["points"]

    check("KPI 窗口天数 = 90 天趋势点数", kpis["windowDays"] == len(points),
          f"{kpis['windowDays']} vs {len(points)}")
    check("KPI 总营收 = 90 天趋势逐日汇总",
          kpis["totalRevenueFen"] == sum(p["revenueFen"] for p in points),
          f"{kpis['totalRevenueFen']} vs {sum(p['revenueFen'] for p in points)}")
    check("KPI 总订单 = 90 天趋势逐日汇总",
          kpis["totalOrders"] == sum(p["orderCount"] for p in points),
          f"{kpis['totalOrders']} vs {sum(p['orderCount'] for p in points)}")
    energy_sum = round(sum(p["energyKwh"] for p in points), 1)
    check("KPI 总电量 = 90 天趋势逐日汇总",
          abs(kpis["totalEnergyKwh"] - energy_sum) <= 0.05,
          f"{kpis['totalEnergyKwh']} vs {energy_sum}")

    stations = {item["stationId"]: item for item in payloads["/api/overview/stations"]}
    check("KPI 总营收 = 各站营收之和",
          kpis["totalRevenueFen"] == sum(item["revenueFen"] for item in stations.values()),
          f"{kpis['totalRevenueFen']} vs {sum(i['revenueFen'] for i in stations.values())}")
    check("KPI 总订单 = 各站订单之和",
          kpis["totalOrders"] == sum(item["orderCount"] for item in stations.values()),
          f"{kpis['totalOrders']} vs {sum(i['orderCount'] for i in stations.values())}")

    status = payloads["/api/overview/charger-status"]
    status_sum = sum(status.values())
    check("五状态之和 = 总桩数", status_sum == kpis["chargerCount"],
          f"{status_sum} vs {kpis['chargerCount']}")
    check("KPI idleCount = charger-status.idle",
          kpis["idleCount"] == status["idle"], f"{kpis['idleCount']} vs {status['idle']}")
    check("各站 chargerCount 之和 = 总桩数",
          sum(item["chargerCount"] for item in stations.values()) == kpis["chargerCount"])

    for sid in station_ids:
        mix = payloads[f"/api/station/{sid}/mix"]
        total = stations[sid]["chargerCount"]
        check(f"站 {sid}：快慢桩数之和 = 该站总桩数",
              mix["fastCount"] + mix["slowCount"] == total,
              f"{mix['fastCount']}+{mix['slowCount']} vs {total}")
        check(f"站 {sid}：health.faultCount <= 总桩数",
              0 <= mix["fastCount"] <= total and payloads[f"/api/station/{sid}/health"]["faultCount"] <= total)

    carbon = payloads["/api/gov/carbon"]
    expected_co2 = round(carbon["totalEnergyKwh"] / 1000.0 * carbon["factorTonPerMwh"], 1)
    check("co2SavedTon = 电量/1000 × factorTonPerMwh",
          abs(carbon["co2SavedTon"] - expected_co2) <= 0.05,
          f"{carbon['co2SavedTon']} vs {expected_co2}")
    check("carbon.totalEnergyKwh = KPI 总电量",
          abs(carbon["totalEnergyKwh"] - kpis["totalEnergyKwh"]) <= 0.05,
          f"{carbon['totalEnergyKwh']} vs {kpis['totalEnergyKwh']}")

    quality = payloads["/api/quality/summary"]
    check("quality.issues 覆盖 R01–R10", len(quality["issues"]) == 10)
    check("quality 各规则 recall ∈ [0, 1]",
          all(0.0 <= item["recall"] <= 1.0 for item in quality["issues"]))
    check("quality.tables 含 ods_orders / ods_telemetry",
          {"ods_orders", "ods_telemetry"} <= {t["name"] for t in quality["tables"]})


# --------------------------------------------------------------------------- #
# 3. 格式与排序
# --------------------------------------------------------------------------- #
def check_formats(payloads: dict[str, dict]) -> None:
    stations = payloads["/api/overview/stations"]
    trend = payloads["/api/enterprise/revenue-trend?{'days': 90}"]
    ranking = payloads["/api/enterprise/station-ranking"]
    rfm = payloads["/api/enterprise/user-rfm"]
    monthly = payloads["/api/enterprise/monthly"]
    price_compare = payloads["/api/user/price-compare"]
    price_distance = payloads["/api/user/price-distance"]
    idle_ranking = payloads["/api/user/idle-ranking"]
    heatmap = payloads["/api/user/peak-heatmap"]
    events = payloads["/api/overview/events?{'limit': 10}"]
    load24h = payloads["/api/overview/load-24h"]

    money_ok = all([
        all(is_int(item["revenueFen"]) and is_int(item["priceFenPerKwh"]) for item in stations),
        all(is_int(item["revenueFen"]) for item in trend["points"]),
        all(is_int(item["revenueFen"]) for item in ranking),
        all(is_int(item["revenueFen"]) for item in rfm),
        all(is_int(item["revenueFen"]) and is_int(item["revenuePerChargerFen"]) for item in monthly),
        is_int(price_compare["cityAvgFenPerKwh"]),
        all(is_int(item["priceFenPerKwh"]) for item in price_compare["stations"]),
    ])
    check("金额字段一律整数分", money_ok)
    check("percentage 为 0–100 数值",
          all(0.0 <= item["utilizationRate"] <= 100.0 for item in stations)
          and 0.0 <= payloads["/api/overview/kpis"]["onlineRate"] <= 100.0)

    trend_dates = [point["date"] for point in trend["points"]]
    check("revenue-trend 按 date 升序", trend_dates == sorted(trend_dates))
    check("revenue-trend 的 orderCount / energyKwh 必填且为正",
          all(point["orderCount"] > 0 and point["energyKwh"] > 0 for point in trend["points"]))

    check("RFM 为 8 个分层且顺序固定",
          len(rfm) == 8 and rfm[0]["segment"] == "重要价值客户" and rfm[-1]["segment"] == "一般挽留客户")
    check("monthly 按月份升序且月份唯一",
          [item["month"] for item in monthly] == sorted({item["month"] for item in monthly}))

    distances = [item["distanceKm"] for item in price_distance]
    check("price-distance 按距离升序", distances == sorted(distances))
    check("distanceKm 保留 2 位小数",
          all(round(value, 2) == value for value in distances),
          f"样例 {distances[:3]}")
    check("idle-ranking 按 idleCount 降序",
          [item["idleCount"] for item in idle_ranking]
          == sorted((item["idleCount"] for item in idle_ranking), reverse=True))

    station_count = len(stations)
    check("peak-heatmap hours=24 / names=存活站点数",
          len(heatmap["hours"]) == 24 and len(heatmap["names"]) == station_count)
    check("peak-heatmap values 维度 = 24 × 存活站点数",
          len(heatmap["values"]) == 24 * station_count)
    check("peak-heatmap 索引在范围内",
          all(0 <= h <= 23 and 0 <= s < station_count for h, s, _v in heatmap["values"]))

    check("load-24h = 存活站点数 × 24 点", len(load24h["points"]) == station_count * 24,
          f"实际 {len(load24h['points'])}")
    check("load-24h observedAt 为 +08:00",
          all(ISO_RE.match(point["observedAt"]) for point in load24h["points"]))
    check("events 时间倒序且为 +08:00",
          all(ISO_RE.match(item["createdAt"]) for item in events)
          and [item["createdAt"] for item in events]
          == sorted((item["createdAt"] for item in events), reverse=True))

    forecast = payloads["/api/forecast/24h"]
    check("forecast.24h 点数 = 启用站数 × 24",
          len(forecast["points"]) % 24 == 0 and len(forecast["points"]) > 0,
          f"{len(forecast['points'])} 点 / runId={forecast['runId']}")
    check("forecast.points horizonH ∈ [1, 24]",
          all(1 <= point["horizonH"] <= 24 for point in forecast["points"]))
    forecast_station_ids = {point["stationId"] for point in forecast["points"]}
    enabled_station_ids = {
        item["stationId"] for item in stations if item["forecastEnabled"]
    }
    check("预测站点集合 = forecastEnabled 站点集合",
          forecast_station_ids == enabled_station_ids,
          f"预测={sorted(forecast_station_ids)} 启用={sorted(enabled_station_ids)}")
    check("每个启用站点恰有 horizon 1–24",
          all(
              {point["horizonH"] for point in forecast["points"]
               if point["stationId"] == station_id} == set(range(1, 25))
              for station_id in enabled_station_ids
          ))
    metrics = payloads["/api/forecast/metrics"]
    check("forecast.metrics 24 个 horizon 且 wape >= 0",
          len(metrics["horizons"]) == 24
          and all(item["wape"] >= 0 and item["mae"] >= 0 for item in metrics["horizons"]))


def main() -> int:
    with app.test_client() as client:
        station_ids = discover_station_ids(client)
        check("从 ADS 发现至少一个存活站点", bool(station_ids), str(station_ids))
        routes = contract_routes(station_ids)
        payloads = check_envelope(client, routes)
        check_errors(client, station_ids)
        if len(payloads) == len(routes):
            check_consistency(payloads, station_ids)
            check_formats(payloads)
        else:
            check("口径自洽检查", False, "信封检查未全部通过，跳过依赖数据的断言")

    failed = [name for name, ok, _ in _results if not ok]
    print("\n" + "=" * 72)
    print(f"合计 {len(_results)} 项，通过 {len(_results) - len(failed)}，失败 {len(failed)}")
    if failed:
        print("失败项：")
        for name in failed:
            print(f"  - {name}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
