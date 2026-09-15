#!/usr/bin/env bash
# =============================================================================
# DWS + ADS 一键作业（在伪分布式 Hadoop 虚拟机上执行）
# =============================================================================
# 前置：
#   1. `source /etc/profile.d/ev-second-project.sh`（JDK/Hadoop/Spark 环境变量）
#   2. ODS 已入 HDFS：`bash scripts/hdfs_put_ods.sh`
#   3. #3 的 DWD 已入 HDFS：`hdfs dfs -put handoff/dwd /ev-charging/`
#      —— 缺 DWD 时本脚本会在 DWS 那一步直接失败，这是刻意的：
#         不允许「静默出一份空表」当成交付物。
#
# 用法：
#   bash scripts/run_dws_ads.sh                     # 全流程
#   bash scripts/run_dws_ads.sh --dry-run           # 只打印要执行的 SQL，不连 Spark
#   bash scripts/run_dws_ads.sh --skip-reconcile    # 跳过对账（不推荐）
#
# 可选环境变量：
#   FORECAST_HANDOFF=handoff/forecast               # 接入 #5 真实预测包，替换 ADS 预测三表
#
# 产出：
#   HDFS  <root>/dws/*、<root>/ads/*              （Parquet，验收用）
#   本地  handoff/dws/、handoff/ads/ads.db        （交接给 #2/#1/#5）
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

HDFS_ROOT="${HDFS_ROOT:-/ev-charging}"
ODS_DIR="${ODS_DIR:-handoff/ods}"
HANDOFF_DWS="${HANDOFF_DWS:-handoff/dws}"
HANDOFF_ADS="${HANDOFF_ADS:-handoff/ads}"
FORECAST_HANDOFF="${FORECAST_HANDOFF:-}"
PYTHON="${PYTHON:-python3}"
SPARK_SUBMIT="${SPARK_SUBMIT:-spark-submit}"

DRY_RUN=""
SKIP_RECONCILE=""
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN="--dry-run" ;;
    --skip-reconcile) SKIP_RECONCILE="1" ;;
    *) echo "[warn] 未知参数：$arg" ;;
  esac
done

step() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }

step "0/5 环境自检"
if [ -z "$DRY_RUN" ]; then
  command -v "$SPARK_SUBMIT" >/dev/null || { echo "[fail] 找不到 spark-submit"; exit 1; }
  hdfs dfs -test -d "${HDFS_ROOT}/dwd" || {
    echo "[fail] ${HDFS_ROOT}/dwd 不存在；先把 #3 的 DWD 入 HDFS"
    exit 1
  }
  echo "spark-submit : $(command -v "$SPARK_SUBMIT")"
  echo "hdfs root    : ${HDFS_ROOT}"
fi

step "1/5 DWS 四表（dws_schema.sql + dws_etl.sql）"
$SPARK_SUBMIT warehouse/jobs/build_dws.py \
  --sql-dir warehouse/sql --hdfs-root "$HDFS_ROOT" $DRY_RUN

step "2/5 ADS 业务指标表（ads_etl.sql）"
$SPARK_SUBMIT warehouse/jobs/build_ads.py \
  --sql-dir warehouse/sql --hdfs-root "$HDFS_ROOT" $DRY_RUN

if [ -n "$DRY_RUN" ]; then
  step "dry-run 结束（未连 Spark、未写 HDFS）"
  exit 0
fi

step "3/5 导出 handoff/dws（Parquet -> CSV 交接包，供 #5/#2 复核）"
$SPARK_SUBMIT warehouse/jobs/export_dws_csv.py \
  --warehouse-root "$HDFS_ROOT" --out "$HANDOFF_DWS"

step "4/5 导出 handoff/ads/ads.db"
FORECAST_ARGS=()
if [ -n "$FORECAST_HANDOFF" ]; then
  FORECAST_ARGS=(--forecast-handoff "$FORECAST_HANDOFF")
fi
$SPARK_SUBMIT warehouse/jobs/export_ads_db.py \
  --warehouse-root "$HDFS_ROOT" --ods "$ODS_DIR" --out "$HANDOFF_ADS" \
  "${FORECAST_ARGS[@]}"

if [ -n "$SKIP_RECONCILE" ]; then
  step "5/5 对账（已跳过）"
else
  step "5/5 三层对账（ODS -> DWS -> ADS，误差 0）"
  "$PYTHON" warehouse/jobs/reconcile.py \
    --ods "$ODS_DIR" --dws "$HANDOFF_DWS" \
    --ads "${HANDOFF_ADS}/ads.db" \
    --dwd "${HDFS_ROOT}/dwd" --require-dwd
fi

step "完成"
echo "DWS Parquet : ${HDFS_ROOT}/dws"
echo "DWS 交接包  : ${HANDOFF_DWS}/manifest.json"
echo "ADS Parquet : ${HDFS_ROOT}/ads"
echo "SQLite 交接 : ${HANDOFF_ADS}/ads.db"
