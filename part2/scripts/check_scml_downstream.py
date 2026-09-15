"""隔离验证 #4 SQL 对 PRL 七表的真实消费；不写正式数仓、不修改队友源码。"""
import argparse
import importlib.util
import json
import re
import sys
import uuid
from datetime import date, datetime
from pathlib import Path
from pyspark.sql import SparkSession, functions as F
from part2.common.handoff import sha256_file
from part2.clean.scml_dwd import load_scml_contract, read_scml, assert_scml
from part2.scripts.spark_quality_clean import require_new_hdfs


def import_file(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def rewrite_sql(sql, database, dwd_root, output, adapt_iso=False):
    """只重定向库/目录；时间修复必须由参数显式开启并在报告标明。"""
    sql = sql.replace("/ev-charging/dwd/dim_date", "__PRL_DIM_DATE__")
    sql = sql.replace("/ev-charging/dwd/", "__PRL_DWD__/")
    sql = sql.replace("/ev-charging", output).replace("__PRL_DWD__", dwd_root)
    sql = sql.replace("__PRL_DIM_DATE__", output + "/dwd_aux/dim_date")
    sql = re.sub(r"\bev_charging\b", database, sql)
    if adapt_iso:
        sql = re.sub(r"UNIX_TIMESTAMP\((ended_at|started_at)\)", r"UNIX_TIMESTAMP(CAST(\1 AS TIMESTAMP))", sql, flags=re.IGNORECASE)
    return sql


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scml-root", type=Path, required=True, help="#4 的 part2/scml 目录或只读提交快照")
    parser.add_argument("--dwd-root", required=True, help="已通过 PRL 读回校验的七表 HDFS 根目录")
    parser.add_argument("--output", required=True, help="不存在的独立 HDFS 验证目录")
    parser.add_argument("--run-result", type=Path, required=True)
    parser.add_argument("--input-spec", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--iso-timestamp-adapter", action="store_true", help="显式修复上游 UNIX_TIMESTAMP 对 ISO 字符串的解析；不改写上游 SQL 文件")
    args = parser.parse_args()
    if args.report.exists() or not args.dwd_root.startswith("hdfs://") or not args.output.startswith("hdfs://"):
        raise ValueError("报告不得覆盖，输入输出必须是明确的 HDFS URI")
    source_result = json.loads(args.run_result.read_text(encoding="utf-8"))
    manifest = json.loads(args.input_spec.read_text(encoding="utf-8"))
    if not source_result.get("ok") or source_result.get("dwd_profile") != "scml-dwd-v0.1" or source_result["source_run_id"] != manifest["run_id"] or args.dwd_root != source_result["output"] + "/dwd":
        raise ValueError("必须使用同一成功批次的 DWD、输入描述与运行结果")
    jobs = args.scml_root / "warehouse/jobs"
    helper = import_file("_prl_probe_scml_lib", jobs / "_lib.py")
    calendar = import_file("_prl_probe_scml_calendar", jobs / "build_dim_date.py")
    sql_dir = args.scml_root / "warehouse/sql"
    database = "prl_scml_probe_" + uuid.uuid4().hex[:12]
    report = {"ok": False, "source_run_id": manifest["run_id"], "prl_run_id": source_result["run_id"], "database": database, "output": args.output, "upstream_commit": load_scml_contract()["upstream_commit"], "iso_timestamp_adapter": args.iso_timestamp_adapter, "source_sql_files_unmodified": True, "executed_sql_mode": "iso-timestamp-adapter" if args.iso_timestamp_adapter else "original", "sql_sources": {}, "steps": [], "checks": [], "scope": "isolated-scml-sql-consumer-check-not-full-project", "ubuntu22_verified": False}
    spark = SparkSession.builder.appName("PRL-SCML-Downstream-Consumer-Check").config("spark.sql.warehouse.dir", args.output + "/catalog").getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")
    spark.conf.set("spark.sql.ansi.enabled", "false")  # 与上游未显式开启 ANSI 的入口一致。
    report.update(master=spark.sparkContext.master, application_id=spark.sparkContext.applicationId)
    try:
        if spark.sparkContext.master != "yarn":
            raise ValueError("本入口要求 Spark on YARN，不能冒充集成验证")
        require_new_hdfs(spark, args.output)
        dwd = read_scml(spark, args.dwd_root)
        counts = {item["table"]: item["rows"] for item in source_result["assertions"]}
        report["dwd_assertions"] = assert_scml(dwd, counts)
        window = manifest["dataWindow"]
        rows = calendar.build_rows(date.fromisoformat(window["start"][:10]), int(window["days"]))
        dates = spark.createDataFrame(rows, "dt string, year int, month int, day_of_week int, is_weekend int")
        dates.write.mode("errorifexists").parquet(args.output + "/dwd_aux/dim_date")
        report["dim_date_rows"] = len(rows)
        report["calendar_source_sha256"] = sha256_file(jobs / "build_dim_date.py")
        for filename in ("dwd_contract.sql", "dws_schema.sql", "dws_etl.sql", "ads_etl.sql"):
            path = sql_dir / filename
            report["sql_sources"][filename] = sha256_file(path)
            statements = helper.split_sql_statements(rewrite_sql(path.read_text(encoding="utf-8"), database, args.dwd_root, args.output, args.iso_timestamp_adapter))
            for index, statement in enumerate(statements, 1):
                report["current_step"] = {"file": filename, "statement": index}
                print(f"SCML_SQL_PROGRESS={filename}:{index}/{len(statements)}", flush=True)
                spark.sql(statement)
            if filename == "dwd_contract.sql":
                for table, spec in load_scml_contract()["tables"].items():
                    if spec["partition"]:
                        spark.sql(f"MSCK REPAIR TABLE {database}.{table}")
                    actual = spark.table(database + "." + table).count()
                    if actual != counts[table]:
                        raise ValueError(f"下游 DDL／分区登记行数不一致：{table}: {actual} != {counts[table]}")
            report["steps"].append({"file": filename, "statements": len(statements), "ok": True})
        completed = dwd["dwd_order_detail"].where(F.col("status") == "completed")
        expected_orders = completed.count()
        expected_fen = completed.agg(F.sum("amount_fen")).first()[0] or 0
        expected_seconds = completed.agg(F.sum(F.to_timestamp("ended_at").cast("long") - F.to_timestamp("started_at").cast("long"))).first()[0] or 0
        if expected_orders == 0 or expected_seconds == 0:
            raise ValueError("样本没有有效完成订单／正充电时长，不能作为消费链路验收")
        totals = [("dws_station_day", "revenue_fen", expected_fen), ("dws_user_day", "amount_fen", expected_fen), ("dws_region_day", "revenue_fen", expected_fen), ("ads_daily", "revenue_fen", expected_fen), ("ads_station_day", "revenue_fen", expected_fen), ("ads_user_rfm", "revenue_fen", expected_fen), ("ads_district", "revenue_fen", expected_fen), ("dws_station_day", "order_cnt", expected_orders), ("ads_daily", "order_cnt", expected_orders), ("dws_charger_day", "charge_duration_sec", expected_seconds)]
        for table, column, expected in totals:
            actual = spark.table(database + "." + table).agg(F.sum(column)).first()[0] or 0
            report["checks"].append({"table": table, "column": column, "expected": expected, "actual": actual, "ok": expected == actual})
        names = ["dws_station_day", "dws_charger_day", "dws_user_day", "dws_region_day", "ads_station", "ads_charger", "ads_daily", "ads_station_day", "ads_station_hourly", "ads_user_rfm", "ads_district"]
        report["table_rows"] = {table: spark.table(database + "." + table).count() for table in names}
        if not all(item["ok"] for item in report["checks"]):
            raise ValueError("下游金额／订单数／时长对账不一致，详情见 checks")
        report["ok"] = True
    except Exception as exc:
        report["error"] = str(exc)[:6000]
    finally:
        spark.stop()
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with args.report.open("x", encoding="utf-8") as target:
            json.dump(report, target, ensure_ascii=False, indent=2)
        print("SCML_DOWNSTREAM_RESULT=" + json.dumps(report, ensure_ascii=False), flush=True)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
