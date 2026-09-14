"""数据质量对账端点（契约 §2 `/api/quality/summary`）。

数据源：`ads_quality_table` / `ads_quality_issue` / `ads_quality_meta`，
由 `server/tools/build_ads_db.py` 按《03-PRL》§3.2 规则对 ODS 独立复算后落库。

`injected` 来自生成器 `handoff/ods/injection_log.json`（Q1–Q10），
`detected` 是 ADS 侧实际命中数，`recall = detected / injected`。
两层是**独立**统计的，因此 recall < 1 是真实结论而非笔误 —— 生成器注入与
清洗规则不可能逐条对齐（例如 Q4 时间格式注入的行可能同时触发 R01 被更早命中）。
"""

from __future__ import annotations

from flask import Blueprint

from services import ads_reader as ads
from services.envelope import ok

bp = Blueprint("quality", __name__)


@bp.get("/quality/summary")
def summary():
    meta = ads.meta_map()
    tables = [
        {
            "name": row["name"],
            "rowsBefore": int(row["rows_before"]),
            "rowsAfter": int(row["rows_after"]),
        }
        for row in ads.query(
            "SELECT name, rows_before, rows_after FROM ads_quality_table ORDER BY name"
        )
    ]
    issues = [
        {
            "rule": row["rule"],
            "type": row["type"],
            "injected": int(row["injected"]),
            "detected": int(row["detected"]),
            "handled": int(row["handled"]),
            "recall": float(row["recall"]),
        }
        for row in ads.query(
            "SELECT rule, type, injected, detected, handled, recall "
            "FROM ads_quality_issue ORDER BY rule"
        )
    ]
    return ok({
        "runId": meta.get("runId", ""),
        "tables": tables,
        "issues": issues,
        "injectedTotal": sum(item["injected"] for item in issues),
        "detectedTotal": sum(item["detected"] for item in issues),
        "note": (
            "injected 取自生成器 injection_log（Q1–Q10），detected 为 ADS 侧独立复算结果；"
            "两者统计口径不完全等价，recall 仅作覆盖度参考。"
        ),
    })
