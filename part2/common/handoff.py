"""ODS 小样本交接包校验；不以文件存在替代行数、字段和哈希核验。"""
import csv
import hashlib
import json
from pathlib import Path
from .contracts import load_contract, raw_columns, validate_record
from .scml_handoff import SCML_PROFILE, normalize_manifest, label_records


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def count_rows(path, fmt):
    with Path(path).open(encoding="utf-8", newline="") as source:
        if fmt == "csv":
            rows = csv.reader(source)
            next(rows)
            return sum(1 for _ in rows)
        if fmt == "jsonl":
            return sum(1 for line in source if line.strip())
        if fmt == "json":
            json.load(source)
            return 1
    raise ValueError(f"未知文件格式：{fmt}")


def verify_handoff(directory, *, require_full=False):
    root = Path(directory).resolve()
    source = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    contract = load_contract()
    manifest = normalize_manifest(source, contract)
    is_scml = manifest.get("input_profile") == SCML_PROFILE
    manifest["source_manifest_sha256"] = sha256_file(root / "manifest.json")
    if manifest.get("kind") not in {"prl-test-fixture", "ods-handoff"}:
        raise ValueError("不支持的交接包类型")
    if require_full and (
        manifest.get("kind") != "ods-handoff"
        or manifest.get("input_profile") != SCML_PROFILE
    ):
        raise ValueError("正式 PRL 运行要求 #4 原生 kind=ods-handoff 交接包")
    if not manifest.get("run_id") or "seed" not in manifest:
        raise ValueError("缺少 run_id 或 seed")
    seen, tables, row_ids = set(), set(), set()
    for item in manifest["files"]:
        rel = item["path"]
        path = (root / rel).resolve()
        if Path(rel).is_absolute() or not path.is_relative_to(root) or rel in seen:
            raise ValueError("文件路径越界或重复")
        seen.add(rel)
        if sha256_file(path) != item["sha256"]:
            raise ValueError(f"文件校验和不一致：{rel}")
        if item.get("count_mode") == "issues":
            actual_rows = len(json.loads(path.read_text(encoding="utf-8"))["issues"])
        else:
            actual_rows = count_rows(path, item["format"])
        if actual_rows != item["rows"]:
            raise ValueError(f"文件行数不一致：{rel}")
        table = item.get("table")
        if not table:
            continue
        if table not in contract["tables"] or item["format"] != contract["tables"][table]["format"]:
            raise ValueError("数据表或格式不匹配")
        tables.add(table)
        with path.open(encoding="utf-8", newline="") as source:
            if item["format"] == "csv":
                records = csv.DictReader(source)
                expected_columns = raw_columns(table)
                valid_header = records.fieldnames == expected_columns
                if is_scml:
                    valid_header = len(records.fieldnames) == len(expected_columns) and set(records.fieldnames) == set(expected_columns)
                if not valid_header:
                    raise ValueError(f"CSV 表头不匹配：{table}")
                item["columns"] = records.fieldnames
                def csv_records():
                    for row in records:
                        if None in row or any(value is None for value in row.values()):
                            raise ValueError(f"CSV 行列数不匹配：{table}；空值应显式写为 \\N")
                        yield {key: None if value == contract["csv_null"] else value for key, value in row.items()}
                iterator = csv_records()
            else:
                if is_scml:
                    # JSON 的数值按原词法转为字符串；不先经 float 再转回，避免精度损失。
                    iterator = ({key: None if value == contract["csv_null"] else value for key, value in json.loads(line, parse_int=str, parse_float=str).items()} for line in source if line.strip())
                else:
                    iterator = (json.loads(line) for line in source if line.strip())
            for record in iterator:
                validate_record(table, record)
                identity = (table, record["_row_id"])
                if identity in row_ids:
                    raise ValueError("原始行标识重复：无法区分重复的业务键记录")
                row_ids.add(identity)
    if tables != set(contract["tables"]):
        raise ValueError("缺少必要的 ODS 表")
    if "injection_log.json" not in seen:
        raise ValueError("缺少受校验保护的注入日志")
    # Spark 只能读受 manifest 哈希保护的数据文件。#4 生成器会额外写空的
    # _SUCCESS 标记，它们不含业务行；其他未列出文件一律拒绝。
    actual_files = {str(path.relative_to(root)) for path in root.rglob("*") if path.is_file()}
    allowed_unlisted = {name for name in actual_files if Path(name).name == "_SUCCESS"}
    unexpected = actual_files - seen - {"manifest.json"} - allowed_unlisted
    if unexpected:
        raise ValueError(f"交接包含未列入 manifest 的文件：{sorted(unexpected)}")
    log = json.loads((root / "injection_log.json").read_text(encoding="utf-8"))
    rules = {rule["id"] for rule in contract["rules"]}
    for finding in label_records(log, manifest):
        if finding["rule"] not in rules or (finding["table"], finding["row_id"]) not in row_ids:
            raise ValueError("注入日志引用未知规则或不存在的原始行")
    return manifest
