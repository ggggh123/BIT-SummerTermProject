"""导出独立批次的 DWD 和报告；兼容旧六表与 SCML 七表，不覆盖正式 DWD。"""
import argparse
import json
import subprocess
from datetime import datetime
from pathlib import Path
from part2.clean.normalization import SHANGHAI
from part2.common.handoff import sha256_file


def hdfs(*args):
    result = subprocess.run(["ev-part2", "hdfs", "dfs", *args], check=True, text=True, stdout=subprocess.PIPE)
    return result.stdout


def verify_export(directory):
    root = Path(directory).resolve()
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("package_version") != "prl-dwd-package-0.1-draft" or manifest.get("kind") not in {"prl-dwd-fixture", "dwd-handoff-draft"}:
        raise ValueError("不是支持的 DWD 草案包")
    seen = set()
    for item in manifest["files"]:
        rel = item["path"]
        path = (root / rel).resolve()
        if Path(rel).is_absolute() or not path.is_relative_to(root) or rel in seen:
            raise ValueError("路径越界或重复")
        seen.add(rel)
        if not path.is_file() or path.stat().st_size != item["bytes"] or sha256_file(path) != item["sha256"]:
            raise ValueError(f"DWD 包文件校验失败：{rel}")
    actual = {str(path.relative_to(root)) for path in root.rglob("*") if path.is_file() and path != root / "manifest.json"}
    if actual != seen:
        raise ValueError("DWD 包有未列入清单或缺失的文件")
    result = json.loads((root / "reports/run_result.json").read_text(encoding="utf-8"))
    success = json.loads((root / "RUN_SUCCEEDED.json").read_text(encoding="utf-8"))
    expected_tables = {"dim_users", "dim_stations", "dim_chargers", "dwd_order_detail", "dwd_telemetry_detail", "dwd_station_hourly"}
    if result.get("dwd_profile") == "scml-dwd-v0.1":
        expected_tables.add("dwd_event")
    counts = {item["table"]: item["rows"] for item in result["assertions"]}
    if success != result or not result["ok"] or not all(item["ok"] for item in result["assertions"]) or set(counts) != expected_tables or counts != manifest["table_rows"] or result["run_id"] != manifest["run_id"]:
        raise ValueError("成功标志、表行数与源作业断言不一致")
    if any(not list((root / "dwd" / table).rglob("*.parquet")) for table in expected_tables):
        raise ValueError("缺少 DWD Parquet 数据文件")
    return manifest


def export_batch(batch, directory):
    if not batch.startswith("hdfs://"):
        raise ValueError("仅导出明确的 HDFS URI")
    batch = batch.rstrip("/")
    result = json.loads(hdfs("-cat", batch + "/RUN_SUCCEEDED.json"))
    if not result.get("ok") or not all(item["ok"] for item in result["assertions"]):
        raise ValueError("批次没有通过完整校验，拒绝导出")
    root = Path(directory).resolve()
    if root.exists():
        raise ValueError(f"导出目录已存在，拒绝覆盖：{root}")
    root.mkdir(parents=True)
    for name in ("dwd", "reports", "RUN_SUCCEEDED.json"):
        hdfs("-get", batch + "/" + name, str(root))
    scml = result.get("dwd_profile") == "scml-dwd-v0.1"
    if scml:
        (root / "audit").mkdir()
        hdfs("-get", batch + "/audit/dwd_lineage", str(root / "audit"))
    note = "# PRL DWD 试交接包\n\n此包基于尚待团队确认的契约／清洗策略。\n\n- 6 张表位于 dwd/，金额为整数分，电量为 Decimal，时间为 Parquet Timestamp（展示时区 +08:00）。\n- reports/ 含真实质量、清洗、断言报告，以及本批次的字段契约和策略快照。\n- _row_id 用于原始行追踪，_run_id 用于批次追踪；它们是新增审计列。\n- manifest 中表级行数来自 Spark 写入后读回；SHA-256 是导出后逐文件计算。\n- 校验和通过只说明交接文件未变；接收方导入后仍应重跑 Spark 约束和行数断言。\n- source_kind=prl-test-fixture 时不得当作正式全量数据或真实运营成果。\n- 未包含 dim_date；events 不是当前约定的 DWD 表。\n- 此命令没有发布到正式 /ev-charging/dwd，也没有推送 Git 或发送给队友。\n"
    if scml:
        note = "# PRL → SCML 七表试交接包\n\n- dwd/ 包含 #4 schema 的七张表，四张事实表按 dt 分区；金额 BIGINT 分，电量 DOUBLE，时间 ISO8601 +08:00 字符串。\n- dwd_event 为故障事件统计来源；未开工订单的 dt/hour 保留 NULL，不能冒充已开工。\n- audit/dwd_lineage 保存业务表与原始行、批次的关联；原始 ODS 未改写。\n- reports/dwd-contract.json 记录上游提交、精确 schema 和待确认策略；reports/ 有实际质量／清洗／读回断言。\n- 坐标及异常小时仍按 PRL 保守隔离处理，不保证与 #4 本地替代清洗结果一致。小时缺口不能直接用于按行偏移的 ML 特征。\n- 注入日志存在语义差异，TP/FP/FN 如实报告；文件校验通过不等于质量规则全部确认。\n- 未含 dim_date；它由 #4 生成。接收方导入后须刷新分区并执行下游 SQL 对账。\n- ready_for_team_delivery=false；这是待协商的兼容试交接包，不是全量正式验收。\n- 未发布到正式 /ev-charging/dwd，未推送 Git 或自动发送给队友。\n"
    (root / "交接说明.md").write_text(note, encoding="utf-8")
    files = [{"path": str(path.relative_to(root)), "bytes": path.stat().st_size, "sha256": sha256_file(path)} for path in sorted(root.rglob("*")) if path.is_file()]
    manifest = {"package_version": "prl-dwd-package-0.1-draft", "kind": "prl-dwd-fixture" if result["source_kind"] == "prl-test-fixture" else "dwd-handoff-draft", "contract_version": result["contract_version"], "policy_version": result["policy_version"], "run_id": result["run_id"], "source_run_id": result["source_run_id"], "source_kind": result["source_kind"], "source_hdfs": batch, "generated_at": datetime.now(SHANGHAI).isoformat(), "table_rows": {item["table"]: item["rows"] for item in result["assertions"]}, "files": files}
    manifest.update(dwd_profile=result.get("dwd_profile", "draft"), source_contract_version=result.get("source_contract_version", result["contract_version"]), ready_for_team_delivery=False)
    (root / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    verify_export(root)
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--batch", help="HDFS 独立批次 URI；指定时导出")
    parser.add_argument("--directory", required=True, type=Path, help="导出目录或待校验目录")
    args = parser.parse_args()
    result = export_batch(args.batch, args.directory) if args.batch else verify_export(args.directory)
    print(json.dumps({"ok": True, "kind": result["kind"], "run_id": result["run_id"], "table_rows": result["table_rows"], "directory": str(args.directory)}, ensure_ascii=False))
