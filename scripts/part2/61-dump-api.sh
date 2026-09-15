#!/usr/bin/env bash
# =============================================================================
# 61-dump-api.sh —— 抓取全部 /api 响应存档（抽验表的「接口值」那一段）
# =============================================================================
# `part2-metric-checklist.md` 的抽验方法分三段取值：ADS 真值（63）、接口值（本脚本）、
# 大屏显示（62）。本脚本把 27 个契约端点的原始 JSON 全量落盘，后面逐位比对时有据可查，
# 也方便出现争议时回放"当时接口到底返回了什么"。
#
# 用法：
#   bash scripts/part2/61-dump-api.sh
#   PART2_PORT=8080 bash scripts/part2/61-dump-api.sh
#   bash scripts/part2/61-dump-api.sh /tmp/api-20260915     # 指定输出目录
#
# 默认输出到 <repo>/runtime/api-dump/（已 gitignore，不入库）。
# 环境里没有 curl 时自动回退 wget（本组虚拟机上就没有 curl）。
# =============================================================================
set -uo pipefail

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SELF_DIR/../.." && pwd)"
HOST="${PART2_HOST:-127.0.0.1}"
PORT="${PART2_PORT:-5000}"
OUT="${1:-$REPO_ROOT/runtime/api-dump}"
BASE="http://${HOST}:${PORT}"

ENDPOINTS=(
  /api/health
  /api/overview/kpis
  /api/overview/stations
  /api/overview/charger-status
  /api/overview/load-24h
  /api/overview/events
  /api/quality/summary
  /api/user/price-compare
  /api/user/price-distance
  /api/user/idle-ranking
  /api/user/peak-heatmap
  /api/station/coverage
  /api/station/1/utilization
  /api/station/1/mix
  /api/station/1/health
  /api/enterprise/revenue-trend
  /api/enterprise/station-ranking
  /api/enterprise/user-growth
  /api/enterprise/user-rfm
  /api/enterprise/monthly
  /api/gov/coverage
  /api/gov/service-stats
  /api/gov/carbon
  /api/gov/peak-load
  /api/gov/utilization
  /api/forecast/24h
  /api/forecast/metrics
  /api/forecast/recommend
)

FETCH=""
if command -v curl >/dev/null 2>&1; then FETCH=curl
elif command -v wget >/dev/null 2>&1; then FETCH=wget
else echo "[fail] 既没有 curl 也没有 wget"; exit 1
fi

mkdir -p "$OUT"
echo "===== 抓取 API 响应 ====="
echo "  服务端  : $BASE"
echo "  输出    : $OUT"
echo "  下载工具: $FETCH"
echo

FAIL=0
for ep in "${ENDPOINTS[@]}"; do
  name="$(printf '%s' "$ep" | sed 's|^/api/||; s|/|_|g')"
  if [ "$FETCH" = "curl" ]; then
    code=$(curl -s -o "$OUT/$name.json" -w '%{http_code}' --max-time 20 "$BASE$ep")
    [ "$code" != "200" ] && { FAIL=$((FAIL + 1)); }
    printf '  %-34s http=%s  %s bytes\n' "$ep" "$code" "$(wc -c <"$OUT/$name.json" 2>/dev/null || echo 0)"
  else
    if wget -q -O "$OUT/$name.json" --timeout=20 "$BASE$ep"; then code=200; else code=ERR; FAIL=$((FAIL + 1)); fi
    printf '  %-34s %s  %s bytes\n' "$ep" "$code" "$(wc -c <"$OUT/$name.json" 2>/dev/null || echo 0)"
  fi
done

echo
echo "===== 完成：$(( ${#ENDPOINTS[@]} - FAIL ))/${#ENDPOINTS[@]} 个端点返回 200 ====="
echo "  下一步：62-capture-pages.mjs（大屏显示值）与 63-ads-truth.py（ADS 真值）"
[ "$FAIL" -eq 0 ] || exit 1
