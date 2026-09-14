#!/usr/bin/env python3
"""把 HDFS 上的 DWS Parquet 导出成本地 CSV 交接包 `handoff/dws/`。

    spark-submit warehouse/jobs/export_dws_csv.py \\
        --warehouse-root /ev-charging \\
        --out handoff/dws

产出格式与 `build_local.py` 完全一致（每表一个 `part-00000.csv` + `_SUCCESS` +
`manifest.json` 含行数与 SHA-256），所以下游（#5 的预测输入、#2 的复核）
**不需要知道数据是 Spark 还是 Python 产的** —— 两条路的交接包可以互换。

复用 `build_local.write_dws`，不重写第二份写盘逻辑。
"""

from __future__ import annotations

import argparse
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _lib import CN_TZ  # noqa: E402
from build_local import DWS_TABLES, write_dws  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="DWS Parquet -> handoff/dws CSV 交接包")
    parser.add_argument("--warehouse-root", default="/ev-charging")
    parser.add_argument("--local-parquet", action="store_true",
                        help="按本地路径读 Parquet，不连 HDFS")
    parser.add_argument("--out", type=Path, default=Path("handoff/dws"))
    parser.add_argument("--generated-at", type=str, default=None)
    args = parser.parse_args()

    generated_at = (
        datetime.fromisoformat(args.generated_at).replace(tzinfo=CN_TZ)
        if args.generated_at
        else datetime.now(CN_TZ).replace(microsecond=0)
    )

    from pyspark.sql import SparkSession

    builder = SparkSession.builder.appName("ev-scml-dws-export").enableHiveSupport()
    if args.local_parquet:
        builder = builder.master("local[1]")
    spark = builder.getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    try:
        tables: dict[str, list[dict]] = {}
        for name in DWS_TABLES:
            path = f"{args.warehouse_root}/dws/{name}"
            rows = [row.asDict(recursive=True) for row in spark.read.parquet(path).collect()]
            tables[name] = rows
            print(f"[read] {path}: {len(rows)} rows", flush=True)
    finally:
        spark.stop()

    run_id = f"dws-{generated_at.strftime('%Y%m%d%H%M%S')}"
    manifest = write_dws(args.out, tables, run_id=run_id, generated_at=generated_at,
                         source_run_id="")
    print(f"[ok] {args.out}/manifest.json  runId={manifest['runId']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
