#!/usr/bin/env bash
# 第二阶段一键停止（配合 30-start-demo.sh）
#   bash scripts/part2/31-stop-demo.sh          # 只停 Flask（保留 HDFS/YARN，便于连续演示）
#   bash scripts/part2/31-stop-demo.sh --all    # 再按反向顺序停 YARN → HDFS（避免元数据损坏）
set -uo pipefail
HADOOP_DIR="${HADOOP_HOME:-/usr/local/hadoop}"

echo "[1/2] 停止 Flask ..."
if pkill -f "server/app.py" 2>/dev/null; then
  sleep 2
  echo "  已停止"
else
  echo "  未在运行"
fi

if [ "${1:-}" = "--all" ]; then
  echo "[2/2] 停止 YARN → HDFS ..."
  "$HADOOP_DIR/sbin/stop-yarn.sh" >/dev/null 2>&1 && echo "  YARN 已停止" || echo "  YARN 停止失败（可能需 sudo）"
  "$HADOOP_DIR/sbin/stop-dfs.sh" >/dev/null 2>&1 && echo "  HDFS 已停止" || echo "  HDFS 停止失败（可能需 sudo）"
else
  echo "[2/2] 保留 HDFS/YARN（如需一并停止：bash $0 --all）"
fi
