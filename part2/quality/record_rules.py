"""在 Spark Python worker 中执行的单行规则；不接收或读取注入标签。

关联、去重、金额参考核验在 rules.py 的 DataFrame 阶段执行。
"""
import json
import math
import re
import unicodedata
from datetime import datetime
from decimal import Decimal
from part2.clean.normalization import decimal_number, normalize_timestamp


def issue(rule, action, detail, origin="direct"):
    return {"rule": rule, "action": action, "detail": detail, "origin": origin}


def blank(value):
    return value is None or not str(value).strip()


def clean_text(value):
    if value is None:
        return None
    # 零宽字符移除；内部控制字符转空格；替换字符 U+FFFD 不可恢复。
    # 中文逗号、括号是正常中文排版，不应因为 NFKC 能转换就视为脏数据。
    # 只折叠全角字母／数字与全角空格；路径、中文标点保持原意。
    fullwidth = {code: chr(code - 0xFEE0) for code in range(0xFF01, 0xFF5F) if chr(code).isalnum()}
    fullwidth[0x3000] = " "
    text = value.translate(fullwidth).strip()
    return "".join(" " if unicodedata.category(char) == "Cc" else char
                   for char in text if unicodedata.category(char) != "Cf").strip()


def numeric_rule(field):
    if "fen" in field:
        return "Q6"
    if field in {"latitude", "longitude"}:
        return "Q9"
    if field in {"power_kw", "rated_power_kw", "load_kw", "energy_kwh", "energy_increment_kwh", "temperature_c"}:
        return "Q3"
    return "Q8"


def parse_number(value, dtype):
    number = decimal_number(value)
    if dtype in {"long", "integer"}:
        limit = 2**63 if dtype == "long" else 2**31
        if number != number.to_integral_value() or not -limit <= number < limit:
            raise ValueError("不是目标范围内的整数")
        return int(number)
    if dtype == "double":
        result = float(number)
        if not math.isfinite(result):
            raise ValueError("浮点值溢出")
        return result
    match = re.fullmatch(r"decimal\((\d+),(\d+)\)", dtype)
    if not match:
        raise ValueError(f"未知数值类型：{dtype}")
    precision, scale = map(int, match.groups())
    if abs(number) >= Decimal(10) ** (precision-scale) or number != number.quantize(Decimal(10) ** -scale):
        raise ValueError("Decimal 精度越界，不允许静默截断或舍入")
    return number


def inspect_record(table, raw, spec, policy):
    """返回明确类型的行及处置建议；缺失不自动转零，失败字段保留在原始 JSON。"""
    result = {"_row_id": raw["_row_id"], "_raw_json": json.dumps(raw, ensure_ascii=False, sort_keys=True)}
    problems = []
    optional = set(policy["optional"].get(table, []))
    defaults = policy["defaults"].get(table, {})
    for field, dtype in spec["columns"].items():
        value = raw.get(field)
        result[field] = None
        if blank(value):
            if field in defaults:
                result[field] = defaults[field]
                # 可选展示字段缺失是合法状态；应用默认值不冒充上游 Q1 脏数据。
                if field not in optional and value != defaults[field]:
                    problems.append(issue("Q1", "fill", f"{field}: 使用非关键字段默认值"))
            elif field not in optional:
                problems.append(issue("Q1", "reject", f"{field}: 必填值缺失"))
            continue
        if dtype == "string":
            normalized = clean_text(value)
            result[field] = normalized
            if "\ufffd" in normalized or any(unicodedata.category(char) == "Cs" for char in normalized):
                problems.append(issue("Q10", "reject", f"{field}: 不可恢复的替换字符或非法 Unicode"))
            elif normalized != value:
                problems.append(issue("Q10", "repair", f"{field}: 全半角、空白或不可见字符标准化"))
            if not normalized and field not in optional:
                problems.append(issue("Q1", "reject", f"{field}: 标准化后为空"))
        elif dtype == "timestamp":
            try:
                normalized = normalize_timestamp(value)
                parsed = datetime.fromisoformat(normalized)
                if not policy["timestamp_year_range"][0] <= parsed.year <= policy["timestamp_year_range"][1]:
                    raise ValueError("时间超出项目可接受年份，不进入历史日历重基逻辑")
                result[field] = parsed
                if normalized != value:
                    problems.append(issue("Q4", "repair", f"{field}: 非标准时间转为 +08:00"))
            except ValueError:
                problems.append(issue("Q4", "reject", f"{field}: 无法解析的时间"))
        else:
            try:
                # 订单金额暂存 Decimal：待关联单价后判断单位，不提前截断小数。
                if table == "orders" and field == "amount_fen":
                    amount = parse_number(value, "decimal(24,6)")
                    result["_amount_raw"] = amount
                    if amount >= 0 and amount == amount.to_integral_value():
                        result[field] = parse_number(value, "long")
                    if amount < 0:
                        problems.append(issue("Q6", "reject", "amount_fen: 负金额"))
                else:
                    result[field] = parse_number(value, dtype)
            except (ValueError, ArithmeticError):
                problems.append(issue(numeric_rule(field), "reject", f"{field}: 非法、非有限或精度越界数值"))

    if table == "orders":
        result.setdefault("_amount_raw", None)
        state = result["status"]
        for field, needed in (("started_at", state in {"charging", "completed"}), ("ended_at", state == "completed")):
            if needed and blank(raw.get(field)):
                problems.append(issue("Q1", "reject", f"{field}: {state} 状态要求此时间"))
        reserved, started, ended = (result[name] for name in ("reserved_at", "started_at", "ended_at"))
        if (started and reserved and started < reserved) or (ended and (started or reserved) and ended < (started or reserved)):
            problems.append(issue("Q5", "reject", "订单时间先后矛盾"))
        if (state == "reserved" and (started or ended)) or (state == "charging" and ended):
            problems.append(issue("Q5", "reject", "订单状态与已存在的开始／结束时间矛盾"))

    for field, allowed in policy["enums"].get(table, {}).items():
        if result[field] is not None and result[field] not in allowed:
            problems.append(issue("Q8", "reject", f"{field}: 枚举不在白名单"))
    for field in spec["columns"]:
        if (field == "id" or field.endswith("_id")) and result[field] is not None and result[field] <= 0:
            problems.append(issue("Q8", "reject", f"{field}: 标识必须是正整数"))
    if table == "users" and result["mobile"] is not None and not re.fullmatch(r"1[0-9]{10}", result["mobile"]):
        problems.append(issue("Q8", "reject", "mobile: 手机号须为 1 开头的 11 位数字"))
    for field, (lower, upper) in policy["ranges"].get(table, {}).items():
        value = result[field]
        if value is not None and not Decimal(str(lower)) <= Decimal(str(value)) <= Decimal(str(upper)):
            problems.append(issue(numeric_rule(field), "reject", f"{field}: 超出已配置范围 [{lower}, {upper}]"))
    if table == "stations":
        for field, (lower, upper) in policy["beijing_bbox"].items():
            if result[field] is not None and not lower <= result[field] <= upper:
                problems.append(issue("Q9", "reject", f"{field}: 超出项目北京包围框"))
    if table == "station_hourly":
        if result["busy_count"] is not None and result["pile_count"] is not None and result["busy_count"] > result["pile_count"]:
            problems.append(issue("Q5", "reject", "busy_count 大于 pile_count"))
        if result["load_kw"] is not None and result["rated_power_kw"] is not None and result["load_kw"] > result["rated_power_kw"]:
            problems.append(issue("Q3", "reject", "load_kw 超过站点额定功率"))
        observed = result["observed_at"]
        if observed and (observed.minute or observed.second or observed.microsecond):
            problems.append(issue("Q5", "reject", "observed_at 不是整点，禁止猜测应归属的小时"))
    result["_issues"] = problems
    return result
