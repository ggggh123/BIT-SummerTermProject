"""Part2 智能预测 · 未来预测生成（#5 PE，分支 feat/part2-ml）。

以 DWD 层 ``dwd_station_hourly`` 中每个站点的**最后一个观测时刻 t0** 为预测起点，
用训练阶段选定的直接式模型预测 t0+h（h ∈ --horizons，默认 **1-24**）的站点负荷与
占用桩数，并派生空闲桩、拥堵等级与高峰标记。

**输出严格对齐 #4 冻结的 ADS 契约表** ``ads_forecast_24h``
（``part2/scml/warehouse/sql/ads_schema.sql`` §7）：

    run_id TEXT / station_id INT / forecast_at TEXT(ISO 8601 +08:00) / horizon_h INT
    predicted_load_kw REAL / predicted_busy_count INT / predicted_idle_count INT
    congestion_level TEXT(low|medium|high) / is_peak INT(0|1)

要点：
  * ``is_peak`` = 未来 24h 内负荷最大的**连续 2h**，该 2 小时**两个点都标 1**
    （与 #1 的 ``web/src/mock/forecast_24h.json`` 契约一致）；
  * ``congestion_level``：预测占用率 ≥80% 为 high，≥50% 为 medium，其余 low；
  * 物理约束见下节。

负荷与占用的一致性（依据 ``tmp/diag_low.py`` 的实测诊断）
--------------------------------------------------------
自测数据中 ``load_kw`` 与 ``busy_count`` 同号：``busy = 0`` 时 ``load`` 必为 0，
且 ``hour = 0`` 的负荷有约 10% 的样本真的是 0（P05 = 0）。因此：

  1. **不对占用做「下界抬升」** —— 凌晨 ``busy = 0`` 是数据的真实形态，
     用历史分位把 0 抬成 1 反而会制造与负荷不一致的占用；
  2. 先整数化占用，再用「占用锚定」修正负荷：
     ``busy = 0`` → ``load = 0``；``busy > 0`` → ``load ≥ busy × 该站该整点历史单桩功率 P05``；
  3. 这样既避免 GBT 在低谷外推出 0 附近的病态值，又保证 ``busy + idle = pile_count``。

    spark-submit --master yarn predict.py \
        --dwd       hdfs://pangxiangzhen01:8020/ev-charging/dwd/dwd_station_hourly \
        --dim-stations hdfs://pangxiangzhen01:8020/ev-charging/dwd/dim_stations \
        --model-out hdfs://pangxiangzhen01:8020/ev-charging/forecast/models \
        --metrics   ./metrics.json \
        --out       hdfs://pangxiangzhen01:8020/ev-charging/ads/ads_forecast_24h
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
from train import parse_horizons

#: 拥堵阈值（《05》§2.3）
CONGESTION_HIGH = 0.80
CONGESTION_MEDIUM = 0.50

#: 单桩功率缺省下限（kW）：最小额定功率 7 kW × 50% 负载率，仅作兜底
DEFAULT_KW_PER_BUSY = 3.5

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


def mark_peak_hours(frame, max_horizon: int):
    """每站选唯一的最佳完整两小时窗口，并把窗口内两个点标为峰值。"""
    ordered = Window.partitionBy("station_id").orderBy("horizon_h")
    pair_average = F.avg("predicted_load_kw").over(ordered.rowsBetween(0, 1))
    ranked = Window.partitionBy("station_id").orderBy(
        F.col("_pair").desc_nulls_last(), F.col("horizon_h").asc()
    )
    return (
        frame.withColumn(
            "_pair",
            F.when(F.col("horizon_h") < F.lit(max_horizon), pair_average),
        )
        .withColumn("_pair_rank", F.row_number().over(ranked))
        .withColumn(
            "_pair_start",
            (F.col("_pair_rank") == 1) & F.col("_pair").isNotNull(),
        )
        .withColumn(
            "_follows_pair_start",
            F.coalesce(F.lag("_pair_start", 1).over(ordered), F.lit(False)),
        )
        .withColumn(
            "is_peak",
            F.when(F.col("_pair_start") | F.col("_follows_pair_start"), 1)
            .otherwise(0)
            .cast("int"),
        )
        .drop("_pair", "_pair_rank", "_pair_start", "_follows_pair_start")
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="Part2 预测生成")
    ap.add_argument("--dwd", required=True)
    ap.add_argument("--dim-stations", default="", help="可选：dim_stations，用于按 forecast_enabled 过滤站点")
    ap.add_argument("--model-out", required=True)
    ap.add_argument("--metrics", default="")
    ap.add_argument("--out", required=True, help="输出路径，建议 .../ads/ads_forecast_24h")
    ap.add_argument("--horizons", default="1-24")
    ap.add_argument("--default-algo", default="gbt")
    ap.add_argument("--run-id", default="")
    args = ap.parse_args()

    horizons = parse_horizons(args.horizons)
    if not horizons or horizons != list(range(1, max(horizons) + 1)) or max(horizons) > 24:
        raise SystemExit("预测输出要求 --horizons 从 1 连续覆盖到不超过 24")
    run_id = args.run_id or f"f-{dt.datetime.now().strftime('%Y%m%d-%H%M%S')}"

    spark = SparkSession.builder.appName("part2-forecast-predict").getOrCreate()
    spark.sparkContext.setLogLevel("WARN")
    spark.conf.set("spark.sql.shuffle.partitions", "8")
    spark.conf.set("spark.sql.session.timeZone", "Asia/Shanghai")
    print(f"[predict] master={spark.sparkContext.master} app={spark.sparkContext.applicationId}")
    print(f"[predict] run_id={run_id} horizons={horizons[0]}..{horizons[-1]} ({len(horizons)} 个)")

    raw = ft.read_hourly(spark, args.dwd)
    feat = ft.add_naive_baseline(ft.build_features(raw, horizons), horizons)

    base = latest_base(feat).select(
        *(c for c in ["station_id", "observed_at", "pile_count", "rated_power_kw"]
          + list(ft.FEATURE_COLS)
          + [f"naive_load_h{h}" for h in horizons]
          + [f"naive_busy_h{h}" for h in horizons]
          if c in feat.columns)
    )

    if args.dim_stations:
        dim = spark.read.parquet(args.dim_stations)
        if "forecast_enabled" in dim.columns:
            enabled = dim.filter(F.col("forecast_enabled").cast("int") == 1).select("station_id")
            before = base.count()
            base = base.join(enabled, on="station_id", how="inner")
            print(f"[predict] forecast_enabled 过滤：{before} -> {base.count()} 个站点")

    base.cache()
    t0 = base.agg(F.max("observed_at")).first()[0]
    n_station = base.count()
    print(f"[predict] 起点 t0={t0} 站点数={n_station}")

    selection = load_selection(args.metrics, args.default_algo)
    if not selection:
        print("[predict] 未提供 metrics.json，全部 horizon 使用 --default-algo")

    # 每个 horizon 的输出只有“启用站点数”行（正式数据为 5 行）。不能把 24 个
    # PipelineModel 的 transform 直接 union 成一张超长 lineage：Spark 会把全部树模型
    # 一次序列化给同一个 executor，低内存演示机在最终 write 时容易 OOM。这里在每个
    # horizon 完成后只 collect 这个有明确上限的小结果，再构造 24h 轻量 DataFrame。
    # collect 的不是 DWD 原始数据，最大规模为 station_count × 24（本项目 ≤ 192 行）。
    prediction_rows, versions = [], {}
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

        acc = ft.clip_to_physical(acc, "predicted_load_kw", None, 0.0)
        acc = ft.clip_to_physical(acc, "predicted_busy_count", "pile_count", 0.0)

        frame = (
            acc.withColumn("horizon_h", F.lit(h))
            .withColumn("forecast_at_col", F.col("observed_at") + F.expr(f"INTERVAL {h} HOURS"))
            .select(
                "station_id", "forecast_at_col", "horizon_h",
                "predicted_load_kw", "predicted_busy_count", "pile_count",
            )
        )
        prediction_rows.extend(row.asDict(recursive=True) for row in frame.collect())

    expected_prediction_rows = n_station * len(horizons)
    if len(prediction_rows) != expected_prediction_rows:
        raise ValueError(
            f"预测行数异常：{len(prediction_rows)}，期望 {n_station} × {len(horizons)}"
        )
    allp = spark.createDataFrame(prediction_rows)

    # ---- 1) 占用整数化：仅做上下界裁剪，不做下界抬升 ----
    allp = allp.withColumn(
        "predicted_busy_count",
        F.round(
            F.greatest(
                F.least(F.col("predicted_busy_count"), F.col("pile_count").cast("double")),
                F.lit(0.0),
            )
        ).cast("int"),
    )

    # ---- 2) 负荷按占用锚定：busy=0 → load=0；busy>0 → load ≥ busy × 历史单桩功率 P05 ----
    kw_per_busy = (
        feat.filter(F.col("busy_count") > 0)
        .groupBy("station_id", "hour")
        .agg(
            F.expr("percentile_approx(load_kw / busy_count, 0.05)")
            .cast("double")
            .alias("_kw_per_busy_p05")
        )
    )
    allp = allp.withColumn("hour", F.hour("forecast_at_col")).join(
        kw_per_busy, on=["station_id", "hour"], how="left"
    )
    allp = allp.withColumn(
        "predicted_load_kw",
        F.when(F.col("predicted_busy_count") <= 0, F.lit(0.0)).otherwise(
            F.greatest(
                F.col("predicted_load_kw"),
                F.col("predicted_busy_count").cast("double")
                * F.coalesce(F.col("_kw_per_busy_p05"), F.lit(DEFAULT_KW_PER_BUSY)),
            )
        ),
    )

    # ---- 3) 空闲桩守恒 + 拥堵分级 ----
    allp = (
        allp.withColumn(
            "predicted_idle_count",
            F.greatest(F.col("pile_count") - F.col("predicted_busy_count"), F.lit(0)).cast("int"),
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

    # ---- 4) is_peak：最佳完整两小时窗口；并列时取最早窗口 ----
    # mark_peak_hours 同时保证每个站点只产生两个连续峰值点，且不会把第 24
    # 小时这个不完整的单点窗口误选为峰值。
    allp = mark_peak_hours(allp, max(horizons))

    result = allp.select(
        F.lit(run_id).cast("string").alias("run_id"),
        F.col("station_id").cast("int").alias("station_id"),
        F.date_format("forecast_at_col", "yyyy-MM-dd'T'HH:mm:ssXXX").alias("forecast_at"),
        F.col("horizon_h").cast("int").alias("horizon_h"),
        F.round("predicted_load_kw", 3).alias("predicted_load_kw"),
        F.col("predicted_busy_count").cast("int").alias("predicted_busy_count"),
        F.col("predicted_idle_count").cast("int").alias("predicted_idle_count"),
        F.col("congestion_level").cast("string").alias("congestion_level"),
        F.col("is_peak").cast("int").alias("is_peak"),
    ).orderBy("station_id", "horizon_h")

    result.write.mode("overwrite").parquet(args.out)
    n_out = result.count()
    peak_cnt = result.filter(F.col("is_peak") == 1).count()
    zero_load = result.filter(F.col("predicted_load_kw") <= 0.001).count()
    inconsistent = result.filter(
        ((F.col("predicted_busy_count") > 0) & (F.col("predicted_load_kw") <= 0.001))
        | ((F.col("predicted_busy_count") <= 0) & (F.col("predicted_load_kw") > 0.001))
    ).count()
    print(f"[predict] ads_forecast_24h rows={n_out} is_peak={peak_cnt} "
          f"zero_load={zero_load} busy/load 不一致行={inconsistent} -> {args.out}")
    result.filter(F.col("horizon_h").isin(1, 6, 24)).orderBy("station_id", "horizon_h").show(30, truncate=False)

    meta_path = os.path.join(os.path.dirname(os.path.abspath(args.metrics or "./_.json")), "predict_meta.json")
    try:
        with open(meta_path, "w", encoding="utf-8") as fh:
            json.dump(
                {"run_id": run_id, "generated_at": dt.datetime.now().isoformat(timespec="seconds"),
                 "horizons": horizons, "t0": str(t0), "stations": n_station,
                 "rows": n_out, "peak_rows": peak_cnt, "zero_load_rows": zero_load,
                 "busy_load_inconsistent_rows": inconsistent, "model_versions": versions,
                 "output": args.out},
                fh, ensure_ascii=False, indent=2,
            )
    except OSError as exc:
        print(f"[predict] 写 meta 失败（不影响结果）: {exc}")

    spark.stop()
    print("PREDICT_DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
