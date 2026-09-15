#!/usr/bin/env bash
# 第二阶段一键启动：HDFS/YARN（可显式跳过）→ ADS 检查 → Flask + web/dist。
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
default_root="$(cd -- "$script_dir/../.." && pwd)"
ROOT="${PART2_ROOT:-$default_root}"
PORT="${PART2_PORT:-5000}"
HOST="${PART2_HOST:-0.0.0.0}"
LOG_DIR="${PART2_LOG_DIR:-$ROOT/logs}"
PID_FILE="$LOG_DIR/flask.pid"
HADOOP_USER="${PART2_HADOOP_USER:-hadoop}"
SKIP_HADOOP="${PART2_SKIP_HADOOP:-0}"
REQUIRE_HADOOP="${PART2_REQUIRE_HADOOP:-1}"

case "$PORT" in
  ''|*[!0-9]*) echo "错误：PART2_PORT 必须是 1～65535 的整数" >&2; exit 2 ;;
esac
if (( PORT < 1 || PORT > 65535 )); then
  echo "错误：PART2_PORT 必须是 1～65535 的整数" >&2
  exit 2
fi
if [[ ! -f "$ROOT/server/app.py" ]]; then
  echo "错误：项目根目录无效，缺少 $ROOT/server/app.py" >&2
  echo "请在仓库内运行本脚本，或设置 PART2_ROOT。" >&2
  exit 2
fi

if [[ -n "${ADS_DB:-}" ]]; then
  ads_db="$ADS_DB"
elif [[ -f "$ROOT/handoff/ads/ads.db" ]]; then
  ads_db="$ROOT/handoff/ads/ads.db"
elif [[ -f "$ROOT/part2/scml/handoff/ads/ads.db" ]]; then
  ads_db="$ROOT/part2/scml/handoff/ads/ads.db"
else
  ads_db="$ROOT/handoff/ads/ads.db"
fi
ADS_DB="$(readlink -m -- "$ads_db")"

python_has_server_deps() {
  "$1" -c 'import flask, flask_cors' >/dev/null 2>&1
}

pick_python() {
  if [[ -n "${PART2_PYTHON:-}" ]]; then
    if ! command -v "$PART2_PYTHON" >/dev/null 2>&1 && [[ ! -x "$PART2_PYTHON" ]]; then
      echo "错误：PART2_PYTHON 不可执行：$PART2_PYTHON" >&2
      return 1
    fi
    if ! python_has_server_deps "$PART2_PYTHON"; then
      echo "错误：PART2_PYTHON 缺少 Flask/flask-cors：$PART2_PYTHON" >&2
      return 1
    fi
    printf '%s\n' "$PART2_PYTHON"
    return
  fi

  local user_home candidate
  user_home="$(getent passwd "$(id -un)" 2>/dev/null | cut -d: -f6)"
  for candidate in \
    "$ROOT/.venv/bin/python" \
    "$ROOT/venv/bin/python" \
    "${user_home:+$user_home/venvs/part2/bin/python}" \
    /usr/local/ev-part2/python/bin/python \
    python3; do
    [[ -n "$candidate" ]] || continue
    if (command -v "$candidate" >/dev/null 2>&1 || [[ -x "$candidate" ]]) \
        && python_has_server_deps "$candidate"; then
      printf '%s\n' "$candidate"
      return
    fi
  done
  echo "错误：未找到同时包含 Flask 与 flask-cors 的 Python。" >&2
  echo "可设置 PART2_PYTHON=/绝对路径/python；不要让脚本猜测或临时安装依赖。" >&2
  return 1
}

PYTHON="$(pick_python)"

tcp_open() {
  "$PYTHON" - "$1" "$2" <<'PY' >/dev/null 2>&1
import socket
import sys

with socket.socket() as sock:
    sock.settimeout(0.8)
    raise SystemExit(sock.connect_ex((sys.argv[1], int(sys.argv[2]))) != 0)
PY
}

hadoop_ports_ready() {
  tcp_open 127.0.0.1 8020 && tcp_open 127.0.0.1 8032
}

start_hadoop() {
  if [[ "$SKIP_HADOOP" == 1 ]]; then
    echo "  已按 PART2_SKIP_HADOOP=1 跳过；演示服务只读已物化 ADS。"
    return 0
  fi
  if hadoop_ports_ready; then
    echo "  HDFS RPC 8020 与 YARN RM 8032 已监听，跳过重复启动。"
    return 0
  fi
  if command -v ev-part2 >/dev/null 2>&1; then
    ev-part2 start
  elif command -v start-dfs.sh >/dev/null 2>&1 \
      && command -v start-yarn.sh >/dev/null 2>&1; then
    start-dfs.sh
    start-yarn.sh
  elif id "$HADOOP_USER" >/dev/null 2>&1 \
      && command -v sudo >/dev/null 2>&1 \
      && sudo -n -u "$HADOOP_USER" true >/dev/null 2>&1; then
    sudo -n -u "$HADOOP_USER" bash -lc \
      'command -v start-dfs.sh >/dev/null && command -v start-yarn.sh >/dev/null && start-dfs.sh && start-yarn.sh'
  else
    echo "  无法非交互管理 Hadoop：未发现 ev-part2、当前用户启动命令或可免密切换的 $HADOOP_USER。" >&2
  fi
  local attempt
  for attempt in {1..12}; do
    hadoop_ports_ready && return 0
    sleep 1
  done
  if [[ "$REQUIRE_HADOOP" == 1 ]]; then
    echo "错误：HDFS/YARN 未就绪。若只展示已物化 ADS，请显式设置 PART2_SKIP_HADOOP=1。" >&2
    return 1
  fi
  echo "  警告：HDFS/YARN 未就绪；按 PART2_REQUIRE_HADOOP=0 继续启动只读展示服务。" >&2
}

health_payload() {
  "$PYTHON" - "$PORT" <<'PY'
import json
import sys
import urllib.request

with urllib.request.urlopen(f"http://127.0.0.1:{sys.argv[1]}/api/health", timeout=1.5) as response:
    payload = json.load(response)
if payload.get("code") != 0 or payload.get("data", {}).get("status") != "up":
    raise SystemExit(1)
print(json.dumps(payload, ensure_ascii=False))
PY
}

mkdir -p -- "$LOG_DIR"
echo "===== 第二阶段一键启动 ====="
echo "根目录 : $ROOT"
echo "ADS 库 : $ADS_DB"
echo "Python : $PYTHON"
echo "监听   : $HOST:$PORT"
echo

echo "[1/4] 检查并启动 Hadoop（HDFS + YARN）..."
start_hadoop

echo "[2/4] 检查 ADS 与前端产物..."
if [[ ! -f "$ADS_DB" ]]; then
  echo "错误：未找到 ADS 库：$ADS_DB" >&2
  echo "请先物化数据，或设置 ADS_DB=/绝对路径/ads.db。" >&2
  exit 1
fi
echo "  ADS：$(du -h -- "$ADS_DB" | cut -f1)"
if [[ -f "$ROOT/web/dist/index.html" ]]; then
  echo "  前端：$ROOT/web/dist"
else
  echo "  警告：未找到 web/dist/index.html；API 可用，但大屏只会显示构建提示。" >&2
  echo "  构建命令：cd '$ROOT/web' && npm ci && npm run build:live" >&2
fi

echo "[3/4] 启动 Flask 服务..."
if payload="$(health_payload 2>/dev/null)"; then
  current_db="$($PYTHON -c 'import json,sys; print(json.load(sys.stdin).get("data",{}).get("dbPath", ""))' <<<"$payload")"
  current_db="$(readlink -m -- "$current_db")"
  if [[ "$current_db" != "$ADS_DB" ]]; then
    echo "错误：端口 $PORT 上已有健康服务，但它读取的是另一份 ADS。" >&2
    echo "  当前：$current_db" >&2
    echo "  期望：$ADS_DB" >&2
    echo "请先运行 part2/scripts/stop_part2.sh，或改用其他 PART2_PORT。" >&2
    exit 1
  fi
  echo "  已有健康服务读取同一份 ADS，保持原进程，不重复启动。"
else
  if tcp_open 127.0.0.1 "$PORT"; then
    echo "错误：端口 $PORT 已被非本项目健康服务占用。" >&2
    exit 1
  fi
  rm -f -- "$PID_FILE"
  ADS_DB="$ADS_DB" PORT="$PORT" HOST="$HOST" \
    setsid nohup "$PYTHON" "$ROOT/server/app.py" \
    >"$LOG_DIR/flask.log" 2>&1 < /dev/null &
  flask_pid=$!
  printf '%s\n' "$flask_pid" >"$PID_FILE"
  payload=""
  for attempt in {1..30}; do
    if payload="$(health_payload 2>/dev/null)"; then
      break
    fi
    if ! kill -0 "$flask_pid" 2>/dev/null; then
      break
    fi
    sleep 0.5
  done
  if [[ -z "$payload" ]]; then
    kill "$flask_pid" 2>/dev/null || true
    rm -f -- "$PID_FILE"
    echo "错误：Flask 未通过健康检查，日志如下：" >&2
    tail -30 "$LOG_DIR/flask.log" 2>/dev/null >&2 || true
    exit 1
  fi
  echo "  已启动 PID=$flask_pid"
fi

run_id="$($PYTHON -c 'import json,sys; print(json.load(sys.stdin).get("data",{}).get("runId", ""))' <<<"$payload")"
lan_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
lan_ip="${lan_ip:-127.0.0.1}"
echo "[4/4] 就绪"
echo "  数据批次：${run_id:-未记录}"
echo "  本机大屏：http://127.0.0.1:$PORT/"
echo "  局域网  ：http://$lan_ip:$PORT/"
echo "  健康检查：http://127.0.0.1:$PORT/api/health"
echo "  日志    ：$LOG_DIR/flask.log"
echo "===== 启动完成 ====="
