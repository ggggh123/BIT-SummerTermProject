"""ODS -> 干净口径的清洗工具（纯标准库，可被 ADS 物化作业与自测脚本共用）。

为什么这里有一份清洗逻辑：`docs/design/part2-api-contract.md` 要求 Flask 从
`handoff/ads/ads.db` 取数，而 ADS 的上游是 DWD（#3 产出）。DWD 尚未交付时，
ADS 物化作业需要一份**与 #3 的 PRL 规则口径一致**的过渡清洗，规则逐条对齐
`03-PRL-数据质量检测与清洗设计.md` §3.2，不另立标准：

| 规则 | 本项目实现 |
|---|---|
| R01 缺失值 | 关键字段（订单时间/金额/外键）缺失 -> 剔除 |
| R02 重复记录 | 按业务主键去重，保留业务时间最早一条 |
| R03 异常值 | 负电量 / 超单次上限 -> 剔除 |
| R04 时间格式混杂 | ISO 8601 / `yyyy/MM/dd HH:mm:ss` / Unix 秒 统一解析，失败剔除 |
| R05 逻辑矛盾 | `ended_at < started_at` 剔除；`busy_count > pile_count` 裁剪回 pile_count |
| R06 金额口径错误 | 按「站点单价 × 电量」重算；差 100 倍（元混入）-> 修正；其余剔除 |
| R07 孤儿引用 | 外键在维度表中不存在 -> 剔除 |
| R08 非法字段值 | 手机号正则不通过 / 状态不在白名单 -> 剔除 |
| R09 经纬度越界 | 越界坐标由维度表按所属行政区质心回填（PRL 明确「由 #4 维度表修正」） |
| R10 文本脏数据 | trim + 全角转半角 |

`#3` 交付 `handoff/dwd` 之后，把 `build_ads_db.py` 的读取源从 ODS 切到 DWD，
本模块即可退役；口径以 #3 的 `cleaning_report.json` 为准。
"""

from __future__ import annotations

import re
from datetime import datetime, timedelta, timezone

CN_TZ = timezone(timedelta(hours=8))
NULL_TEXT = r"\N"

# 单次充电电量上限（kWh）：额定 120kW 桩按最长 180 分钟满功率上浮 20% 留裕度。
MAX_SESSION_KWH = 120 * 3 * 1.2

# 金额重算容差（PRL §3.2：偏差 ≤1% 视为正常）
AMOUNT_TOLERANCE = 0.01

# 北京市经纬度边界（PRL R09）
LAT_RANGE = (39.4, 41.1)
LNG_RANGE = (115.4, 117.5)

MOBILE_RE = re.compile(r"^1\d{10}$")

CHARGER_STATES = ("idle", "reserved", "charging", "fault", "restarting")

# ODS 的 district 取值来自生成器（英文拼音），展示层与政府视角统一为中文行政区名。
DISTRICT_CN = {
    "chaoyang": "朝阳区",
    "haidian": "海淀区",
    "fengtai": "丰台区",
    "tongzhou": "通州区",
    "daxing": "大兴区",
}

# R09 回填用的行政区质心（WGS84 近似值，来源：北京市行政区划中心点，用于坐标回填）。
DISTRICT_CENTROID = {
    "朝阳区": (39.9216, 116.4435),
    "海淀区": (39.9590, 116.2980),
    "丰台区": (39.8585, 116.2870),
    "通州区": (39.9026, 116.6584),
    "大兴区": (39.7280, 116.3410),
}
DEFAULT_CENTROID = (39.9087, 116.3975)  # 天安门（用户视角距离参考点）

# 行政区常住人口（万人 -> 人）。北京市第七次全国人口普查常住人口口径，
# 属**外部参考数据**，不是生成器产出；答辩需注明来源（合同要求 gov/coverage.population）。
DISTRICT_POPULATION = {
    "朝阳区": 3450000,
    "海淀区": 3130000,
    "丰台区": 2010000,
    "通州区": 1840000,
    "大兴区": 1990000,
}

# 碳减排换算（合同要求 co2SavedTon = 电量/1000 × factorTonPerMwh，前端会自校验）
CARBON_FACTOR_TON_PER_MWH = 0.581
CARBON_FACTOR_NOTE = "按全国电网平均排放因子 0.581 tCO₂/MWh 折算（项目假设，答辩需注明来源）"
KG_CO2_PER_TREE_YEAR = 18.0


def parse_timestamp(value: object) -> datetime | None:
    """解析 ODS 里三种并存的时间写法，返回 +08:00 时间；不可解析返回 None（R04）。"""
    if value is None:
        return None
    text = str(value).strip()
    if not text or text == NULL_TEXT:
        return None
    if text.isdigit() and len(text) >= 9:  # Unix 秒
        try:
            return datetime.fromtimestamp(int(text), tz=CN_TZ)
        except (OverflowError, OSError, ValueError):
            return None
    for pattern in ("%Y/%m/%d %H:%M:%S", "%Y-%m-%d %H:%M:%S"):
        try:
            return datetime.strptime(text, pattern).replace(tzinfo=CN_TZ)
        except ValueError:
            pass
    try:
        parsed = datetime.fromisoformat(text)
    except ValueError:
        return None
    return parsed.replace(tzinfo=CN_TZ) if parsed.tzinfo is None else parsed.astimezone(CN_TZ)


def normalize_text(value: object) -> str:
    """R10：去首尾空白 + 全角转半角。"""
    if value is None:
        return ""
    text = str(value)
    if text == NULL_TEXT:
        return ""
    out = []
    for char in text:
        code = ord(char)
        if code == 0x3000:
            out.append(" ")
        elif 0xFF01 <= code <= 0xFF5E:
            out.append(chr(code - 0xFEE0))
        else:
            out.append(char)
    return "".join(out).strip()


def to_district_cn(value: object) -> str:
    raw = normalize_text(value)
    if not raw:
        return "未知行政区"
    return DISTRICT_CN.get(raw.lower(), raw)


def is_valid_mobile(value: object) -> bool:
    return bool(MOBILE_RE.match(normalize_text(value)))


def is_valid_coordinate(latitude: float, longitude: float) -> bool:
    return LAT_RANGE[0] <= latitude <= LAT_RANGE[1] and LNG_RANGE[0] <= longitude <= LNG_RANGE[1]


def impute_coordinate(district_cn: str, station_id: int) -> tuple[float, float]:
    """R09：越界坐标按所属行政区质心回填，同一行政区内多站点做微小抖动避免完全重叠。"""
    base_lat, base_lng = DISTRICT_CENTROID.get(district_cn, DEFAULT_CENTROID)
    offset = (station_id % 5) * 0.004
    return round(base_lat + offset, 6), round(base_lng - offset, 6)


def station_display_name(raw_name: object, district_cn: str, station_id: int) -> str:
    """把生成器产出的「Chaoyang Station 1」这类站名渲染成大屏可读的中文名。

    这是应用层展示口径，不改动事实字段：ODS 的原始名保留在 `name_raw`。
    """
    cleaned = normalize_text(raw_name)
    match = re.match(r"^(chaoyang|haidian|fengtai|tongzhou|daxing)\s+station\s+(\d+)$", cleaned, re.I)
    if match:
        return f"{district_cn}{int(match.group(2))}号充电站"
    return cleaned or f"{district_cn}{station_id}号充电站"


def resolve_amount_fen(amount_fen: object, energy_kwh: float, price_fen_per_kwh: int) -> tuple[int | None, bool]:
    """R06：按「站点单价 × 电量」重算金额。

    返回 `(amount_fen, repaired)`；无法判断为可修正时返回 `(None, False)` 表示应剔除。
    """
    raw = str(amount_fen).strip()
    if not raw or raw == NULL_TEXT:
        return None, False
    try:
        amount = int(float(raw))
    except ValueError:
        return None, False
    expected = energy_kwh * price_fen_per_kwh
    if expected <= 0:
        return (amount, False) if amount >= 0 else (None, False)
    if abs(amount - expected) <= expected * AMOUNT_TOLERANCE:
        return amount, False
    if abs(amount * 100 - expected) <= expected * AMOUNT_TOLERANCE:  # 元被当成「分」写入
        return int(round(amount * 100)), True
    return None, False


def is_outlier_energy(value: object) -> bool:
    """R03：负电量或超单次上限。"""
    try:
        energy = float(str(value).strip())
    except (TypeError, ValueError):
        return True
    return energy < 0 or energy > MAX_SESSION_KWH


def is_outlier_power(value: object, rated_power_kw: int) -> bool:
    """R03：功率为空或超过额定功率（含遥测被放大 10 倍的注入）。"""
    if value is None or str(value).strip() in ("", NULL_TEXT):
        return True
    try:
        power = float(str(value).strip())
    except ValueError:
        return True
    return power < 0 or power > rated_power_kw * 1.05


def seconds_to_minutes(seconds: float) -> float:
    return round(seconds / 60.0, 1)
