"""Part2 智能预测 · 未来预测生成（#5 PE，分支 feat/part2-ml）。

以 DWD 层 dwd_station_hourly 中每个站点的**最后一个观测时刻 t0** 为预测起点，
使用训练阶段选定的直接式模型预测 t0+h（h ∈ --horizons，默认 1/6/24）的
站点负荷与占用桩数，并派生：

  * ``predicted_idle_count = pile_count - predicted_busy_count``
  * ``congestion_level ∈ {low, medium, high}``：按预测占用率 busy / pile_count
    （≥80% 为 high，≥50% 为 medium）
  * ``is_peak``：未来窗口内负荷最高的连续区间起点（horizon 为 1/6/24 时，
    以相邻 horizon 两点构成的区间近似「连续 2h」）

输出表 ``ads_forecast_result`` 写 HDFS（Parquet）。

    spark-submit --master yarn predict.py \
        --dwd     hdfs://pangxiangzhen01:8020/ev-charging/dwd/dwd_station_hourly \
        --model-out hdfs://pangxiangzhen01:8020/ev-charging/forecast/models \
        --metrics ./metrics.json \
        --out     hdfs://pangxiangzhen01:8020/ev-charging/ads/ads_forecast_result
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pyspark.ml import PipelineModel
from pyspark.sql import SparkSession, Window
from pyspark.sql import functions as F

import features as ft

#: 拥堵阈值（《05》§2.3）
CONGESTION_HIGH = 0.80
CONGESTION_MEDIUM = 0.50

TARGETS = (("load", "predicted_load_kw"), ("busy", "predicted_busy_count"))


def load_selection(metrics_path: str, default_algo: str) -> dict:
    """从 metrics.json 读取每个 (horizon, 目标) 在验证集上选定的算法与模型路径。"""
    sel: dict = {}
    if not metrics_path or not os.path.exists(metrics_path):
        return sel
    with open(metrics_path, encoding="utf-8") as fh:
        report = json.load(fh)
    for hkey, targets in report.get("horizons", {}).items():
        try:
            h = int(str(hkey).lstrip("hH"))
        except ValueError:
            continue
        for name, entry in targets.items():
            chosen = entry.get("selected", {})
            sel[(h, name)] = (chosen.get("algo", default_algo), chosen.get("model_path"))
    return sel


def latest_base(feat):
    """每个站点取最后一行作为预测起点，该行已带全部滞后 / 滚动特征。"""
    w = Window.partitionBy("station_id").orderBy(F.col("observed_at").desc())
    return feat.withColumn("_rn", F.row_number().over(w)).filter(F.col("_rn") == 1).drop("_rn")


def main() -> int:
    ap = argparse.ArgumentParser(description="Part2 预测生成")
    ap.add_argument("--dwd", required=True)
    ap.add_argument("--model-out", required=True)
    ap.add_argument("--metrics", default="")
    ap.add_argument("--out", required=True)
    ap.add_argument("--horizons", default="1,6,24")
    ap.add_argument("--default-algo", default="gbt")
    ap.add_argument("--run-id", default="")
    args = ap.parse_args()

    horizons = [int(x) for x in args.horizons.split(",") if x.strip()]
    run_id = args.run_id or dt.datetime.now().strftime("%Y%m%d%H%M%S")
    generated_at = dt.datetime.now().isoformat(timespec="seconds")

    spark = SparkSession.builder.appName("part2-forecast-predict").getOrCreate()
    spark.conf.set("spark.sql.shuffle.partitions", "8")
    print(f"[predict] master={spark.sparkContext.master} app={spark.sparkContext.applicationId}")

    raw = ft.read_hourly(spark, args.dwd)
    feat = ft.add_naive_baseline(ft.build_features(raw, horizons), horizons)

    base = latest_base(feat).select(
        *(c for c in ["station_id", "observed_at", "pile_count", "rated_power_kw"]
          + list(ft.FEATURE_COLS)
          + [f"naive_load_h{h}" for h in horizons]
          + [f"naive_busy_h{h}" for h in horizons]
          if c in feat.columns)
    )
    base.cache()
    t0 = base.agg(F.max("observed_at")).first()[0]
    n_station = base.count()
    print(f"[predict] 起点 t0={t0} 站点数={n_station} horizons={horizons}")

    selection = load_selection(args.metrics, args.default_algo)
    if not selection:
        print("[predict] 未提供 metrics.json，全部 horizon 使用 --default-algo")

    frames, versions = [], {}
    for h in horizons:
        acc = base.select("station_id", "observed_at", "pile_count", "rated_power_kw")
        for name, out_col in TARGETS:
            algo, model_path = selection.get((h, name), (args.default_algo, None))
            if algo in (None, "naive"):
                naive_col = f"naive_{name}_h{h}"
                pred = base.select(
                    F.col("station_id"),
                    F.col("observed_at"),
                    F.col(naive_col).cast("double").alias(out_col),
                )
                versions[f"h{h}_{name}"] = "naive(seasonal)"
            else:
                path = model_path or f"{args.model_out.rstrip('/')}/{name}_h{h}_{algo}"
                model = PipelineModel.load(path)
                pred = model.transform(base).select(
                    F.col("station_id"),
                    F.col("observed_at"),
                    F.col("prediction").cast("double").alias(out_col),
                )
                versions[f"h{h}_{name}"] = f"{algo}@{path}"
            acc = acc.join(pred, on=["station_id", "observed_at"], how="left")

        # 物理约束：负荷非负；占用不超过总桩数且非负
        acc = ft.clip_to_physical(acc, "predicted_load_kw", None, 0.0)
        acc = ft.clip_to_physical(acc, "predicted_busy_count", "pile_count", 0.0)

        frames.append(
            acc.withColumn("horizon_h", F.lit(h))
            .withColumn("forecast_at", F.col("observed_at") + F.expr(f"INTERVAL {h} HOURS"))
            .select(
                "station_id", "forecast_at", "horizon_h",
                "predicted_load_kw", "predicted_busy_count", "pile_count",
            )
        )
        print(f"[predict] h={h} 完成，versions={ {k: v for k, v in versions.items() if k.startswith(f'h{h}_')} }")

    allp = frames[0]
    for fr in frames[1:]:
        allp = allp.unionByName(fr)

    allp = (
        allp.withColumn(
            "predicted_idle_count",
            F.greatest(F.col("pile_count").cast("double") - F.col("predicted_busy_count"), F.lit(0.0)),
        )
        .withColumn(
            "occupancy",
            F.when(F.col("pile_count") > 0, F.col("predicted_busy_count") / F.col("pile_count"))
            .otherwise(F.lit(0.0)),
        )
        .withColumn(
            "congestion_level",
            F.when(F.col("occupancy") >= CONGESTION_HIGH, "high")
            .when(F.col("occupancy") >= CONGESTION_MEDIUM, "medium")
            .otherwise("low"),
        )
    )

    # 高峰：以相邻 horizon 两点为一组，负荷最高的组起点记为 is_peak
    w_station = Window.partitionBy("station_id").orderBy("forecast_at")
    allp = allp.withColumn("_pair_avg", F.avg("predicted_load_kw").over(w_station.rowsBetween(0, 1)))
    rank_w = Window.partitionBy("station_id").orderBy(F.col("_pair_avg").desc_nulls_last(), F.col("forecast_at"))
    allp = (
        allp.withColumn("_rk", F.row_number().over(rank_w))
        .withColumn("is_peak", F.col("_rk") == 1)
        .drop("_rk", "_pair_avg")
    )

    result = allp.select(
        F.lit(run_id).alias("run_id"),
        F.col("station_id").cast("int").alias("station_id"),
        F.col("forecast_at").alias("forecast_at"),
        F.col("horizon_h").cast("int").alias("horizon_h"),
        F.round("predicted_load_kw", 2).alias("predicted_load_kw"),
        F.round("predicted_busy_count", 2).alias("predicted_busy_count"),
        F.round("predicted_idle_count", 2).alias("predicted_idle_count"),
        F.col("congestion_level"),
        F.col("is_peak"),
        F.lit(run_id).alias("model_version"),
        F.lit(generated_at).alias("generated_at"),
    ).orderBy("station_id", "horizon_h")

    result.write.mode("overwrite").parquet(args.out)
    n_out = result.count()
    print(f"[predict] ads_forecast_result rows={n_out} -> {args.out}")
    result.show(24, truncate=False)

    meta_path = os.path.join(os.path.dirname(os.path.abspath(args.metrics or "./_.json")), "predict_meta.json")
    try:
        with open(meta_path, "w", encoding="utf-8") as fh:
            json.dump(
                {"run_id": run_id, "generated_at": generated_at, "horizons": horizons,
                 "t0": str(t0), "stations": n_station, "rows": n_out,
                 "model_versions": versions, "output": args.out},
                fh, ensure_ascii=False, indent=2,
            )
    except OSError as exc:
        print(f"[predict] 写 meta 失败（不影响结果）: {exc}")

    spark.stop()
    print("PREDICT_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
