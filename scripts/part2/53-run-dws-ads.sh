#!/usr/bin/env bash
# =============================================================================
# 53-run-dws-ads.sh —— 跑 DWS / ADS 分层（老师要求第 5 步：SparkSQL 分层分析）
# =============================================================================
# 前置：先跑完 51-run-prl.sh 与 52-publish-dwd.sh —— DWD 必须已经发布到
#       $HDFS_ROOT/dwd 并且注册进 `ev_charging` 库，否则本脚本会在 DWS 那一步报
#       TABLE_OR_VIEW_NOT_FOUND。
#
# 本脚本只做三件事：把本机工具链环境准备好 → 调官方 `part2/scml/scripts/run_dws_ads.sh`
# → 打印产出。分层 SQL 与对账逻辑一律不在本脚本里重写。
#
# 已知缺陷（不是数据问题，答辩时别被"对账失败"吓到）
# --------------------------------------------------
# `run_dws_ads.sh` 第 5 步的内置对账与新布局不兼容，会报 24/30、6 项失败：
#   ① 小时表容差写死 0（15,073 vs 15,120，缺的 47 小时是已知事实）
#   ② "重算"走的是 SCML 的 Python 清洗口径（112,422 单），与 PRL 清洗后的官方口径
#      （97,804 单）不可比
#   ③ 找 DWD 时不认 `dt=` 分区目录
# 需要看干净产出时用 `--skip-reconcile`，把对账结论单独记录，别让它掩盖真实产出。
#
# 用法：
#   bash scripts/part2/53-run-dws-ads.sh
#   bash scripts/part2/53-run-dws-ads.sh --skip-reconcile
#   bash scripts/part2/53-run-dws-ads.sh --forecast-handoff handoff/forecast
#        ↑ 把 #5 的预测交接包一起并进 ads.db（不带就会按设计退回 seasonal-naive 基线）
#
# 可选环境变量：
#   PART2_VENV / EV_SHIM_DIR / HDFS_ROOT / PART2_WORK  同 51-run-prl.sh
#   ADS_DB      交接库输出路径，默认 <repo>/handoff/ads/ads.db
# =============================================================================
set -uo pipefail

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SELF_DIR/../.." && pwd)"
cd "$REPO_ROOT" || exit 1

VENV="${PART2_VENV:-$HOME/venvs/part2}"
SHIM_DIR="${EV_SHIM_DIR:-$HOME/ev-shim}"
HDFS_ROOT="${HDFS_ROOT:-/ev-charging}"
ODS_DIR="${ODS_DIR:-$REPO_ROOT/handoff/ods}"
HANDOFF_DWS="${HANDOFF_DWS:-$REPO_ROOT/handoff/dws}"
HANDOFF_ADS="${HANDOFF_ADS:-$REPO_ROOT/handoff/ads}"
FORECAST_HANDOFF="${FORECAST_HANDOFF:-}"
EXTRA=()

while [ $# -gt 0 ]; do
  case "$1" in
    --skip-reconcile) EXTRA+=(--skip-reconcile); shift ;;
    --dry-run) EXTRA+=(--dry-run); shift ;;
    --forecast-handoff) FORECAST_HANDOFF="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,35p' "$0"; exit 0 ;;
    *) EXTRA+=("$1"); shift ;;
  esac
done

export PATH="$SHIM_DIR:$PATH"
export PYSPARK_PYTHON="$VENV/bin/python"
export PYSPARK_DRIVER_PYTHON="$VENV/bin/python"
export SPARK_SUBMIT="${SPARK_SUBMIT:-$SHIM_DIR/spark-submit-derby.sh}"
export PYTHON="$VENV/bin/python"
export HDFS_ROOT ODS_DIR HANDOFF_DWS HANDOFF_ADS
[ -n "$FORECAST_HANDOFF" ] && export FORECAST_HANDOFF

echo "===== DWS / ADS 分层（SparkSQL）====="
echo "  仓库        : $REPO_ROOT"
echo "  HDFS 根     : $HDFS_ROOT"
echo "  DWS 交接包  : $HANDOFF_DWS"
echo "  ADS 交接库  : $HANDOFF_ADS/ads.db"
echo "  预测交接包  : ${FORECAST_HANDOFF:-（未指定，ads.db 会退回 seasonal-naive 基线）}"
echo

if ! command -v hdfs >/dev/null 2>&1; then
  echo "[fail] PATH 里没有 hdfs —— 非 login shell 的常见现象，用 bash -l 跑本脚本"
  exit 1
fi

START=$(date +%s)
bash part2/scml/scripts/run_dws_ads.sh "${EXTRA[@]}" 2>&1 | tail -45
rc=${PIPESTATUS[0]}
echo
echo "run_dws_ads.sh rc=$rc，耗时 $(( $(date +%s) - START )) 秒"

echo
echo "===== 产出 ====="
hdfs dfs -du -h "$HDFS_ROOT/dws" "$HDFS_ROOT/ads" 2>&1 | head -20
ls -l "$HANDOFF_ADS/ads.db" 2>&1

if [ -f "$HANDOFF_ADS/ads.db" ]; then
  echo
  echo "ADS 关键值（ads_meta）："
  "$VENV/bin/python" - "$HANDOFF_ADS/ads.db" <<'PY' 2>/dev/null || true
import sqlite3, sys
keys = ("runId", "stationCount", "chargerCount", "orderCount",
        "totalRevenueFen", "totalEnergyKwh", "forecastSource", "forecastIsBaseline")
con = sqlite3.connect(sys.argv[1])
meta = dict(con.execute("SELECT key, value FROM ads_meta"))
for k in keys:
    print(f"      {k:22s} {meta.get(k, '—')}")
PY
fi

echo
echo "===== 完成 ====="
echo "  下一步：bash scripts/part2/60-status.sh（体检）与 61/62/63（取证）"
