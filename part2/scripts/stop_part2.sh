#!/usr/bin/env bash
# 第二阶段一键停止：本脚本启动的 Flask → YARN → HDFS。
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
default_root="$(cd -- "$script_dir/../.." && pwd)"
ROOT="${PART2_ROOT:-$default_root}"
LOG_DIR="${PART2_LOG_DIR:-$ROOT/logs}"
PID_FILE="$LOG_DIR/flask.pid"
HADOOP_USER="${PART2_HADOOP_USER:-hadoop}"
SKIP_HADOOP="${PART2_SKIP_HADOOP:-0}"

stop_flask() {
  if [[ ! -f "$PID_FILE" ]]; then
    echo "  未找到 PID 文件；不会用模糊 pkill 误伤其他 Python 服务。"
    return
  fi
  local pid cmdline attempt
  pid="$(tr -d '[:space:]' <"$PID_FILE")"
  if [[ ! "$pid" =~ ^[1-9][0-9]*$ ]] || [[ ! -r "/proc/$pid/cmdline" ]]; then
    echo "  PID 文件已过期，清理记录。"
    rm -f -- "$PID_FILE"
    return
  fi
  cmdline="$(tr '\0' ' ' <"/proc/$pid/cmdline")"
  if [[ "$cmdline" != *"$ROOT/server/app.py"* ]]; then
    echo "错误：PID $pid 不是本项目 Flask，拒绝终止：$cmdline" >&2
    return 1
  fi
  kill "$pid"
  for attempt in {1..20}; do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.25
  done
  if kill -0 "$pid" 2>/dev/null; then
    echo "错误：Flask PID $pid 未在宽限时间内退出；保留进程供人工检查。" >&2
    return 1
  fi
  rm -f -- "$PID_FILE"
  echo "  已停止 PID=$pid"
}

stop_hadoop() {
  if [[ "$SKIP_HADOOP" == 1 ]]; then
    echo "  已按 PART2_SKIP_HADOOP=1 跳过。"
    return
  fi
  if command -v ev-part2 >/dev/null 2>&1; then
    ev-part2 stop
  elif command -v stop-yarn.sh >/dev/null 2>&1 \
      && command -v stop-dfs.sh >/dev/null 2>&1; then
    stop-yarn.sh
    stop-dfs.sh
  elif id "$HADOOP_USER" >/dev/null 2>&1 \
      && command -v sudo >/dev/null 2>&1 \
      && sudo -n -u "$HADOOP_USER" true >/dev/null 2>&1; then
    sudo -n -u "$HADOOP_USER" bash -lc \
      'command -v stop-yarn.sh >/dev/null && command -v stop-dfs.sh >/dev/null && stop-yarn.sh && stop-dfs.sh'
  else
    echo "  无法非交互管理 Hadoop；未执行停止。" >&2
  fi
}

echo "===== 第二阶段一键停止 ====="
echo "[1/2] 停止 Flask..."
stop_flask
echo "[2/2] 停止 YARN/HDFS..."
stop_hadoop
echo "===== 停止完成 ====="
