"""只读质量摘要与明细：兼容 SCML 独立复算、PRL 逐行精确对账两种来源。"""
from __future__ import annotations

import json
import math

from flask import Blueprint
from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("quality", __name__)


def _json(meta, key, expected, default):
    try:
        value = json.loads(meta.get(key, "null"))
    except (ValueError, TypeError):
        return default
    return value if isinstance(value, expected) else default


def _number(value, integer=False):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return None
    if integer and (value < 0 or int(value) != value):
        return None
    return int(value) if integer else value


@bp.get("/quality/summary")
def summary():
    meta = ads.meta_map()
    quality_meta = {r["key"]: r["value"] for r in ads.query("SELECT key, value FROM ads_quality_meta")}
    exact = _json(quality_meta, "exactMetrics", dict, {})
    details = _json(quality_meta, "tableDetails", dict, {})
    tables = []
    for row in ads.query("SELECT name, rows_before, rows_after FROM ads_quality_table ORDER BY name"):
        detail = details.get(row["name"])
        detail = detail if isinstance(detail, dict) else {}
        tables.append({
            "name": row["name"], "rowsBefore": int(row["rows_before"]),
            "rowsAfter": int(row["rows_after"]),
            "cascadeAffectedRows": _number(detail.get("cascadeAffectedRows"), integer=True),
        })
    issues = []
    for row in ads.query(
        "SELECT rule, type, injected, detected, handled, recall FROM ads_quality_issue ORDER BY rule"
    ):
        metric = exact.get(row["rule"])
        metric = metric if isinstance(metric, dict) else {}
        issues.append({
            "rule": row["rule"], "type": row["type"],
            "injected": int(row["injected"]), "detected": int(row["detected"]), "handled": int(row["handled"]),
            "recall": _number(metric["recall"]) if "recall" in metric else _number(row["recall"]),
            "truePositive": _number(metric.get("truePositive"), integer=True),
            "falsePositive": _number(metric.get("falsePositive"), integer=True),
            "falseNegative": _number(metric.get("falseNegative"), integer=True),
            "precision": _number(metric.get("precision")),
        })
    source = quality_meta.get("source", "")
    exact_source = source == "prl-quality-report"
    return ok({
        "runId": meta.get("runId", ""),
        "source": source,
        "qualityRunId": quality_meta.get("qualityRunId"),
        "sourceRunId": quality_meta.get("sourceRunId"),
        "policyVersion": quality_meta.get("policyVersion"),
        "readyForTeamDelivery": _json(quality_meta, "readyForTeamDelivery", bool, None),
        "pendingPolicyNotes": _json(quality_meta, "pendingPolicyNotes", list, []),
        "metricSemantics": quality_meta.get("metricSemantics", ""),
        "tables": tables, "issues": issues,
        "injectedTotal": sum(i["injected"] for i in issues),
        "detectedTotal": sum(i["detected"] for i in issues),
        "note": (
            "来自 PRL 逐行对账；injected=TP+FN，detected=handled=TP+FP；同一记录可命中多条规则。"
            if exact_source else
            "来自 ADS 独立复算；注入与检出统计口径不完全等价，原 recall 仅作覆盖度参考；缺少精确对账项显示未发布。"
        ),
    })
