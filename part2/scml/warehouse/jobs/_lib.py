"""ADS/DWS 物化作业的共用工具：ODS 读取、清洗规则、表结构装载。

纯标准库，零第三方依赖 —— 这是刻意的：ADS 物化必须能在**没有 Hadoop/Spark 的机器**
上跑通（前端联调、答辩查数、CI 自测都依赖这一点），所以清洗与聚合全部用 stdlib 实现。

### 清洗规则从哪来

规则逐条对齐 `docs/.../03-PRL-数据质量检测与清洗设计.md` §3.2，**不另立标准**：

| 规则 | 这里的实现 |
|---|---|
| R01 缺失值 | 关键字段（订单时间/金额/外键）缺失 -> 剔除 |
| R02 重复记录 | 按业务主键去重，保留业务时间最早一条 |
| R03 异常值 | 负电量 / 超单次上限 / 功率超额定 -> 剔除或计数 |
| R04 时间格式混杂 | ISO 8601 / `yyyy/MM/dd HH:mm:ss` / Unix 秒 统一解析，失败剔除 |
| R05 逻辑矛盾 | `ended_at < started_at` 剔除；`busy_count > pile_count` 裁剪回 pile_count |
| R06 金额口径错误 | 按「站点单价 × 电量」重算；差 100 倍（元混入）-> 修正；其余剔除 |
| R07 孤儿引用 | 外键在维度表中不存在 -> 剔除 |
| R08 非法字段值 | 手机号正则不通过 / 状态不在白名单 -> 剔除 |
| R09 经纬度越界 | 越界坐标由维度表按所属行政区质心回填 |
| R10 文本脏数据 | trim + 全角转半角 |

`#3` 交付 `handoff/dwd` 之后，正式链路由 `dws_etl.sql` / `ads_etl.sql` 读 DWD；
本模块只作为「无 Spark 环境下的同构实现」保留，口径以 #3 的 `cleaning_report.json` 为准。
"""

from __future__ import annotations

import csv
import json
import re
import sqlite3
from datetime import datetime, timedelta, timezone
from pathlib import Path

# --------------------------------------------------------------------------- #
# 常量（与 dws_etl.sql / ads_etl.sql 中的字面量必须保持一致）
# --------------------------------------------------------------------------- #
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

# R09 回填用的行政区质心（WGS84 近似值，北京市行政区划中心点，用于坐标回填）。
DISTRICT_CENTROID = {
    "朝阳区": (39.9216, 116.4435),
    "海淀区": (39.9590, 116.2980),
    "丰台区": (39.8585, 116.2870),
    "通州区": (39.9026, 116.6584),
    "大兴区": (39.7280, 116.3410),
}
DEFAULT_CENTROID = (39.9087, 116.3975)  # 天安门（用户视角距离参考点）

# 行政区常住人口。北京市第七次全国人口普查常住人口口径，
# 属**外部参考数据**，不是生成器产出；答辩需注明来源（契约要求 gov/coverage.population）。
DISTRICT_POPULATION = {
    "朝阳区": 3450000,
    "海淀区": 3130000,
    "丰台区": 2010000,
    "通州区": 1840000,
    "大兴区": 1990000,
}

# 碳减排换算（契约要求 co2SavedTon = 电量/1000 × factorTonPerMwh，前端会自校验）
CARBON_FACTOR_TON_PER_MWH = 0.581
CARBON_FACTOR_NOTE = "按全国电网平均排放因子 0.581 tCO₂/MWh 折算（项目假设，答辩需注明来源）"
KG_CO2_PER_TREE_YEAR = 18.0

# RFM 八分层的固定顺序（前端折线按此顺序取点，缺档会断线）
RFM_SEGMENTS = [
    "重要价值客户",
    "重要保持客户",
    "重要发展客户",
    "重要挽留客户",
    "一般价值客户",
    "一般保持客户",
    "一般发展客户",
    "一般挽留客户",
]

# 生成器注入日志用 Q1..Q10，PRL 用 R01..R10，一一对应
INJECTION_TO_RULE = {f"Q{i}": f"R{i:02d}" for i in range(1, 11)}

# 规则 ID -> 中文类型（对齐 03-PRL §2.3 与前端 quality_summary 契约）
RULE_TYPES = [
    ("R01", "缺失值"),
    ("R02", "重复记录"),
    ("R03", "异常值"),
    ("R04", "时间格式混杂"),
    ("R05", "逻辑矛盾"),
    ("R06", "金额口径错误"),
    ("R07", "孤儿引用"),
    ("R08", "非法字段值"),
    ("R09", "经纬度越界"),
    ("R10", "文本脏数据"),
]

SCHEMA_PATH = Path(__file__).resolve().parents[1] / "sql" / "ads_schema.sql"


# --------------------------------------------------------------------------- #
# ODS 读取
# --------------------------------------------------------------------------- #
def iter_rows(table_dir: Path):
    """读取一张 ODS 表：优先 Hive 风格 `dt=*` 分区目录，其次扁平 `part-*` 文件。

    ODS 保留脏数据（原样落地），所有类型转换都在调用方按规则做。

    **扁平目录里的同名 `.jsonl` / `.csv` 只取一份**：生成器对事件流会同时写出
    `part-00000.jsonl` 与 `part-00000.csv`（JSONL 是契约要求的正式格式，CSV 是便于
    人工查看的副本）。如果两个都读，每一行都会进两次，`ads_event` 的主键立刻撞车。
    正式交接包里事件流是 `dt=*/part-*.jsonl` 分区目录，走不到这条分支；
    `handoff/ods` 的小样例是扁平目录，正好会踩到。
    """
    if not table_dir.is_dir():
        return
    parts = sorted(p for p in table_dir.glob("dt=*/part-*") if p.is_file())
    if not parts:
        candidates = sorted(p for p in table_dir.glob("part-*") if p.is_file())
        # 同名的 .jsonl 优先，丢掉 .csv 副本
        jsonl_stems = {p.stem for p in candidates if p.suffix == ".jsonl"}
        parts = [
            p for p in candidates
            if not (p.suffix == ".csv" and p.stem in jsonl_stems)
        ]
    for part in parts:
        if part.suffix == ".jsonl":
            with part.open(encoding="utf-8") as handle:
                for line in handle:
                    line = line.strip()
                    if line:
                        yield json.loads(line)
        else:
            with part.open(encoding="utf-8", newline="") as handle:
                for row in csv.DictReader(handle):
                    yield row


def to_int(value, default: int = 0) -> int:
    try:
        return int(float(str(value).strip()))
    except (TypeError, ValueError):
        return default


def to_float(value, default: float = 0.0) -> float:
    try:
        return float(str(value).strip())
    except (TypeError, ValueError):
        return default


def needs_text_repair(raw: object, normalized: str) -> bool:
    """判断 R10 是否命中：原文与清洗后不一致，且不是空值。"""
    text = "" if raw is None else str(raw)
    return text != normalized and text != NULL_TEXT and normalized != ""


# --------------------------------------------------------------------------- #
# 清洗规则
# --------------------------------------------------------------------------- #
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
    """R09：越界坐标按所属行政区质心回填，同区内多站点做微小抖动避免完全重叠。"""
    base_lat, base_lng = DISTRICT_CENTROID.get(district_cn, DEFAULT_CENTROID)
    offset = (station_id % 5) * 0.004
    return round(base_lat + offset, 6), round(base_lng - offset, 6)


def station_display_name(raw_name: object, district_cn: str, station_id: int) -> str:
    """把生成器产出的「Chaoyang Station 1」渲染成大屏可读的中文名。

    这是应用层展示口径，不改动事实字段：ODS 的原始名保留在 `name_raw`。
    """
    cleaned = normalize_text(raw_name)
    match = re.match(
        r"^(chaoyang|haidian|fengtai|tongzhou|daxing)\s+station\s+(\d+)$", cleaned, re.I
    )
    if match:
        return f"{district_cn}{int(match.group(2))}号充电站"
    return cleaned or f"{district_cn}{station_id}号充电站"


def resolve_amount_fen(
    amount_fen: object, energy_kwh: float, price_fen_per_kwh: int
) -> tuple[int | None, bool]:
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


# --------------------------------------------------------------------------- #
# 表结构装载
# --------------------------------------------------------------------------- #
def load_schema(path: Path | None = None) -> str:
    """读取 ADS 的 SQLite DDL 真源（`warehouse/sql/ads_schema.sql`）。"""
    schema_path = path or SCHEMA_PATH
    return schema_path.read_text(encoding="utf-8")


def schema_table_names(schema_sql: str) -> list[str]:
    """从 DDL 里抽出表名（不含视图），顺序即出现顺序。"""
    names = []
    for match in re.finditer(r"CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?(\w+)", schema_sql, re.I):
        names.append(match.group(1))
    return names


def reset_sql(schema_sql: str) -> str:
    """幂等重建：不删文件、只 DROP 表与视图。

    目标目录可能受回收站/沙箱删除策略保护（Windows 下 `unlink` 会被拦），
    且幂等重建对「反复重跑物化」更友好：文件 inode 不变，已开的只读连接不会失效。
    """
    statements = [
        f"DROP VIEW IF EXISTS {name};"
        for name in re.findall(r"CREATE\s+VIEW\s+(\w+)", schema_sql, re.I)
    ]
    statements += [
        f"DROP TABLE IF EXISTS {name};" for name in schema_table_names(schema_sql)
    ]
    return "\n".join(statements)


def write_ads_db(db_path: Path, tables: dict[str, list[dict]], schema_sql: str | None = None) -> None:
    """按 `ads_schema.sql` 建库并写入各表。

    **插入列顺序直接取自 `PRAGMA table_info`**，不在 Python 里再抄一遍列名：
    这样 `ads_schema.sql` 是唯一的列顺序真源，改 DDL 不用同时改作业代码；
    而且一旦某行字典缺列会立刻 `KeyError`，等于自带结构漂移检测。
    """
    schema_sql = schema_sql if schema_sql is not None else load_schema()
    connection = sqlite3.connect(db_path)
    try:
        connection.executescript(reset_sql(schema_sql))
        connection.executescript(schema_sql)
        for name, rows in tables.items():
            if not rows:
                continue
            columns = [row[1] for row in connection.execute(f"PRAGMA table_info({name})")]
            if not columns:
                raise KeyError(f"ads_schema.sql 里没有表 {name}，但作业要写它")
            missing = set(columns) - set(rows[0].keys())
            if missing:
                raise KeyError(f"{name} 的行缺少列 {sorted(missing)}（结构漂移）")
            placeholders = ",".join("?" * len(columns))
            connection.executemany(
                f"INSERT INTO {name} ({','.join(columns)}) VALUES ({placeholders})",
                [[row[column] for column in columns] for row in rows],
            )
        connection.commit()
    finally:
        connection.close()


def split_sql_statements(sql: str) -> list[str]:
    """把一份 `.sql` 文件拆成单条语句，供 `spark.sql()` 逐条执行。

    Spark 的 `spark.sql()` 一次只吃一条语句，而 Hive 系又不支持 `-f` 之外的多语句
    执行方式，所以在 Python 侧拆。拆分前的预处理：

    * 丢掉整行注释（`-- ...`）；
    * 截掉行尾的行内注释（` -- ...`）；
    * 再按 `;` 切，丢掉空片段。

    前提是**语句里没有以「空格 + `--`」开头的字符串字面量**。本项目的
    `dws_schema.sql` / `dws_etl.sql` / `ads_etl.sql` 都是纯 SQL，没有这种字面量；
    `ads_schema.sql` 有行内注释但只给 SQLite 用（走 `executescript`，不经过这里）。
    """
    lines = []
    for line in sql.splitlines():
        if line.strip().startswith("--"):
            continue
        index = line.find(" --")
        lines.append(line[:index] if index != -1 else line)
    return [statement.strip() for statement in "\n".join(lines).split(";") if statement.strip()]
