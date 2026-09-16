"""ODS → 十类检测／隔离／标准化 → DWD → 读回断言 → JSON/Markdown 报告。"""
import argparse
import json
import os
import sys
import time
from datetime import datetime
from pathlib import Path, PurePosixPath
from pyspark.sql import SparkSession, functions as F, types as T
from part2.clean.assertions import assert_dwd
from part2.clean.scml_dwd import PROFILE, load_scml_contract, scml_frames, lineage_frame, write_scml, read_scml, assert_scml
from part2.clean.normalization import SHANGHAI
from part2.common.spark_io import read_csv_strings
from part2.common.scml_handoff import SCML_PROFILE
from part2.quality.rules import raw_schema, run_rules, finding_frame, dwd_frames, rejected, accepted
from part2.quality.reporting import build_reports, json_default, markdown_report


def write_new(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as target:
        target.write(content)


def require_new_hdfs(spark, uri):
    path = spark._jvm.org.apache.hadoop.fs.Path(uri)
    fs = path.getFileSystem(spark._jsc.hadoopConfiguration())
    if fs.exists(path):
        raise ValueError(f"输出已存在，拒绝覆盖（失败批次也不覆盖）：{uri}")
    fs.mkdirs(path)


def load_raw(spark, root, manifest, contract):
    frames = {}
    for table, spec in contract["tables"].items():
        items = [item for item in manifest["files"] if item.get("table") == table]
        if not items or any(item["format"] != spec["format"] for item in items):
            raise ValueError(f"缺少表或格式不一致：{table}")
        paths = []
        for item in items:
            relative = PurePosixPath(item["path"])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("manifest 路径越界")
            paths.append(root.rstrip("/") + "/" + item["path"])
        reader = spark.read.schema(raw_schema(spec)).option("mode", "FAILFAST")
        if spec["format"] == "csv":
            # 按各文件已核验的表头读入，再按列名归一；不能按位置套用本机旧草案。
            groups = {}
            for path, item in zip(paths, items):
                groups.setdefault(tuple(item.get("columns", raw_schema(spec).fieldNames())), []).append(path)
            frame = None
            for columns, group_paths in groups.items():
                schema = T.StructType([T.StructField(name, T.StringType(), True) for name in columns])
                group = read_csv_strings(spark, group_paths, schema, contract["csv_null"]).select(*raw_schema(spec).fieldNames())
                frame = group if frame is None else frame.unionByName(group)
        else:
            frame = reader.json(paths)
            if manifest.get("input_profile") == SCML_PROFILE:
                frame = frame.select(*[F.when(F.col(name) == contract["csv_null"], F.lit(None).cast("string")).otherwise(F.col(name)).alias(name) for name in frame.columns])
        frame = frame.persist()
        counts = frame.agg(F.count("*").alias("rows"), F.countDistinct("_row_id").alias("row_ids"), F.sum(F.when(F.col("_row_id").isNull() | (F.trim(F.col("_row_id")) == ""), 1).otherwise(0)).alias("invalid_row_ids")).first()
        if counts.rows != sum(item["rows"] for item in items) or counts.rows != counts.row_ids or counts.invalid_row_ids:
            raise ValueError(f"HDFS 行数／追踪标识校验失败：{table}")
        frames[table] = frame
    return frames


def load_expected(spark, root, manifest):
    """必须在检测完成后调用；SCML 标签只更换字段名／表名前缀。"""
    scml = manifest.get("input_profile") == SCML_PROFILE
    field = "issues" if scml else "expected_findings"
    fields = T.StructType([T.StructField(name, T.StringType()) for name in ("rule", "table", "row_id")])
    schema = T.StructType([T.StructField(field, T.ArrayType(fields))])
    expected = spark.read.schema(schema).option("multiLine", True).json(root.rstrip("/") + "/injection_log.json").select(F.explode(field).alias("item")).select("item.*")
    return expected.withColumn("table", F.regexp_replace("table", "^ods_", "")) if scml else expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="已校验上传的 ODS HDFS URI")
    parser.add_argument("--output", required=True, help="不存在的独立 HDFS 批次目录")
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--reports", required=True, type=Path)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--accept-draft", action="store_true", help="显式按未冻结草案试处理正式交接样本，不等于团队确认")
    parser.add_argument("--dwd-profile", choices=("auto", "draft", PROFILE), default="auto", help="auto 对 #4 原始交接包输出七张兼容表；旧夹具仍走原六表回归")
    args = parser.parse_args()
    if not all(uri.startswith("hdfs://") for uri in (args.input, args.output)):
        raise ValueError("交付作业必须明确使用 HDFS 输入输出")
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    policy = json.loads(args.policy.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    scml_output = args.dwd_profile == PROFILE or (args.dwd_profile == "auto" and manifest.get("input_profile") == SCML_PROFILE)
    if manifest["contract_version"] != contract["contract_version"] or manifest["kind"] not in {"prl-test-fixture", "ods-handoff"}:
        raise ValueError("输入契约／类型不匹配")
    if manifest["kind"] != "prl-test-fixture" and not args.accept_draft:
        raise ValueError("契约尚未冻结；正式样本试跑必须显式 --accept-draft")
    if args.reports.exists() and any(args.reports.iterdir()):
        raise ValueError("本地报告目录非空，拒绝覆盖")
    os.environ["TZ"] = "Asia/Shanghai"
    time.tzset()
    started = time.monotonic()
    spark = SparkSession.builder.appName("EV-Part2-PRL-Quality-Clean").getOrCreate()
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")
    spark.conf.set("spark.sql.ansi.enabled", "true")
    spark.sparkContext.setLogLevel("WARN")
    try:
        if spark.sparkContext.master != "yarn":
            raise ValueError("正式入口要求 --master yarn；本地测试不能冒充 HDFS/YARN 验证")
        workers = spark.sparkContext.parallelize([1, 2], 2).map(lambda _: sys.version.split()[0]).collect()
        if any(version != sys.version.split()[0] for version in workers):
            raise ValueError("Python driver/worker 版本不一致")
        require_new_hdfs(spark, args.output)
        spark.sparkContext.setCheckpointDir(args.output + "/_work/checkpoints")
        raw = load_raw(spark, args.input, manifest, contract)
        processed = run_rules(spark, raw, contract, policy)
        findings = finding_frame(processed).checkpoint(eager=True)
        findings.write.mode("errorifexists").parquet(args.output + "/audit/findings")
        quarantine = None
        for table, frame in processed.items():
            rows = frame.where(rejected()).select(F.lit(table).alias("table"), F.col("_row_id").alias("row_id"), F.col("_raw_json").alias("raw_json"), F.col("_issues").alias("issues"))
            quarantine = rows if quarantine is None else quarantine.unionByName(rows)
        quarantine.write.mode("errorifexists").parquet(args.output + "/audit/quarantine")
        accepted(processed["events"]).drop("_issues", "_raw_json").write.mode("errorifexists").parquet(args.output + "/audit/clean_events")
        internal = dwd_frames(processed, contract, args.run_id)
        dwd_mapping = None
        if scml_output:
            # 既有业务约束先独立验收；兼容投影另做写后读回断言，不能替代前者。
            internal_checks = assert_dwd(internal, contract, policy)
            expected_counts = {item["table"]: item["rows"] for item in internal_checks}
            expected_counts["dwd_event"] = accepted(processed["events"]).count()
            outputs = scml_frames(processed)
            write_scml(outputs, args.output + "/dwd")
            readback = read_scml(spark, args.output + "/dwd")
            checks = assert_scml(readback, expected_counts)
            lineage_frame(processed, args.run_id).write.mode("errorifexists").parquet(args.output + "/audit/dwd_lineage")
            dwd_mapping = {spec["source"]: name for name, spec in load_scml_contract()["tables"].items()}
        else:
            outputs = internal
            for table, frame in outputs.items():
                frame.write.mode("errorifexists").parquet(args.output + "/dwd/" + table)
            readback = {table: spark.read.parquet(args.output + "/dwd/" + table) for table in outputs}
            checks = assert_dwd(readback, contract, policy)
        print("PRL_PROGRESS=dwd_readback_passed", flush=True)
        # 至此检测和 DWD 已落盘；标签第一次进入作业，只服务指标对账。
        expected = load_expected(spark, args.input, manifest)
        metadata = {"contract_version": contract["contract_version"], "policy_version": policy["policy_version"], "contract_status": contract["status"], "run_id": args.run_id, "source_run_id": manifest["run_id"], "source_kind": manifest["kind"], "data_cutoff": manifest.get("data_cutoff"), "generated_at": datetime.now(SHANGHAI).isoformat(), "application_id": spark.sparkContext.applicationId, "master": spark.sparkContext.master, "spark": spark.version, "driver_python": sys.version.split()[0], "worker_python": workers, "money_reference": policy["money"], "output": args.output, "ubuntu22_verified": False}
        metadata.update(input_profile=manifest.get("input_profile", "prl-draft"), source_contract_version=manifest.get("source_contract_version", manifest["contract_version"]), source_manifest_sha256=manifest.get("source_manifest_sha256"), dwd_profile=PROFILE if scml_output else "draft", ready_for_team_delivery=False)
        if scml_output:
            # 质量策略已于 2026-09-16 冻结：契约改用 resolved_policy_notes 记录逐条结论，
            # 因此这里允许缺省（缺省 = 本批没有待确认策略），不再强制要求旧字段。
            metadata["pending_policy_notes"] = load_scml_contract().get("unresolved_policy_notes", [])
            if manifest.get("dataWindow"):
                expected_hours = readback["dim_stations"].count() * int(manifest["dataWindow"]["days"]) * 24
                actual_hours = next(item["rows"] for item in checks if item["table"] == "dwd_station_hourly")
                metadata["hourly_coverage"] = {"expected_station_hours": expected_hours, "retained_station_hours": actual_hours, "missing_station_hours": expected_hours - actual_hours, "row_offset_ml_safe": expected_hours == actual_hours, "note": "缺失小时不插值；#5 不可把第 h 行当作第 h 小时。"}
        quality, cleaning = build_reports(raw, processed, findings, contract, policy, metadata, expected, checks, dwd_mapping=dwd_mapping)
        for name, document in (("quality_report.json", quality), ("cleaning_report.json", cleaning), ("contract.json", contract), ("policy.json", policy)):
            write_new(args.reports / name, json.dumps(document, ensure_ascii=False, indent=2, default=json_default) + "\n")
        write_new(args.reports / "quality_report.md", markdown_report(quality, cleaning))
        if scml_output:
            write_new(args.reports / "dwd-contract.json", json.dumps(load_scml_contract(), ensure_ascii=False, indent=2) + "\n")
        if not scml_output and manifest["kind"] == "prl-test-fixture" and manifest.get("generator") == "part2.scripts.make_fixture":
            expected_counts = {"dim_users": 2, "dim_stations": 2, "dim_chargers": 3, "dwd_order_detail": 6, "dwd_telemetry_detail": 1, "dwd_station_hourly": 2}
            if {item["table"]: item["rows"] for item in checks} != expected_counts:
                raise ValueError("标准夹具 DWD 保留量不符")
            if any((item["truePositive"], item["falsePositive"], item["falseNegative"]) != (1, 0, 0) for item in quality["injection_comparison"].values()):
                raise ValueError("标准夹具十类问题未精确对账；请检查已保存的报告")
        result = {**metadata, "ok": True, "scope": "draft-quality-clean-fixture" if manifest["kind"] == "prl-test-fixture" else "draft-quality-clean-handoff", "elapsed_seconds": round(time.monotonic()-started, 2), "dwd_tables": len(checks), "dwd_rows": sum(item["rows"] for item in checks), "input_rows": sum(item["before"] for item in cleaning["tables"].values()), "quarantined_rows": sum(item["quarantined"] for item in cleaning["tables"].values()), "assertions": checks}
        write_new(args.reports / "run_result.json", json.dumps(result, ensure_ascii=False, indent=2) + "\n")
        # 报告尚在本地：这里只表示数据已校验；外层入口上传报告后才发布全批次标志。
        spark.createDataFrame([(json.dumps(result, ensure_ascii=False),)], "value string").coalesce(1).write.mode("errorifexists").text(args.output + "/DATA_VALIDATED")
        print("EV_PART2_PIPELINE_RESULT=" + json.dumps(result, ensure_ascii=False), flush=True)
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
