"""Part2 智能预测 · 训练与评估（#5 PE，分支 feat/part2-ml）。

用法（在伪分布式 Hadoop 上以 Spark on YARN 提交）：
    spark-submit --master yarn train.py \
        --dwd hdfs://pangxiangzhen01:8020/ev-charging/dwd/dwd_station_hourly \
        --model-out hdfs://pangxiangzhen01:8020/ev-charging/forecast/models \
        --metrics ./metrics.json

产出：
  * metrics.json：每个 horizon × 目标（load / busy）的 seasonal-naive 基线、
    GBT、RF 三组指标（MAE / RMSE / WAPE），以及模型选择结论；
  * HDFS 上的 PipelineModel（VectorAssembler + 回归器整体保存）。

模型选择（《05》§3）：验证集 MAE 最优者胜出；若最优模型不优于基线，
则结论标记为 ``naive``，由大屏如实展示基线结果，不伪造。
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import sys

# 保证与 features.py 同目录时可被 import（spark-submit 的 driver 端）
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pyspark.ml import Pipeline
from pyspark.ml.feature import VectorAssembler
from pyspark.ml.regression import GBTRegressor, RandomForestRegressor
from pyspark.sql import SparkSession

import features as ft


def build_pipeline(algo: str, feature_cols, label_col: str, seed: int) -> Pipeline:
    assembler = VectorAssembler(
        inputCols=list(feature_cols), outputCol="features", handleInvalid="skip"
    )
    if algo == "gbt":
        est = GBTRegressor(
            featuresCol="features", labelCol=label_col,
            maxIter=60, maxDepth=5, stepSize=0.1, seed=seed,
        )
    elif algo == "rf":
        est = RandomForestRegressor(
            featuresCol="features", labelCol=label_col,
            numTrees=60, maxDepth=6, seed=seed,
        )
    else:
        raise ValueError(f"未知算法: {algo}")
    return Pipeline(stages=[assembler, est])


TARGETS = (
    # (名称, 标签模板, 基线列模板, 物理上限列)
    ("load", "y_load_h{h}", "naive_load_h{h}", None),
    ("busy", "y_busy_h{h}", "naive_busy_h{h}", "pile_count"),
)


def main() -> int:
    ap = argparse.ArgumentParser(description="Part2 Spark MLlib 训练与评估")
    ap.add_argument("--dwd", required=True, help="dwd_station_hourly 路径（HDFS Parquet 或本地 CSV）")
    ap.add_argument("--model-out", required=True, help="模型输出根目录（建议 HDFS）")
    ap.add_argument("--metrics", default="./metrics.json", help="指标 JSON 输出路径（driver 本地）")
    ap.add_argument("--algos", default="gbt,rf", help="参与对照的算法，逗号分隔")
    ap.add_argument("--seed", type=int, default=20260914)
    ap.add_argument("--shuffle-partitions", type=int, default=8)
    args = ap.parse_args()

    algos = [a.strip() for a in args.algos.split(",") if a.strip()]

    spark = SparkSession.builder.appName("part2-forecast-train").getOrCreate()
    spark.conf.set("spark.sql.shuffle.partitions", str(args.shuffle_partitions))
    print(f"[train] master={spark.sparkContext.master} app={spark.sparkContext.applicationId}")

    raw = ft.read_hourly(spark, args.dwd)
    feat = ft.add_naive_baseline(ft.build_features(raw, ft.HORIZONS), ft.HORIZONS)

    train_df, valid_df, test_df, bounds = ft.split_by_time(feat, ft.HORIZONS)
    n_raw = raw.count()
    n_train, n_valid, n_test = train_df.count(), valid_df.count(), test_df.count()
    print(f"[train] rows raw={n_raw} train={n_train} valid={n_valid} test={n_test}")
    print(f"[train] bounds={bounds}")

    run_id = dt.datetime.now().strftime("%Y%m%d%H%M%S")
    report: dict = {
        "run_id": run_id,
        "generated_at": dt.datetime.now().isoformat(timespec="seconds"),
        "spark_version": spark.version,
        "master": spark.sparkContext.master,
        "dwd_path": args.dwd,
        "rows": {"raw": n_raw, "train": n_train, "valid": n_valid, "test": n_test},
        "split_bounds": bounds,
        "feature_cols": list(ft.FEATURE_COLS),
        "horizons": {},
    }

    for h in ft.HORIZONS:
        report["horizons"][f"h{h}"] = {}
        for name, label_tpl, naive_tpl, upper in TARGETS:
            label = label_tpl.format(h=h)
            naive = naive_tpl.format(h=h)
            cols = list(ft.FEATURE_COLS)

            tr = train_df.dropna(subset=[label] + cols)
            va = valid_df.dropna(subset=[label] + cols)
            te = test_df.dropna(subset=[label] + cols)

            entry: dict = {
                "label": label,
                "naive_col": naive,
                "rows": {"train": tr.count(), "valid": va.count(), "test": te.count()},
                "baseline": {
                    "valid": ft.evaluate(va, label, naive),
                    "test": ft.evaluate(te, label, naive),
                },
                "models": {},
            }

            best_algo, best_mae, best_model = None, None, None
            for algo in algos:
                pipeline = build_pipeline(algo, cols, label, args.seed)
                model = pipeline.fit(tr)

                va_p = ft.clip_to_physical(model.transform(va), "prediction", upper)
                te_p = ft.clip_to_physical(model.transform(te), "prediction", upper)
                m_va = ft.evaluate(va_p, label, "prediction")
                m_te = ft.evaluate(te_p, label, "prediction")

                entry["models"][algo] = {"valid": m_va, "test": m_te}
                print(f"[train] h={h} {name} {algo} valid_mae={m_va['mae']} test_mae={m_te['mae']}")

                if m_va["mae"] is not None and (best_mae is None or m_va["mae"] < best_mae):
                    best_algo, best_mae, best_model = algo, m_va["mae"], model

            base_mae = entry["baseline"]["valid"]["mae"]
            if best_algo is None or (base_mae is not None and best_mae is not None and best_mae >= base_mae):
                entry["selected"] = {
                    "algo": "naive",
                    "reason": "最优模型未优于 seasonal-naive 基线，按《05》§3 降级为基线",
                    "valid_mae": base_mae,
                }
                print(f"[train] h={h} {name} 选择基线 naive (valid_mae={base_mae})")
            else:
                entry["selected"] = {"algo": best_algo, "valid_mae": best_mae}
                path = f"{args.model_out.rstrip('/')}/{name}_h{h}_{best_algo}"
                best_model.write().overwrite().save(path)
                entry["selected"]["model_path"] = path
                print(f"[train] h={h} {name} 选择 {best_algo} -> {path}")

            report["horizons"][f"h{h}"][name] = entry

    with open(args.metrics, "w", encoding="utf-8") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2)
    print(f"[train] metrics -> {os.path.abspath(args.metrics)}")

    spark.stop()
    print("TRAIN_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
