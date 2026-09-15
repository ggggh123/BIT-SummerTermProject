"""可单测的时间／金额基础原语；十类 Spark 编排见 quality/rules.py。"""
from datetime import datetime, timezone, timedelta
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import re
import unicodedata

SHANGHAI = timezone(timedelta(hours=8))
ORDER_STATES = {"reserved", "charging", "completed", "cancelled"}


def decimal_number(value):
    if value is None or not str(value).strip():
        raise ValueError("数值缺失")
    try:
        number = Decimal(str(value).strip())
    except InvalidOperation as exc:
        raise ValueError("数值格式非法") from exc
    if not number.is_finite():
        raise ValueError("数值必须有限")
    return number


def money_to_fen(value, unit="fen"):
    number = decimal_number(value)
    if unit == "yuan":
        number *= 100
    elif unit != "fen":
        raise ValueError("金额单位只能是 fen 或 yuan；禁止隐式猜测")
    if number < 0 or number != number.to_integral_value():
        raise ValueError("换算后必须是非负整数分")
    return int(number)


def expected_amount_fen(price_fen_per_kwh, energy_kwh):
    price, energy = decimal_number(price_fen_per_kwh), decimal_number(energy_kwh)
    if price < 0 or energy < 0:
        raise ValueError("单价和电量必须非负")
    return int((price * energy).quantize(Decimal("1"), rounding=ROUND_HALF_UP))


def normalize_timestamp(value):
    if value is None or not str(value).strip():
        return None
    text = str(value).strip()
    try:
        if re.fullmatch(r"\d{10}|\d{13}", text):
            stamp = Decimal(text) / (1000 if len(text) == 13 else 1)
            parsed = datetime.fromtimestamp(float(stamp), tz=timezone.utc)
        elif "/" in text:
            parsed = datetime.strptime(text, "%Y/%m/%d %H:%M:%S").replace(tzinfo=SHANGHAI)
        else:
            parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
            if parsed.tzinfo is None:
                parsed = parsed.replace(tzinfo=SHANGHAI)
        return parsed.astimezone(SHANGHAI).isoformat()
    except (ValueError, OverflowError, OSError) as exc:
        raise ValueError(f"时间无法解析：{text}") from exc


def validate_order_times(record):
    status = record["status"]
    if status not in ORDER_STATES:
        raise ValueError("订单状态非法")
    parsed = {field: normalize_timestamp(record.get(field)) for field in ("reserved_at", "started_at", "ended_at")}
    if not parsed["reserved_at"]:
        raise ValueError("预约时间缺失")
    if status in {"charging", "completed"} and not parsed["started_at"]:
        raise ValueError("已开始订单缺少开始时间")
    if status == "completed" and not parsed["ended_at"]:
        raise ValueError("已完成订单缺少结束时间")
    dates = {key: datetime.fromisoformat(value) if value else None for key, value in parsed.items()}
    if dates["started_at"] and dates["started_at"] < dates["reserved_at"]:
        raise ValueError("开始时间早于预约")
    if dates["ended_at"] and dates["ended_at"] < (dates["started_at"] or dates["reserved_at"]):
        raise ValueError("结束时间倒置")
    return parsed


def normalize_text(value):
    return None if value is None else unicodedata.normalize("NFKC", value).strip()
