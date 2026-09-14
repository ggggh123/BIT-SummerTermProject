"""ADS 中**不由 Spark 产出**的那 8 张表的计算逻辑。

为什么这 8 张不走 SparkSQL（理由要能答辩，写在 `warehouse/README.md` 里）：

| 表 | 为什么放 Python |
|---|---|
| `ads_meta` | 要拼 ODS manifest 的 runId/seed、注入总量，以及一串口径说明文本 —— 纯元数据装配 |
| `ads_quality_table` / `ads_quality_issue` / `ads_quality_meta` | R01–R10 的检出逻辑（金额单位识别、坐标范围、手机号正则、重复行判定）与 #3 的 DWD 清洗**共用一套规则**；写进 SQL 就是第二份实现，必然漂移，反而失去「独立复算」的对账意义 |
| `ads_forecast_batch` / `ads_forecast_24h` / `ads_forecast_metric` | seasonal-naive 降级基线 + 回测指标（《05-PE》§8）；#5 交付真实 MLlib 批次后整表替换 |
| `ads_event` | 事件流文案要 JOIN 站点名与订单金额，属展示层组装 |

分工不改变验收点：**业务指标表（7 张）全部走 SparkSQL**，本文件只补旁路产出。
"""

from __future__ import annotations

import math
from collections import Counter, defaultdict
from datetime import datetime, timedelta

from _lib import (
    CARBON_FACTOR_NOTE,
    CARBON_FACTOR_TON_PER_MWH,
    INJECTION_TO_RULE,
    KG_CO2_PER_TREE_YEAR,
    NULL_TEXT,
    RFM_SEGMENTS,
    RULE_TYPES,
    iter_rows,
    normalize_text,
    parse_timestamp,
    to_int,
)


# --------------------------------------------------------------------------- #
# ads_meta：口径说明的唯一出处
# --------------------------------------------------------------------------- #
def build_meta(
    *,
    run_id: str,
    generated_at: datetime,
    ods_manifest: dict,
    window_start: str,
    window_end: str,
    window_days: int,
    station_count: int,
    charger_count: int,
    user_count: int,
    order_count: int,
    total_revenue_fen: int,
    total_energy_kwh: float,
    batch: dict | None,
    producer: str,
    first_order_user_count: int,
) -> dict:
    """组装 `ads_meta`。本地作业与 Spark 导出端共用这一份，避免两套口径说明。

    这里写的每一条都是**如实标注**：哪些是推算的、哪些是外部参考数据、哪些是
    上游缺失时的降级，全部落库可 SQL 查证，答辩时不用靠记忆解释。
    """
    return {
        "adsContractVersion": "ads-flask-v1",
        "runId": run_id,
        "generatedAt": generated_at.isoformat(timespec="seconds"),
        "sourceKind": "ods-handoff",
        "sourceRunId": str(ods_manifest.get("runId", "")),
        "sourceSeed": str(ods_manifest.get("seed", "")),
        "producer": producer,
        "dataWindowStart": f"{window_start}T00:00:00+08:00",
        "dataWindowEnd": f"{window_end}T23:59:59+08:00",
        "windowDays": window_days,
        "stationCount": station_count,
        "chargerCount": charger_count,
        "userCount": user_count,
        "orderCount": order_count,
        "totalRevenueFen": total_revenue_fen,
        "totalEnergyKwh": total_energy_kwh,
        "forecastSource": batch["source"] if batch else "none",
        "forecastIsBaseline": "1" if batch else "0",
        "carbonFactorTonPerMwh": CARBON_FACTOR_TON_PER_MWH,
        "carbonFactorNote": CARBON_FACTOR_NOTE,
        "equivalentTrees": int(
            round(
                total_energy_kwh / 1000.0 * CARBON_FACTOR_TON_PER_MWH * 1000 / KG_CO2_PER_TREE_YEAR
            )
        ),
        "cleaningSource": "PRL §3.2（ODS -> ADS 过渡口径，DWD 交付后由 SparkSQL 链路替换）",
        "populationSource": "北京市第七次全国人口普查常住人口（外部参考数据，非生成器产出）",
        "serviceRadiusNote": "服务半径为运营规划参数，按站点订单需求规模折算，非实测",
        "avgWaitNote": (
            "平均等待 = started_at − reserved_at；当前 ODS 未建模预约到开工的排队时长，故为 0"
        ),
        "faultNote": (
            "fault_cnt 为「该日发生故障上报的去重桩数」，取自事件流 charger_fault；"
            "ads_station.fault_cnt 是「当期处于 fault 的桩数」快照口径，两者不同"
        ),
        "newUserNote": (
            "new_user_cnt 为「窗口内首次完成订单的用户数」（首单新客口径）。"
            f"生成器 {user_count} 个用户的注册时间全部早于业务窗口，"
            "窗口期内没有注册事件，若按 registered_at 聚合会得到零线；"
            "如需改回注册口径，须让生成器在窗口期内注入注册时间并重新交接 ODS。"
        ),
        "registeredUserCount": user_count,
        "firstOrderUserCount": first_order_user_count,
    }


# --------------------------------------------------------------------------- #
# RFM 八分层
# --------------------------------------------------------------------------- #
def _ntile_buckets(count: int, groups: int = 5) -> list[int]:
    """复刻 Spark `NTILE(groups)` 的分桶：前 `count % groups` 桶各多一行。

    为什么不直接用「排名 × groups / count」的整除：那个公式只在 count 能被 groups
    整除时与 `NTILE` 等价。要让 Spark 侧与本地侧**逐行一致**（`reconcile.py` 才有
    意义），必须严格复刻 `NTILE` 的分桶边界。
    """
    if count <= 0:
        return []
    size, extra = divmod(count, groups)
    buckets: list[int] = []
    for bucket in range(1, groups + 1):
        take = size + (1 if bucket <= extra else 0)
        buckets.extend([bucket] * take)
    return buckets


def _scores(items: list[tuple[int, float]], higher_is_better: bool) -> dict[int, int]:
    """按 `(取值, user_id)` 排序后分 5 档打分，返回 user_id -> 1..5。

    **必须带 `user_id` 做次级排序**：recency 是整数天，同一天最后一次消费的用户会
    大量并列；若不指定并列时的顺序，Spark 与本地两侧会把并列用户分到不同桶，
    分层人数与均值都会对不上。`ads_etl.sql` 里的 `NTILE(... ORDER BY ..., user_id)`
    与此一一对应。
    """
    if not items:
        return {}
    ordered = sorted(items, key=lambda kv: (kv[1], kv[0]), reverse=higher_is_better)
    buckets = _ntile_buckets(len(ordered))
    # 桶 1 是最优 -> 5 分
    return {
        user_id: 6 - buckets[rank]
        for rank, (user_id, _value) in enumerate(ordered)
    }


def build_rfm(orders: list[dict], window_end_date: str) -> list[dict]:
    """RFM 八分层。返回固定 8 行（空分层补 0），顺序与前端折线一致。

    `recency_days` 为**整数天** = (窗口末日 + 1) − 用户最后一次完成订单的日期，
    与 `ads_etl.sql` 的 `DATEDIFF(DATE_ADD(MAX(dt), 1), MAX(dt))` 同口径。
    """
    per_user: dict[int, dict] = defaultdict(lambda: {"orders": 0, "amount": 0, "last": None})
    for order in orders:
        rec = per_user[order["user_id"]]
        rec["orders"] += 1
        rec["amount"] += order["amount_fen"]
        day = order["started_at"].date()
        if rec["last"] is None or day > rec["last"]:
            rec["last"] = day

    if not per_user:
        return [
            {
                "segment": segment,
                "user_cnt": 0,
                "avg_recency_days": 0.0,
                "avg_frequency": 0.0,
                "avg_monetary_fen": 0.0,
                "revenue_fen": 0,
            }
            for segment in RFM_SEGMENTS
        ]

    anchor = datetime.fromisoformat(f"{window_end_date}T00:00:00+08:00").date() + timedelta(days=1)
    users = sorted(per_user.items())
    recency = [(uid, float((anchor - rec["last"]).days)) for uid, rec in users]
    frequency = [(uid, float(rec["orders"])) for uid, rec in users]
    monetary = [(uid, float(rec["amount"])) for uid, rec in users]

    r_score = _scores(recency, higher_is_better=False)
    f_score = _scores(frequency, higher_is_better=True)
    m_score = _scores(monetary, higher_is_better=True)

    buckets: dict[str, dict] = defaultdict(
        lambda: {"user_cnt": 0, "recency": 0.0, "frequency": 0.0, "monetary": 0.0, "revenue": 0}
    )
    for user_id, rec in users:
        high_r, high_f, high_m = r_score[user_id] >= 4, f_score[user_id] >= 4, m_score[user_id] >= 4
        if high_r and high_f and high_m:
            segment = "重要价值客户"
        elif (not high_r) and high_f and high_m:
            segment = "重要保持客户"
        elif high_r and (not high_f) and high_m:
            segment = "重要发展客户"
        elif (not high_r) and (not high_f) and high_m:
            segment = "重要挽留客户"
        elif high_r and high_f and (not high_m):
            segment = "一般价值客户"
        elif (not high_r) and high_f and (not high_m):
            segment = "一般保持客户"
        elif high_r and (not high_f) and (not high_m):
            segment = "一般发展客户"
        else:
            segment = "一般挽留客户"
        bucket = buckets[segment]
        bucket["user_cnt"] += 1
        bucket["recency"] += float((anchor - rec["last"]).days)
        bucket["frequency"] += float(rec["orders"])
        bucket["monetary"] += float(rec["amount"])
        bucket["revenue"] += rec["amount"]

    rows = []
    for segment in RFM_SEGMENTS:
        bucket = buckets.get(segment)
        count = bucket["user_cnt"] if bucket else 0
        rows.append(
            {
                "segment": segment,
                "user_cnt": count,
                "avg_recency_days": round(bucket["recency"] / count, 1) if count else 0.0,
                "avg_frequency": round(bucket["frequency"] / count, 1) if count else 0.0,
                "avg_monetary_fen": round(bucket["monetary"] / count, 1) if count else 0.0,
                "revenue_fen": int(bucket["revenue"]) if bucket else 0,
            }
        )
    return rows


# --------------------------------------------------------------------------- #
# 预测：seasonal-naive 降级基线
# --------------------------------------------------------------------------- #
def build_baseline_forecast(
    hourly: list[dict], stations: dict[int, dict], generated_at: datetime
) -> tuple[dict, list[dict]]:
    """按《05-PE》§8 降级预案：seasonal-naive（昨日同一时刻值）顶替，并如实标注。

    《05-PE》明确「若 D3 仍不达标，按降级预案以 seasonal-naive 基线顶上并如实标注，
    或大屏显示『暂无预测』，不伪造结果」。这里选前者，并在批次与 meta 里标注基线
    来源；#5 的真实 MLlib 批次落地后直接替换 `ads_forecast_24h` 即可。
    """
    last_dt = max((row["dt"] for row in hourly), default=None)
    if last_dt is None:
        return {}, []
    by_station_last_day: dict[int, dict[int, dict]] = defaultdict(dict)
    for row in hourly:
        if row["dt"] == last_dt:
            by_station_last_day[row["station_id"]][row["hour"]] = row

    anchor = datetime.fromisoformat(f"{last_dt}T00:00:00+08:00")
    forecast_day = anchor + timedelta(days=1)
    run_id = f"baseline-{forecast_day.date().isoformat()}"
    points: list[dict] = []
    for station_id, station in sorted(stations.items()):
        if not station["forecast_enabled"]:
            continue
        hours = by_station_last_day.get(station_id)
        if not hours or len(hours) < 24:
            continue
        rows = []
        for hour in range(24):
            source = hours.get(hour)
            if source is None:
                continue
            pile = max(1, int(source["pile_count"]))
            busy = min(pile, int(source["busy_count"]))
            occupancy = busy / pile
            level = "high" if occupancy >= 0.8 else "medium" if occupancy >= 0.55 else "low"
            rows.append(
                {
                    "station_id": station_id,
                    "forecast_at": (forecast_day + timedelta(hours=hour)).isoformat(timespec="seconds"),
                    "horizon_h": hour + 1,
                    "predicted_load_kw": round(float(source["load_kw"]), 1),
                    "predicted_busy_count": busy,
                    "predicted_idle_count": max(0, pile - busy),
                    "congestion_level": level,
                    "is_peak": 0,
                }
            )
        if len(rows) < 24:
            continue
        # 高峰 = 预测负荷最大的连续 2 小时（对齐《05》§2.3）；并列取更早的小时
        peak_index = max(range(len(rows)), key=lambda i: rows[i]["predicted_load_kw"])
        rows[peak_index]["is_peak"] = 1
        if peak_index + 1 < len(rows):
            rows[peak_index + 1]["is_peak"] = 1
        points.extend(rows)

    batch = {
        "run_id": run_id,
        "model_version": "seasonal-naive-baseline",
        "activated_at": generated_at.isoformat(timespec="seconds"),
        "source": "ads_station_hourly(seasonal-naive: 昨日同时刻值)",
        "horizon_h_max": 24,
        "is_baseline": 1,
        "note": (
            "Spark MLlib 批次尚未交接（#5），按《05-PE》§8 降级预案以 seasonal-naive "
            "基线顶替并如实标注；#5 交付 handoff/forecast 后替换 ads_forecast_24h 表即可，"
            "前端无需改动。"
        ),
    }
    return batch, points


def build_baseline_metrics(hourly: list[dict], stations: dict[int, dict]) -> list[dict]:
    """seasonal-naive 基线的回测指标：用「前一日同时刻值」预测「当日同时刻值」。

    口径（对齐《05-PE》§4 评估表）：
      MAE / RMSE 单位 kW；WAPE = Σ|误差| / Σ|实际| × 100（百分比）。
      `baseline_wape` 是「前一日全天均值」这个常量预测的 WAPE，用来证明 seasonal-naive
      相对朴素常数基线确有增益；若更差则如实体现，不伪造。
    """
    days = sorted({row["dt"] for row in hourly})
    if len(days) < 2:
        return []
    actual_day, prev_day = days[-1], days[-2]
    enabled = {sid for sid, st in stations.items() if st["forecast_enabled"]}

    def index(day: str) -> dict[tuple[int, int], float]:
        return {
            (row["station_id"], row["hour"]): float(row["load_kw"])
            for row in hourly
            if row["dt"] == day and row["station_id"] in enabled
        }

    actual, previous = index(actual_day), index(prev_day)
    per_station_bucket: dict[int, list[float]] = defaultdict(list)
    for (sid, _hour), value in previous.items():
        per_station_bucket[sid].append(value)
    per_station_mean = {sid: sum(v) / len(v) for sid, v in per_station_bucket.items()}

    metrics: list[dict] = []
    for hour in range(24):
        pairs = [
            (previous[(sid, hour)], actual[(sid, hour)], per_station_mean.get(sid, 0.0))
            for sid in sorted(enabled)
            if (sid, hour) in previous and (sid, hour) in actual
        ]
        if not pairs:
            metrics.append(
                {"horizon_h": hour + 1, "mae": 0.0, "rmse": 0.0, "wape": 0.0, "baseline_wape": 0.0}
            )
            continue
        errors = [pred - real for pred, real, _ in pairs]
        total = sum(abs(real) for _, real, _ in pairs) or 1.0
        base_errors = [mean - real for _, real, mean in pairs]
        metrics.append(
            {
                "horizon_h": hour + 1,
                "mae": round(sum(abs(e) for e in errors) / len(errors), 2),
                "rmse": round(math.sqrt(sum(e * e for e in errors) / len(errors)), 2),
                "wape": round(sum(abs(e) for e in errors) / total * 100.0, 2),
                "baseline_wape": round(sum(abs(e) for e in base_errors) / total * 100.0, 2),
            }
        )
    return metrics


# --------------------------------------------------------------------------- #
# 事件流
# --------------------------------------------------------------------------- #
def build_events(
    ods_dir,
    orders_by_id: dict[int, dict],
    stations: dict[int, dict],
    chargers: dict[int, dict],
    detected: Counter,
) -> list[dict]:
    """把 ODS 事件流清洗并组装成中文可读文案。

    设计文档 §3.1 的 DWD 表清单里**没有事件表**，所以事件流在 ADS 侧直接从
    `ods_events` 取（属已知的层间越级，记录在 `warehouse/README.md` 的已知限制里）。
    """
    events: list[dict] = []
    for row in iter_rows(ods_dir / "ods_events"):
        created_at = parse_timestamp(row.get("created_at"))
        if created_at is None:
            detected["R04"] += 1
            continue
        raw_type = normalize_text(row.get("event_type")) or "unknown"
        message_raw = normalize_text(row.get("message"))
        entity_id = to_int(row.get("entity_id"))
        if raw_type == "charger_fault":
            event_type = "fault"
            charger = chargers.get(entity_id)
            station = stations.get(charger["station_id"]) if charger else None
            if charger and station:
                message = f"设备告警：{station['name']} {charger['code']} 号桩上报故障"
            else:
                message = f"设备告警：充电桩 #{entity_id} 上报故障"
        elif raw_type == "order_completed":
            event_type = "order_completed"
            order = orders_by_id.get(entity_id)
            if order:
                station = stations.get(order["station_id"])
                message = (
                    f"订单完成：{station['name'] if station else '未知站点'}结算 "
                    f"{order['amount_fen'] / 100:.2f} 元"
                )
            else:
                message = f"订单完成：# {entity_id}"
        else:
            event_type = raw_type
            message = message_raw
        events.append(
            {
                "event_id": to_int(row.get("id")),
                "event_type": event_type,
                "message": message,
                "message_raw": message_raw,
                "created_at": created_at.isoformat(timespec="seconds"),
            }
        )
    events.sort(key=lambda item: (item["created_at"], item["event_id"]))
    return events


# --------------------------------------------------------------------------- #
# 质量对账
# --------------------------------------------------------------------------- #
def quality_rows(
    injection: dict,
    detected: Counter,
    rows_before: dict[str, int],
    rows_after: dict[str, int],
    telemetry_total: int,
    telemetry_removed: int,
    generated_at: datetime,
) -> tuple[list[tuple], list[tuple], list[tuple]]:
    """返回 (ads_quality_table 行, ads_quality_issue 行, ads_quality_meta 行)。

    「注入量」取自生成器的 `injection_log.json`（Q1..Q10 → R01..R10 映射）；
    「检出量」是 ADS 侧按 PRL §3.2 独立复算的结果。两者是**独立来源**，
    所以 recall 才是有效指标，而不是自己跟自己对。
    """
    detected_by_rule = {
        "R01": detected["R01"],
        "R02": detected["R02"],
        "R03": detected["R03"],
        "R04": detected["R04"],
        "R05": detected["R05"],
        "R06": detected["R06"] + detected["R06_bad"],
        "R07": detected["R07"],
        "R08": detected["R08_users"] + detected["R08_chargers"],
        "R09": detected["R09"],
        "R10": detected["R10"] + detected["R10_users"],
    }
    injected_by_rule: Counter = Counter()
    for rule, summary in (injection.get("summary") or {}).items():
        target = INJECTION_TO_RULE.get(rule)
        if target:
            injected_by_rule[target] += int(summary.get("total", 0))
    if not injected_by_rule:
        # 向后兼容：早期交接包（如 `kind = prl-test-fixture` 的小样例）没有 `summary`
        # 汇总块，只有逐条的 `issues`。这时按 rule 现数一遍，避免注入量整列显示 0。
        for issue in injection.get("issues") or []:
            target = INJECTION_TO_RULE.get(str(issue.get("rule", "")))
            if target:
                injected_by_rule[target] += 1

    issue_rows = []
    for rule, label in RULE_TYPES:
        injected = injected_by_rule.get(rule, 0)
        found = detected_by_rule.get(rule, 0)
        issue_rows.append(
            {
                "rule": rule,
                "type": label,
                "injected": injected,
                "detected": found,
                "handled": found,
                "recall": round(min(1.0, found / injected), 4) if injected else 0.0,
            }
        )

    table_rows = [
        {"name": "ods_orders", "rows_before": rows_before.get("ods_orders", 0),
         "rows_after": rows_after["ods_orders"]},
        {"name": "ods_telemetry", "rows_before": telemetry_total,
         "rows_after": telemetry_total - telemetry_removed},
        {"name": "ods_station_hourly", "rows_before": rows_before.get("ods_station_hourly", 0),
         "rows_after": rows_after["ods_station_hourly"]},
        {"name": "ods_users", "rows_before": rows_before.get("ods_users", 0),
         "rows_after": rows_after["ods_users"]},
        {"name": "ods_chargers", "rows_before": rows_before.get("ods_chargers", 0),
         "rows_after": rows_after["ods_chargers"]},
        {"name": "ods_stations", "rows_before": rows_before.get("ods_stations", 0),
         "rows_after": rows_after["ods_stations"]},
        {"name": "ods_events", "rows_before": rows_before.get("ods_events", 0),
         "rows_after": rows_after["ods_events"]},
    ]
    meta_rows = [
        {"key": "run_id", "value": f"ads-quality-{generated_at.strftime('%Y%m%d%H%M')}"},
        {"key": "source", "value": "ADS 侧按 PRL §3.2 规则对 ODS 独立复算"},
        {"key": "injection_run_id", "value": str(injection.get("runId", ""))},
        {"key": "note",
         "value": "#3 的 DWD 清洗报告（handoff/dwd/cleaning_report.json）交付后以其为准"},
    ]
    return table_rows, issue_rows, meta_rows
