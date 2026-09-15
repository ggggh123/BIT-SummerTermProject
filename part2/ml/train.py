"""Part2 智能预测 · 训练与评估（#5 PE，分支 feat/part2-ml）。

用法（在伪分布式 Hadoop 上以 Spark on YARN 提交）：
    spark-submit --master yarn train.py \
        --dwd       hdfs://pangxiangzhen01:8020/ev-charging/dwd/dwd_station_hourly \
        --model-out hdfs://pangxiangzhen01:8020/ev-charging/forecast/models \
        --metrics   ./metrics.json \
        --horizons 1-24 --rf-horizons 1,6,24

产出：
  * ``metrics.json``：每个 horizon × 目标（load / busy）的 seasonal-naive 基线、
    GBT 与 RF（仅 1/6/24，控制耗时）指标，以及 ADS 契约表 ``ads_forecast_metric``
    的换算结果（``wape`` 已按契约 ×100，见 #4 的 ``warehouse/sql/ads_schema.sql`` §7）；
  * HDFS 上的 ``PipelineModel``（VectorAssembler + 回归器随 Pipeline 保存）。

模型选择（《05》§3）：验证集 MAE 最优者胜出；若最优模型不优于 seasonal-naive 基线，
则结论标记为 ``naive`` 降级，由大屏如实展示，不伪造。

契约对齐（#4 冻结）：
  * ``ads_forecast_24h`` 要求 horizon 覆盖 **1–24**，故默认训练全部 24 个步长；
  * ``wape`` 单位为百分比（0–100）。
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


def parse_horizons(text: str) -> list[int]:
    """解析步长表达式：``1-24`` / ``1,6,24`` / 混合写法。"""
    out: list[int] = []
    for part in str(text).split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            lo, hi = part.split("-", 1)
            out.extend(range(int(lo), int(hi) + 1))
        else:
            out.append(int(part))
    return sorted(set(out))


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
    ap.add_argument("--horizons", default="1-24", help="训练步长，支持 1-24 或 1,6,24")
    ap.add_argument("--rf-horizons", default="1,6,24",
                    help="对照组（非 GBT）只在这些步长上训练，控制总耗时")
    ap.add_argument("--seed", type=int, default=20260914)
    ap.add_argument("--shuffle-partitions", type=int, default=8)
    args = ap.parse_args()

    algos = [a.strip() for a in args.algos.split(",") if a.strip()]
    horizons = parse_horizons(args.horizons)
    rf_horizons = set(parse_horizons(args.rf_horizons))
    if not horizons or any(horizon < 1 or horizon > 24 for horizon in horizons):
        raise SystemExit("--horizons 必须是 1..24 的非空子集")
    if not algos or any(algo not in {"gbt", "rf"} for algo in algos):
        raise SystemExit("--algos 只支持 gbt,rf")
    os.makedirs(os.path.dirname(os.path.abspath(args.metrics)), exist_ok=True)

    spark = SparkSession.builder.appName("part2-forecast-train").getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    spark.conf.set("spark.sql.shuffle.partitions", str(args.shuffle_partitions))
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")
    print(f"[train] master={spark.sparkContext.master} app={spark.sparkContext.applicationId}")
    print(f"[train] horizons={horizons[0]}..{horizons[-1]} ({len(horizons)} 个) 对照组={sorted(rf_horizons)}")

    raw = ft.read_hourly(spark, args.dwd)
    feat = ft.add_naive_baseline(ft.build_features(raw, horizons), horizons)

    train_df, valid_df, test_df, bounds = ft.split_by_time(feat, horizons)
    # 54 个模型共享同一组窗口特征；不缓存会为每个模型重复构造完整窗口计划。
    train_df.cache()
    valid_df.cache()
    test_df.cache()
    n_raw = raw.count()
    split_counts = (train_df.count(), valid_df.count(), test_df.count())
    if not all(split_counts):
        raise ValueError(f"时间切分产生空数据集：train/valid/test={split_counts}")
    print(f"[train] rows raw={n_raw} train={split_counts[0]} valid={split_counts[1]} test={split_counts[2]}")
    print(f"[train] bounds={bounds}")

    run_id = dt.datetime.now().strftime("%Y%m%d%H%M%S")
    report: dict = {
        "run_id": run_id,
        "generated_at": dt.datetime.now().isoformat(timespec="seconds"),
        "spark_version": spark.version,
        "master": spark.sparkContext.master,
        "dwd_path": args.dwd,
        "rows": {"raw": n_raw},
        "split_bounds": bounds,
        "horizons_trained": horizons,
        "rf_horizons": sorted(rf_horizons),
        "feature_cols": list(ft.FEATURE_COLS),
        "horizons": {},
    }

    for h in horizons:
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
                "baseline": {
                    "valid": ft.evaluate(va, label, naive),
                    "test": ft.evaluate(te, label, naive),
                },
                "models": {},
            }

            best_algo, best_mae, best_model = None, None, None
            for algo in algos:
                if algo != "gbt" and h not in rf_horizons:
                    continue
                pipeline = build_pipeline(algo, cols, label, args.seed)
                model = pipeline.fit(tr)

                va_p = ft.clip_to_physical(model.transform(va), "prediction", upper)
                te_p = ft.clip_to_physical(model.transform(te), "prediction", upper)
                m_va = ft.evaluate(va_p, label, "prediction")
                m_te = ft.evaluate(te_p, label, "prediction")

                entry["models"][algo] = {"valid": m_va, "test": m_te}
                mark = " *" if h in ft.REPORT_HORIZONS else ""
                print(f"[train] h={h:<2d} {name:<4s} {algo} valid_mae={m_va['mae']} test_mae={m_te['mae']}{mark}")

                if m_va["mae"] is not None and (best_mae is None or m_va["mae"] < best_mae):
                    best_algo, best_mae, best_model = algo, m_va["mae"], model

            base_mae = entry["baseline"]["valid"]["mae"]
            if best_algo is None or (base_mae is not None and best_mae is not None and best_mae >= base_mae):
                entry["selected"] = {
                    "algo": "naive",
                    "reason": "最优模型未优于 seasonal-naive 基线，按《05》§3 降级为基线",
                    "valid_mae": base_mae,
                }
            else:
                entry["selected"] = {"algo": best_algo, "valid_mae": best_mae}
                path = f"{args.model_out.rstrip('/')}/{name}_h{h}_{best_algo}"
                best_model.write().overwrite().save(path)
                entry["selected"]["model_path"] = path

            report["horizons"][f"h{h}"][name] = entry

    # ---------------- ADS 契约表 ads_forecast_metric（#4 冻结列集） ----------------
    metric_rows = []
    for h in horizons:
        entry = report["horizons"][f"h{h}"]["load"]
        algo = entry["selected"]["algo"]
        m = entry["baseline"]["test"] if algo == "naive" else entry["models"][algo]["test"]
        baseline = entry["baseline"]["test"]

        def _pct(v):
            return None if v is None else round(float(v) * 100.0, 4)

        metric_rows.append({
            "horizon_h": h,
            "mae": round(float(m["mae"]), 4) if m["mae"] is not None else None,
            "rmse": round(float(m["rmse"]), 4) if m["rmse"] is not None else None,
            "wape": _pct(m["wape"]),
            "baseline_wape": _pct(baseline["wape"]),
        })
    report["ads_forecast_metric"] = metric_rows

    with open(args.metrics, "w", encoding="utf-8") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2)
    print(f"[train] metrics -> {os.path.abspath(args.metrics)}")
    print(f"[train] ads_forecast_metric: {len(metric_rows)} 行（wape 已 ×100）")

    for h in ft.REPORT_HORIZONS:
        if f"h{h}" in report["horizons"]:
            e = report["horizons"][f"h{h}"]["load"]
            print(f"[train][report] h={h} load selected={e['selected']['algo']} "
                  f"test_mae={e['models'][e['selected']['algo']]['test']['mae'] if e['selected']['algo'] != 'naive' else e['baseline']['test']['mae']} "
                  f"baseline_mae={e['baseline']['test']['mae']}")

    train_df.unpersist()
    valid_df.unpersist()
    test_df.unpersist()
    spark.stop()
    print("TRAIN_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
