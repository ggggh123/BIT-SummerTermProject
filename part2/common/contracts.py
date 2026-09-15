"""显式加载原始字段契约，不使用脏数据推断 schema。"""
import json
from pathlib import Path

CONTRACT_PATH = Path(__file__).resolve().parents[1] / "contracts/ods-dwd-v0.1.json"


def load_contract():
    return json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))


def raw_columns(table):
    spec = load_contract()
    return spec["metadata_columns"] + list(spec["tables"][table]["columns"])


def validate_record(table, record):
    expected = set(raw_columns(table))
    if set(record) != expected:
        raise ValueError(f"{table} 字段不一致：缺少 {sorted(expected-set(record))}；多出 {sorted(set(record)-expected)}")
    if not record["_row_id"]:
        raise ValueError("_row_id 必须存在，用于定位重复主键下的不同原始行")
    if any(value is not None and not isinstance(value, str) for value in record.values()):
        raise ValueError("ODS 业务值必须为字符串或 null，不能提前丢弃金额单位错误等信息")
