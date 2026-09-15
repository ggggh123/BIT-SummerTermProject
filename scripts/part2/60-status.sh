#!/usr/bin/env bash
# =============================================================================
# 60-status.sh —— 一键体检：Hadoop 守护进程 / 演示服务 / HDFS 分层 / ADS 交接库
# =============================================================================
# 只读，不改任何状态。答辩前、开演示前、怀疑"哪一层断了"时跑这一个就够。
#
# 用法：
#   bash scripts/part2/60-status.sh
#   PART2_PORT=8080 bash scripts/part2/60-status.sh
#
# 输出四段：① 集群 ② 演示服务（HTTP） ③ HDFS 分层 ④ 本地 ADS 交接库
# 退出码：0 = 四段都正常；1 = 至少一段异常（便于脚本化判断）。
# =============================================================================
set -uo pipefail

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SELF_DIR/../.." && pwd)"
VENV="${PART2_VENV:-$HOME/venvs/part2}"
SHIM_DIR="${EV_SHIM_DIR:-$HOME/ev-shim}"
HDFS_ROOT="${HDFS_ROOT:-/ev-charging}"
HOST="${PART2_HOST:-127.0.0.1}"
PORT="${PART2_PORT:-5000}"
ADS_DB="${ADS_DB:-$REPO_ROOT/handoff/ads/ads.db}"
BAD=0

export PATH="$SHIM_DIR:$PATH"

hr() { printf '%s\n' "------------------------------------------------------------"; }

echo "===== 第二阶段状态体检　$(date -Is) ====="

# ---------------- ① 集群 ----------------
hr
echo "① Hadoop / YARN"
if command -v jps >/dev/null 2>&1; then
  n=$(jps | grep -c -E 'NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager')
  jps | grep -E 'NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager' | sed 's/^/    /'
  if [ "$n" -eq 5 ]; then echo "    → 5 个守护进程齐"; else echo "    → 只有 $n 个（应为 5）"; BAD=1; fi
else
  # 非 login shell 的 PATH 往往没有 JDK，jps 会"找不到命令"——这是已知坑，不是集群没起
  echo "    [warn] PATH 里没有 jps（非 login shell 常见）。用：bash -lc jps"
fi
if hdfs dfs -test -d "$HDFS_ROOT/ods" 2>/dev/null; then
  echo "    → HDFS 可读（fs.defaultFS=$(hdfs getconf -confKey fs.defaultFS 2>/dev/null)）"
else
  echo "    [fail] HDFS 不可读或 $HDFS_ROOT/ods 不存在"; BAD=1
fi

# ---------------- ② 演示服务 ----------------
hr
echo "② 演示服务（Flask 托管 dist + /api）"
echo -n "    监听 ${HOST}:${PORT} 的进程数: "
pgrep -fc "server/app.py" 2>/dev/null || echo 0
if command -v ss >/dev/null 2>&1; then
  echo -n "    端口 ${PORT} LISTEN: "
  ss -ltn 2>/dev/null | grep -c ":${PORT}\b" || true
fi
HEALTH=$(python3 - "$HOST" "$PORT" <<'PY' 2>/dev/null
import json, sys, urllib.request
host, port = sys.argv[1], sys.argv[2]
try:
    with urllib.request.urlopen(f"http://{host}:{port}/api/health", timeout=8) as r:
        print(r.read().decode("utf-8"))
except Exception as exc:
    print(f"ERROR {exc}")
PY
)
if [ "${HEALTH#ERROR}" = "$HEALTH" ] && [ -n "$HEALTH" ]; then
  echo "    /api/health → $(printf '%s' "$HEALTH" | cut -c1-160)"
  if printf '%s' "$HEALTH" | grep -q '"dbPath"'; then
    echo "    dbPath      → $(printf '%s' "$HEALTH" | sed -n 's/.*"dbPath":"\([^"]*\)".*/\1/p')"
  fi
  KPIS=$(python3 - "$HOST" "$PORT" <<'PY' 2>/dev/null
import json, sys, urllib.request
host, port = sys.argv[1], sys.argv[2]
try:
    with urllib.request.urlopen(f"http://{host}:{port}/api/overview/kpis", timeout=8) as r:
        d = json.loads(r.read().decode("utf-8")).get("data", {})
    print(f"订单 {d.get('totalOrders')} | 营收 {d.get('totalRevenueFen')} 分 | 电量 {d.get('totalEnergyKwh')} kWh "
          f"| 桩 {d.get('chargerCount')} | 在线率 {d.get('onlineRate')}% | 空闲 {d.get('idleCount')}")
except Exception as exc:
    print(f"ERROR {exc}")
PY
)
  echo "    KPI         → $KPIS"
  case "$KPIS" in ERROR*) BAD=1 ;; esac
else
  echo "    [fail] /api/health 不可用：$HEALTH"; BAD=1
fi

# ---------------- ③ HDFS 分层 ----------------
hr
echo "③ HDFS 分层（$HDFS_ROOT）"
for d in ods dwd dws ads quality; do
  if hdfs dfs -test -d "$HDFS_ROOT/$d" 2>/dev/null; then
    size=$(hdfs dfs -du -h -s "$HDFS_ROOT/$d" 2>/dev/null | awk '{print $1}')
    n=$(hdfs dfs -ls "$HDFS_ROOT/$d" 2>/dev/null | awk 'NR>1' | wc -l)
    printf '    %-8s %-10s %s 个条目\n' "$d" "$size" "$n"
  else
    printf '    %-8s [缺失]\n' "$d"; BAD=1
  fi
done

# ---------------- ④ 本地 ADS 交接库 ----------------
hr
echo "④ 本地 ADS 交接库"
if [ -f "$ADS_DB" ]; then
  ls -l "$ADS_DB" | sed 's/^/    /'
  "$VENV/bin/python" - "$ADS_DB" <<'PY' 2>/dev/null
import sqlite3, sys
con = sqlite3.connect(sys.argv[1])
keys = ("runId", "generatedAt", "sourceRunId", "stationCount", "chargerCount",
        "orderCount", "totalRevenueFen", "totalEnergyKwh",
        "forecastSource", "forecastIsBaseline", "forecastRunId")
meta = dict(con.execute("SELECT key, value FROM ads_meta"))
for k in keys:
    v = meta.get(k)
    if v is not None:
        print(f"    {k:20s} {v}")
tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
print(f"    表数 {len(tables)}: {' '.join(tables)}")
for t in ("ads_forecast_24h", "ads_forecast_metric"):
    try:
        print(f"    {t}: {con.execute(f'SELECT COUNT(*) FROM {t}').fetchone()[0]} 行")
    except sqlite3.Error as exc:
        print(f"    {t}: 读取失败 {exc}")
PY
else
  echo "    [fail] 找不到 $ADS_DB"; BAD=1
fi

hr
if [ "$BAD" = "0" ]; then
  echo "结论：四段均正常。"
else
  echo "结论：有异常项（见上面的 [fail]）。"
fi
exit "$BAD"
