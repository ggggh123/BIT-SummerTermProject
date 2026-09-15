#!/usr/bin/env bash
# start_part2.sh —— 东软充电桩第二阶段一键启动（集成/演示机专用）
# 依据：《02-TL-Hadoop平台与Flask后端设计》§4.3 启动脚本
# 职责：#2 TL。顺序：HDFS/YARN → 数据检查 → Flask（同时托管 web/dist）
#
# 用法：
#   bash scripts/start_part2.sh              # 使用默认路径
#   PART2_ROOT=/home/bit/part2 bash scripts/start_part2.sh
set -uo pipefail

ROOT="${PART2_ROOT:-/home/bit/part2}"
ADS_DB="${ADS_DB:-$ROOT/part2/scml/handoff/ads/ads.db}"
PORT="${PART2_PORT:-5000}"
LOG_DIR="$ROOT/logs"
mkdir -p "$LOG_DIR"

echo "===== 第二阶段一键启动 ====="
echo "根目录 : $ROOT"
echo "ADS 库 : $ADS_DB"
echo "端口   : $PORT"
echo

# ---------- 1) HDFS + YARN ----------
echo "[1/4] 启动 Hadoop（HDFS + YARN）..."
if sudo -u hadoop bash -lc "jps" 2>/dev/null | grep -q NameNode; then
  echo "  HDFS 已在运行，跳过"
else
  sudo -u hadoop bash -lc "start-dfs.sh" >/dev/null 2>&1 && echo "  HDFS 已启动"
fi
if sudo -u hadoop bash -lc "jps" 2>/dev/null | grep -q ResourceManager; then
  echo "  YARN 已在运行，跳过"
else
  sudo -u hadoop bash -lc "start-yarn.sh" >/dev/null 2>&1 && echo "  YARN 已启动"
fi
sleep 3
DAEMONS=$(sudo -u hadoop bash -lc "jps" 2>/dev/null | grep -cE "NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager")
echo "  守护进程: $DAEMONS/5"

# ---------- 2) 数据检查 ----------
echo "[2/4] 检查 ADS 库 ..."
if [ -f "$ADS_DB" ]; then
  echo "  OK: $(du -h "$ADS_DB" | cut -f1)  $ADS_DB"
else
  echo "  !! 未找到 ADS 库：$ADS_DB"
  echo "  !! 请先物化数据："
  echo "     cd $ROOT/part2/scml"
  echo "     bash scripts/run_scml_full.sh"
  echo "     python3 warehouse/jobs/build_local.py --ods handoff/ods --dws handoff/dws --ads handoff/ads"
  exit 1
fi

if [ -d "$ROOT/web/dist" ]; then
  echo "  OK: 前端产物 web/dist 已就绪（$(du -sh "$ROOT/web/dist" | cut -f1)）"
else
  echo "  !! 警告：未找到 web/dist，大屏页面将不可用"
  echo "     请构建：cd $ROOT/web && npm ci && npm run build"
fi

# ---------- 3) 启动 Flask（同时托管大屏） ----------
echo "[3/4] 启动 Flask 服务 ..."
pkill -f "python3 server/app.py" 2>/dev/null && sleep 1
cd "$ROOT" || exit 1
ADS_DB="$ADS_DB" PORT="$PORT" setsid nohup python3 server/app.py > "$LOG_DIR/flask.log" 2>&1 < /dev/null &
sleep 4

if curl -s --max-time 10 "http://127.0.0.1:$PORT/api/health" | grep -q '"status": *"up"'; then
  RUN_ID=$(curl -s --max-time 10 "http://127.0.0.1:$PORT/api/health" | sed -n 's/.*"runId": *"\([^"]*\)".*/\1/p')
  echo "  OK: 服务已就绪（runId=$RUN_ID）"
else
  echo "  !! 启动失败，请查看 $LOG_DIR/flask.log"
  tail -20 "$LOG_DIR/flask.log" 2>/dev/null
  exit 1
fi

# ---------- 4) 汇总 ----------
IP=$(hostname -I | awk '{print $1}')
echo "[4/4] 就绪"
echo "  大屏    : http://$IP:$PORT/"
echo "  API     : http://$IP:$PORT/api/overview/kpis"
echo "  健康检查: http://$IP:$PORT/api/health"
echo "  日志    : $LOG_DIR/flask.log"
echo "===== 启动完成 ====="
