#!/usr/bin/env bash
# 第二阶段一键停止（跨机适配版，配合 30-start-demo.sh）
#   bash scripts/part2/31-stop-demo.sh          # 只停 Flask（保留 HDFS/YARN，便于连续演示）
#   bash scripts/part2/31-stop-demo.sh --all    # 再按反向顺序停 YARN → HDFS（避免元数据损坏）
#
# 跨机兼容：与 30-start-demo.sh 相同的两种部署形态——
#   A) Hadoop 以当前用户运行 → 直接调 stop-*.sh；
#   B) Hadoop 以独立 hadoop 用户运行（集成机）→ 经 sudo -u hadoop + login shell 执行。
# 停止判据同样是"进程是否真的消失"，不依赖脚本退出码。
set -uo pipefail

HADOOP_DIR="${HADOOP_HOME:-/usr/local/hadoop}"
HADOOP_USER="${PART2_HADOOP_USER:-hadoop}"

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

hadoop_stop() {  # $1=stop-dfs.sh|stop-yarn.sh  $2=关联进程名
  local script="$HADOOP_DIR/sbin/$1" proc="$2"
  jps_run | grep -q "$proc" || return 0          # 已不在运行
  if sudo -n true 2>/dev/null && id "$HADOOP_USER" >/dev/null 2>&1; then
    sudo -u "$HADOOP_USER" bash -lc "$script" >/dev/null 2>&1 || true
  else
    "$script" >/dev/null 2>&1 || true
  fi
  for _i in $(seq 1 10); do
    jps_run | grep -q "$proc" || return 0
    sleep 3
  done
  return 1
}

echo "[1/2] 停止 Flask ..."
if pkill -f "server/app.py" 2>/dev/null; then
  sleep 2
  echo "  已停止"
else
  echo "  未在运行"
fi

if [ "${1:-}" = "--all" ]; then
  echo "[2/2] 停止 YARN → HDFS ..."
  hadoop_stop stop-yarn.sh ResourceManager && echo "  YARN 已停止" \
    || echo "  YARN 停止失败（手工：sudo -u $HADOOP_USER bash -lc '$HADOOP_DIR/sbin/stop-yarn.sh'）"
  hadoop_stop stop-dfs.sh NameNode && echo "  HDFS 已停止" \
    || echo "  HDFS 停止失败（手工：sudo -u $HADOOP_USER bash -lc '$HADOOP_DIR/sbin/stop-dfs.sh'）"
  echo
  echo "剩余 Hadoop 进程："
  REMAIN=$(jps_run | grep -E "NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager" || true)
  [ -n "$REMAIN" ] && echo "$REMAIN" || echo "  （已全部停止）"
else
  echo "[2/2] 保留 HDFS/YARN（如需一并停止：bash $0 --all）"
fi
