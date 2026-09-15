#!/usr/bin/env bash
# 第二阶段一键启动（可移植版）：HDFS/YARN → 数据检查 → Flask（同进程托管 web/dist 与 /api/*）
#
# 与 part2/scripts/start_part2.sh（#2 版，面向集成机 `niyujun01`）的分工：
#   * #2 版把集成机的环境写死（ROOT=/home/bit/part2、sudo -u hadoop、系统 python3、依赖 curl），换机器即失效；
#   * 本脚本把四件事全部自适应：仓库根按脚本位置推导、Python 自动挑"能 import flask"的解释器、
#     健康检查 curl 缺失时回退 wget、HDFS/YARN 只能在无需 sudo 的场景启动（否则只提示不报错）。
#   两台机器都可用的前提下，答辩前建议统一到本脚本，或按 docs/test/evidence/part2-2026-09-15/vm-integration-run.md §4 把 #2 版参数化。
#
# 用法（普通用户即可）：
#   bash scripts/part2/30-start-demo.sh                      # 默认端口 5000
#   PART2_PORT=8080 bash scripts/part2/30-start-demo.sh
#   PART2_PYTHON=~/venvs/part2/bin/python bash scripts/part2/30-start-demo.sh
#   ADS_DB=/path/to/ads.db bash scripts/part2/30-start-demo.sh
set -uo pipefail

SELF=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT="${PART2_ROOT:-$(cd "$SELF/../.." && pwd)}"
PORT="${PART2_PORT:-5000}"
LOG_DIR="${PART2_LOG_DIR:-$ROOT/logs}"
HADOOP_DIR="${HADOOP_HOME:-/usr/local/hadoop}"
mkdir -p "$LOG_DIR"

echo "===== 第二阶段一键启动（可移植版） ====="
echo "  仓库根 : $ROOT"
echo "  端口   : $PORT"
echo "  日志   : $LOG_DIR/flask.log"
echo

# ---------- 1) 选 Python：必须能 import flask（本机 flask 在 venv，系统 python3 没有） ----------
echo "[1/5] 选择 Python 解释器 ..."
PY=""
for c in "${PART2_PYTHON:-}" "$HOME/venvs/part2/bin/python" "$HOME/venv-py311/bin/python" "$(command -v python3 || true)"; do
  [ -n "$c" ] && [ -x "$c" ] || continue
  if "$c" -c 'import flask' >/dev/null 2>&1; then PY="$c"; break; fi
done
if [ -z "$PY" ]; then
  echo "  !! 找不到已安装 flask 的 Python 解释器"
  echo "     本机可用：PART2_PYTHON=~/venvs/part2/bin/python bash $0"
  exit 1
fi
echo "  OK: $PY ($("$PY" --version 2>&1))"

# ---------- 2) HDFS + YARN（仅在没有守护进程时尝试启动；需要 sudo 的机器会提示而不报错） ----------
echo "[2/5] 检查 Hadoop 守护进程 ..."
JP=$(jps 2>/dev/null || true)
if command -v jps >/dev/null 2>&1; then
  if echo "$JP" | grep -q NameNode; then echo "  HDFS 已在运行"; else
    if "$HADOOP_DIR/sbin/start-dfs.sh" >/dev/null 2>&1; then echo "  HDFS 已启动"; else
      echo "  !! HDFS 未运行且启动失败（需 sudo 的机器请先手工启动：start-dfs.sh）"; fi
  fi
  if echo "$JP" | grep -q ResourceManager; then echo "  YARN 已在运行"; else
    if "$HADOOP_DIR/sbin/start-yarn.sh" >/dev/null 2>&1; then echo "  YARN 已启动"; else
      echo "  !! YARN 未运行且启动失败（需 sudo 的机器请先手工启动：start-yarn.sh）"; fi
  fi
  sleep 3
  DAEMONS=$(jps 2>/dev/null | grep -cE "NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager")
  echo "  守护进程: $DAEMONS/5"
else
  echo "  !! 找不到 jps（请用登录 shell，或确认 PATH 含 \$JAVA_HOME/bin）"
fi

# ---------- 3) ADS 库：多候选路径探测 ----------
echo "[3/5] 检查 ADS 库 ..."
if [ -z "${ADS_DB:-}" ]; then
  for c in "$ROOT/handoff/ads/ads.db" "$ROOT/part2/scml/handoff/ads/ads.db"; do
    [ -f "$c" ] && ADS_DB="$c" && break
  done
fi
if [ -n "${ADS_DB:-}" ] && [ -f "$ADS_DB" ]; then
  echo "  OK: $(du -h "$ADS_DB" | cut -f1)  $ADS_DB"
else
  echo "  !! 未找到 ADS 库（候选：$ROOT/handoff/ads/ads.db、$ROOT/part2/scml/handoff/ads/ads.db）"
  echo "     物化方式：python part2/scml/warehouse/jobs/build_local.py --ods handoff/ods --dws handoff/dws --ads handoff/ads"
  exit 1
fi

# ---------- 4) 前端产物 ----------
echo "[4/5] 检查前端产物 ..."
if [ -d "$ROOT/web/dist" ]; then
  echo "  OK: web/dist（$(du -sh "$ROOT/web/dist" | cut -f1)）"
else
  echo "  !! 未找到 web/dist：先执行 cd web && npm ci && npm run build（离线机器需预先打包 node_modules）"
fi

# ---------- 5) 启动 Flask ----------
echo "[5/5] 启动 Flask ..."
pkill -f "server/app.py" 2>/dev/null && sleep 1
cd "$ROOT" || exit 1
ADS_DB="$ADS_DB" PORT="$PORT" setsid nohup "$PY" server/app.py > "$LOG_DIR/flask.log" 2>&1 < /dev/null &
sleep 4

fetch() {  # curl 缺失时回退 wget（Ubuntu 25.04 最小安装没有 curl）
  if command -v curl >/dev/null 2>&1; then curl -s --max-time 10 "$1"
  elif command -v wget >/dev/null 2>&1; then wget -qO- --timeout=10 "$1"
  else "$PY" -c "import urllib.request,sys;sys.stdout.write(urllib.request.urlopen('$1',timeout=10).read().decode())"; fi
}

HEALTH=$(fetch "http://127.0.0.1:$PORT/api/health" || true)
if echo "$HEALTH" | grep -q '"status": *"up"'; then
  RUN_ID=$(echo "$HEALTH" | sed -n 's/.*"runId": *"\([^"]*\)".*/\1/p')
  IP=$(hostname -I 2>/dev/null | awk '{print $1}')
  echo "  OK: 服务已就绪（runId=$RUN_ID）"
  echo
  echo "===== 启动完成 ====="
  echo "  大屏    : http://${IP:-<本机IP>}:$PORT/"
  echo "  API     : http://${IP:-<本机IP>}:$PORT/api/overview/kpis"
  echo "  健康检查: http://${IP:-<本机IP>}:$PORT/api/health"
  echo "  停止    : bash scripts/part2/31-stop-demo.sh"
  echo "  如实声明: 单机伪分布式 Hadoop + 项目生成的模拟数据；预测为 seasonal-naive 基线（isBaseline=1）"
else
  echo "  !! 启动失败，flask.log 末尾："
  tail -20 "$LOG_DIR/flask.log" 2>/dev/null
  exit 1
fi
