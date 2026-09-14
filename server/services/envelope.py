"""统一响应信封（契约 §1）。

    {"code": 0, "message": "ok", "data": ..., "generatedAt": "2026-09-14T10:05:00+08:00"}

`code != 0` 表示业务错误 —— 前端会显示错误并**保留上次成功数据**，
因此错误也必须走 HTTP 200 + 合法信封，而不是 4xx/5xx（否则 axios 先 reject，
前端拿不到 message）。
"""

from __future__ import annotations

from typing import Any

from flask import jsonify

from services.ads_reader import generated_at


def ok(data: Any, generated: str | None = None):
    return jsonify({
        "code": 0,
        "message": "ok",
        "data": data,
        "generatedAt": generated or generated_at(),
    })


def fail(code: int, message: str, generated: str | None = None):
    """错误信封（仅 response，HTTP 状态码由调用方决定，默认 200）。

    刻意走 HTTP 200 + 非 0 code：axios 对 4xx/5xx 会直接 reject，前端就拿不到
    `message` 去区分「接口挂了」还是「业务无数据」（契约 §1）。
    """
    return jsonify({
        "code": code,
        "message": message,
        "data": None,
        "generatedAt": generated or generated_at(),
    })
