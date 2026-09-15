"""真实 YARN/HDFS 冒烟：读原始字段、执行 Python worker、写画像并读回。

此结果是运行环境证据，不是十类问题检测报告，也不是清洗后的 DWD。
"""
import argparse
import json
import os
import sys
from pathlib import Path
from pyspark.sql import SparkSession, functions as F, types as T


def worker_version(_):
    import sys
    return sys.version.split()[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()
    if not args.input.startswith("hdfs://") or not args.output.startswith("hdfs://"):
        raise ValueError("本测试必须明确使用 HDFS URI")
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest["kind"] != "prl-test-fixture":
        raise ValueError("此冒烟测试只使用明确标注的 PRL 测试夹具")
    spark = SparkSession.builder.appName("EV-Part2-PRL-HDFS-Smoke").getOrCreate()
    try:
        if spark.sparkContext.master != "yarn":
            raise ValueError("验收证据要求 --master yarn，不能用 local 冒充")
        spark.sparkContext.setLogLevel("WARN")
        worker_versions = spark.sparkContext.parallelize([1, 2], 2).map(worker_version).collect()
        # 不锁定具体小版本：验收要求是「driver 与各 worker 使用同一解释器」，
        # 而不是某个固定补丁号（22.04 用系统 3.10.x，25.04 可能用自建 3.10/3.11）。
        driver_python = sys.version.split()[0]
        if len(set(worker_versions)) != 1 or driver_python != worker_versions[0]:
            raise ValueError(
                f"driver 与 worker 必须使用同一 Python 解释器：driver={driver_python}, worker={sorted(set(worker_versions))}"
            )
        # 可选强校验：需要固定版本时设 EV_PART2_EXPECT_PYTHON=<版本>
        expected_python = os.environ.get("EV_PART2_EXPECT_PYTHON")
        if expected_python and driver_python != expected_python:
            raise ValueError(f"期望 Python {expected_python}，实际 {driver_python}")
        profiles = []
        for item in manifest["files"]:
            if "table" not in item:
                continue
            table = item["table"]
            columns = contract["metadata_columns"] + list(contract["tables"][table]["columns"])
            schema = T.StructType([T.StructField(name, T.StringType(), True) for name in columns])
            source = f"{args.input.rstrip('/')}/{item['path']}"
            reader = spark.read.schema(schema).option("mode", "FAILFAST")
            if item["format"] == "csv":
                frame = reader.option("header", True).option("enforceSchema", False).option("nullValue", "").option("emptyValue", "").option("escape", '"').csv(source)
                frame = frame.select(*[F.when(F.col(name) == contract["csv_null"], F.lit(None).cast("string")).otherwise(F.coalesce(F.col(name), F.lit(""))).alias(name) for name in columns])
            else:
                frame = reader.json(source)
            counts = frame.agg(F.count("*").alias("rows"), *[F.sum(F.when(F.col(name).isNull(), 1).otherwise(0)).alias(name) for name in columns]).first().asDict()
            if counts["rows"] != item["rows"]:
                raise ValueError(f"HDFS 读回行数不匹配：{table}")
            profiles.append((table, counts.pop("rows"), json.dumps(counts, ensure_ascii=False, sort_keys=True)))
        profile = spark.createDataFrame(profiles, "table string, rows long, null_counts_json string")
        profile.write.mode("errorifexists").parquet(args.output)
        if spark.read.parquet(args.output).count() != len(profiles):
            raise ValueError("HDFS Parquet 读回校验失败")
        print("EV_PART2_SMOKE_RESULT=" + json.dumps({"ok": True, "kind": "environment-smoke-not-dwd", "application_id": spark.sparkContext.applicationId, "master": spark.sparkContext.master, "spark": spark.version, "driver_python": sys.version.split()[0], "worker_python": worker_versions, "tables": len(profiles), "rows": sum(row[1] for row in profiles), "output": args.output}, ensure_ascii=False))
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
