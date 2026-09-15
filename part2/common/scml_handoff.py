"""#4 ODS 交接适配：只规范描述信息，不改写原始 CSV/JSONL 或注入标签。"""
from copy import deepcopy
from datetime import datetime

SCML_VERSION = "ods-dwd-v0.1"
SCML_PROFILE = "scml-ods-v0.1"
PARTITIONED = {"orders", "telemetry", "station_hourly", "events"}


def normalize_manifest(source, contract):
    """返回内部读入描述；来源版本、文件字节哈希和原表名仍明确保留。"""
    manifest = deepcopy(source)
    if source.get("contractVersion") != SCML_VERSION:
        if source.get("contract_version") != contract["contract_version"]:
            raise ValueError("交接包契约版本不一致")
        if source.get("contractVersion") or source.get("input_profile") == SCML_PROFILE:
            raise ValueError("交接包来源版本不一致")
        return manifest
    if source.get("contract_version") not in (None, contract["contract_version"]):
        raise ValueError("交接包契约版本别名冲突")
    if source.get("input_profile") == SCML_PROFILE:
        # 本地接收校验已生成的描述；不再对表名去第二次前缀。
        return manifest
    if source.get("runId") != source.get("run_id") or not source.get("run_id"):
        raise ValueError("SCML 批次标识不一致")
    window = source.get("dataWindow", {})
    try:
        start, end = (datetime.fromisoformat(window[key]) for key in ("start", "end"))
        valid = start.utcoffset() == end.utcoffset() and start.utcoffset().total_seconds() == 28800
        valid = valid and start.hour == start.minute == start.second == end.hour == end.minute == end.second == 0
        valid = valid and (end - start).total_seconds() == int(window["days"]) * 86400 and end > start
    except (KeyError, TypeError, ValueError, AttributeError):
        valid = False
    if not valid:
        raise ValueError("SCML 数据窗口必须是 +08:00 的完整日区间")
    manifest.update(contract_version=contract["contract_version"], input_profile=SCML_PROFILE,
                    source_contract_version=SCML_VERSION, data_cutoff=window["end"])
    for item in manifest["files"]:
        source_table = item.get("table")
        item["source_table"] = source_table
        if source_table == "injection_log":
            if item["path"] != "injection_log.json" or item["format"] != "json":
                raise ValueError("SCML 注入日志描述不一致")
            item["table"] = None
            item["count_mode"] = "issues"
        elif source_table in {"ods_" + table for table in contract["tables"]}:
            table = source_table[4:]
            item["table"] = table
            parts = item["path"].split("/")
            if parts[0] != source_table:
                raise ValueError("SCML 数据表与文件目录不一致")
            if table in PARTITIONED:
                dt = item.get("dt", "")
                try:
                    partition_date = datetime.strptime(dt, "%Y-%m-%d").date()
                except (TypeError, ValueError):
                    partition_date = None
                if len(parts) != 3 or parts[1] != "dt=" + dt or partition_date is None or not start.date() <= partition_date < end.date():
                    raise ValueError("SCML 日期分区与清单／数据窗口不一致")
            elif len(parts) != 2 or item.get("dt") is not None:
                raise ValueError("SCML 快照维表不应按日分区")
        else:
            raise ValueError(f"SCML 未知数据表：{source_table}")
    for table in contract["tables"]:
        name = "ods_" + table
        entries = [item for item in manifest["files"] if item.get("source_table") == name]
        summary = source.get("tables", {}).get(name, {})
        if not entries or summary.get("rows") != sum(item["rows"] for item in entries) or summary.get("files") != [item["path"] for item in entries]:
            raise ValueError(f"SCML 表级行数／文件清单不一致：{name}")
    return manifest


def label_records(log, manifest):
    """仅供接收校验和检测结束后的对账使用，检测函数不得调用。"""
    if manifest.get("input_profile") != SCML_PROFILE:
        return log["expected_findings"]
    if log.get("contractVersion") != SCML_VERSION or log.get("run_id") != manifest["run_id"] or log.get("runId") != manifest["run_id"] or log.get("seed") != manifest["seed"]:
        raise ValueError("SCML 注入日志与数据批次不一致")
    if log.get("totalInjected") != len(log.get("issues", [])):
        raise ValueError("SCML 注入日志总数不一致")
    labels = []
    for item in log["issues"]:
        table = item["table"]
        if not table.startswith("ods_"):
            raise ValueError("SCML 注入日志表名不合法")
        labels.append({"rule": item["rule"], "table": table[4:], "row_id": item["row_id"]})
    return labels
