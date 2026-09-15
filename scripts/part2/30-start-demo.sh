#!/usr/bin/env bash
# 第二阶段一键启动（跨机适配版）：HDFS/YARN → 数据预检 → Flask（同进程托管 web/dist 与 /api/*）
#
# 本脚本由两版合并而来（2026-09-15，#2 TL 收口）：
#   * 基底：scripts/part2/30-start-demo.sh（可移植版）——仓库根按脚本位置推导、Python 自动挑
#     "能 import flask" 的解释器、curl → wget → urllib 三级回退、ADS 库多候选路径探测；
#   * 并入：part2/scripts/start_part2.sh（#2 集成机专用版，已删除）——追加"跨机 Hadoop 启停"：
#     集成机上 Hadoop 以独立 hadoop 用户运行，普通用户的 jps 看不到守护进程；
#     `sudo -u hadoop jps` 也不行（非 login shell 的 PATH 不含 JDK），必须 `bash -lc`。
#     另外**不能信任启动脚本的退出码**：以当前用户身份跑 start-dfs.sh 会返回 0 却不启动任何
#     进程（2026-09-15 实测），因此改成以"守护进程是否真的出现/消失"作为成功判据。
#   两版原有的 ADS 库预检、物化命令提示、web/dist 预检、访问地址汇总全部保留。
#
# 用法（普通用户即可，不强制 sudo；需要提权的场景自动降级为提示）：
#   bash scripts/part2/30-start-demo.sh
#   PART2_PORT=8080 bash scripts/part2/30-start-demo.sh
#   PART2_PYTHON=~/venvs/part2/bin/python bash scripts/part2/30-start-demo.sh
#   ADS_DB=/path/to/ads.db bash scripts/part2/30-start-demo.sh
#   PART2_HADOOP_USER=hadoop bash scripts/part2/30-start-demo.sh    # 指定 Hadoop 运行用户
set -uo pipefail

SELF=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT="${PART2_ROOT:-$(cd "$SELF/../.." && pwd)}"
PORT="${PART2_PORT:-5000}"
LOG_DIR="${PART2_LOG_DIR:-$ROOT/logs}"
HADOOP_DIR="${HADOOP_HOME:-/usr/local/hadoop}"
HADOOP_USER="${PART2_HADOOP_USER:-hadoop}"
mkdir -p "$LOG_DIR"

# ============ 跨机兼容层：jps 与 Hadoop 启停 ============
# 组内存在两种部署形态：
#   A) Hadoop 以当前登录用户运行（VM 上直接跑 start-dfs.sh）→ jps 直接可用；
#   B) Hadoop 以独立 hadoop 用户运行（集成机 niyujun01）→ 必须 sudo -u hadoop + login shell。
# 模式在函数外一次性判定：函数体常在 $( ) 子 shell 中执行，内部赋值不会回传父 shell。
if command -v jps >/dev/null 2>&1; then
  JPS_MODE="local"
elif sudo -n -u "$HADOOP_USER" true 2>/dev/null; then
  JPS_MODE="sudo"
else
  JPS_MODE="none"
fi

jps_run() {
  case "$JPS_MODE" in
    local) jps 2>/dev/null ;;
    sudo)  sudo -u "$HADOOP_USER" bash -lc "jps" 2>/dev/null ;;
    *)     jps 2>/dev/null || true ;;
  esac
}

_hadoop_exec() {  # 以合适身份执行 sbin 脚本
  local script="$HADOOP_DIR/sbin/$1"
  [ -x "$script" ] || return 1
  if sudo -n true 2>/dev/null && id "$HADOOP_USER" >/dev/null 2>&1; then
    sudo -u "$HADOOP_USER" bash -lc "$script" >/dev/null 2>&1 || true
  else
    "$script" >/dev/null 2>&1 || true
  fi
}

hadoop_start() {  # $1=start-dfs.sh|start-yarn.sh  $2=期望出现的进程名
  local proc="$2"
  jps_run | grep -q "$proc" && return 0          # 已在运行
  _hadoop_exec "$1"
  for _i in $(seq 1 10); do
    jps_run | grep -q "$proc" && return 0
    sleep 3
  done
  return 1
}

echo "===== 第二阶段一键启动（跨机适配版）====="
echo "  仓库根 : $ROOT"
echo "  端口   : $PORT"
echo "  日志   : $LOG_DIR/flask.log"
echo

# ---------- 1) 选 Python：必须能 import flask ----------
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

# ---------- 2) HDFS + YARN ----------
echo "[2/5] 检查 HDFS/YARN 守护进程 ..."
if hadoop_start start-dfs.sh NameNode; then
  echo "  HDFS 就绪"
else
  echo "  !! HDFS 未能就绪（手工：sudo -u $HADOOP_USER bash -lc '$HADOOP_DIR/sbin/start-dfs.sh'）"
fi
if hadoop_start start-yarn.sh ResourceManager; then
  echo "  YARN 就绪"
else
  echo "  !! YARN 未能就绪（手工：sudo -u $HADOOP_USER bash -lc '$HADOOP_DIR/sbin/start-yarn.sh'）"
fi
DAEMONS=$(jps_run | grep -cE "NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager")
echo "  守护进程: $DAEMONS/5（jps 模式: $JPS_MODE）"
[ "$DAEMONS" -lt 5 ] && echo "  !! 守护进程不足 5 个，HDFS/YARN 可能尚未完全就绪"

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
  echo "     物化方式：cd part2/scml && bash scripts/run_scml_full.sh \\"
  echo "               && python3 warehouse/jobs/build_local.py --ods handoff/ods --dws handoff/dws --ads handoff/ads"
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

fetch() {  # curl 缺失时回退 wget，再回退 Python urllib
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
