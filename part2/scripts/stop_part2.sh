#!/usr/bin/env bash
# stop_part2.sh —— 东软充电桩第二阶段一键停止（集成/演示机专用）
# 依据：《02-TL-Hadoop平台与Flask后端设计》§4.3
# 顺序：Flask → YARN → HDFS（与启动相反，避免元数据损坏）
set -uo pipefail

ROOT="${PART2_ROOT:-/home/bit/part2}"

echo "===== 第二阶段一键停止 ====="

# 1) Flask
echo "[1/3] 停止 Flask ..."
if pkill -f "python3 server/app.py" 2>/dev/null; then
  echo "  OK: 已停止"
else
  echo "  -: 未在运行"
fi
sleep 1

# 2) YARN
echo "[2/3] 停止 YARN ..."
if sudo -u hadoop bash -lc "jps" 2>/dev/null | grep -q ResourceManager; then
  sudo -u hadoop bash -lc "stop-yarn.sh" >/dev/null 2>&1 && echo "  OK: 已停止"
else
  echo "  -: 未在运行"
fi

# 3) HDFS
echo "[3/3] 停止 HDFS ..."
if sudo -u hadoop bash -lc "jps" 2>/dev/null | grep -q NameNode; then
  sudo -u hadoop bash -lc "stop-dfs.sh" >/dev/null 2>&1 && echo "  OK: 已停止"
else
  echo "  -: 未在运行"
fi

sleep 2
echo
echo "剩余 Java 进程："
sudo -u hadoop bash -lc "jps" 2>/dev/null || echo "  （无）"
echo "===== 停止完成 ====="
