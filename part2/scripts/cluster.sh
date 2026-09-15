#!/usr/bin/env bash
# 第二阶段 HDFS/YARN 启停。**双形态自适应**：
#
#   system 形态：Hadoop 为系统级安装（存在 <HADOOP_HOME>/sbin/start-dfs.sh），
#                守护进程可能由独立用户（如 hadoop）启动 → 用 start-dfs.sh/start-yarn.sh，必要时 sudo -n -u
#   local  形态：使用 ev-part2 专用目录 + `hdfs --daemon start` 逐进程启动（旧基线机器沿用，逻辑未变）
#
# 形态可用 EV_PART2_CLUSTER_MODE=system|local 强制指定（默认 auto 探测）。
# 判活一律以「守护进程真实数量 + 端口监听」为准，不信任启停脚本的退出码
# （已知坑：以无权限用户运行 start-dfs.sh 会返回 0 却不启动任何进程）。
set -euo pipefail
if [[ $EUID -eq 0 ]]; then
  echo '请用普通用户启动／停止第二阶段服务，不要使用 sudo。' >&2
  echo '若 Hadoop 属于其他用户，请设置 EV_PART2_HADOOP_USER=<user>，脚本会用 sudo -n -u 代为执行。' >&2
  exit 1
fi
ev_p2_toolchain="${EV_PART2_TOOLCHAIN:-/usr/local/ev-part2}"
source "$ev_p2_toolchain/env.sh"

# ── 形态判定 ──────────────────────────────────────────────────────
EV_PART2_CLUSTER_MODE="${EV_PART2_CLUSTER_MODE:-auto}"
if [[ "$EV_PART2_CLUSTER_MODE" == "auto" ]]; then
  if [[ -x "$HADOOP_HOME/sbin/start-dfs.sh" ]]; then
    EV_PART2_CLUSTER_MODE="system"
  else
    EV_PART2_CLUSTER_MODE="local"
  fi
fi

# ── 执行身份 ──────────────────────────────────────────────────────
# 本机 Hadoop 由独立用户（如 hadoop）运行时，用 sudo -n -u 代执行；免密 sudo 不可用则提示。
EV_PART2_HADOOP_USER="${EV_PART2_HADOOP_USER:-$(id -un)}"
as_hadoop() {
  if [[ "$EV_PART2_HADOOP_USER" == "$(id -un)" ]]; then
    bash -lc "$1"
  elif command -v sudo >/dev/null 2>&1 && sudo -n -u "$EV_PART2_HADOOP_USER" true >/dev/null 2>&1; then
    sudo -n -u "$EV_PART2_HADOOP_USER" bash -lc "$1"
  else
    echo "无法以 $EV_PART2_HADOOP_USER 身份执行（需要免密 sudo）：$1" >&2
    return 1
  fi
}

alive_count() {
  as_hadoop 'jps' 2>/dev/null | grep -cE 'NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager' || true
}

wait_alive() {  # $1 = 期望数量
  local want="$1" i
  for i in $(seq 1 45); do
    [[ "$(alive_count)" -ge "$want" ]] && return 0
    sleep 1
  done
  return 1
}

wait_dead() {
  local i
  for i in $(seq 1 45); do
    [[ "$(alive_count)" -eq 0 ]] && return 0
    sleep 1
  done
  return 1
}

ports_ready() {  # 期望 8020 / 9870 / 8088 / 8042 均监听
  "$EV_PART2_PYTHON" -c 'import socket,time
deadline=time.monotonic()+45
for port in (8020,9870,8088,8042):
    while True:
        try:
            with socket.create_connection(("127.0.0.1",port),timeout=1): pass
            break
        except OSError:
            if time.monotonic()>deadline: raise SystemExit("服务未就绪，请检查 Hadoop 日志："+str(port))
            time.sleep(.5)
print("HDFS/YARN 端口已就绪。")'
}

start_daemon() {
  local command="$1" daemon="$2"
  if "$command" --daemon status "$daemon" >/dev/null 2>&1; then
    echo "$daemon 已运行，保留现有进程。"
  else
    "$command" --daemon start "$daemon"
  fi
}

case "${1:-status}" in
  start)
    if [[ "$EV_PART2_CLUSTER_MODE" == "system" ]]; then
      echo "形态：system（系统级 Hadoop，执行身份：$EV_PART2_HADOOP_USER）"
      if [[ "$(alive_count)" -ge 5 ]]; then
        echo '五个守护进程已在运行，跳过启动。'
      else
        as_hadoop "$HADOOP_HOME/sbin/start-dfs.sh" || true
        as_hadoop "$HADOOP_HOME/sbin/start-yarn.sh" || true
        if ! wait_alive 5; then
          echo "错误：守护进程仅 $(alive_count)/5，未达预期。请检查 $HADOOP_LOG_DIR。" >&2
          exit 1
        fi
      fi
    else
      echo '形态：local（ev-part2 专用目录）'
      "$EV_PART2_PYTHON" "$ev_p2_toolchain/bin/configure_local.py"
      part2_name_dir="$EV_PART2_HOME/runtime/hdfs/name"
      if [[ ! -f "$part2_name_dir/current/VERSION" ]]; then
        if [[ -n "$(ls -A "$part2_name_dir")" || -n "$(ls -A "$EV_PART2_HOME/runtime/hdfs/data")" ]]; then
          echo '检测到已有 HDFS 内容但无有效 NameNode 元数据，拒绝重新格式化。' >&2
          echo '（若确实要换基线重建：清空 $EV_PART2_HOME/runtime/hdfs 后重试）' >&2
          exit 1
        fi
        hdfs namenode -format -nonInteractive >"$HADOOP_LOG_DIR/first-format.log" 2>&1
        echo "已初始化空的专用 HDFS 目录；日志：$HADOOP_LOG_DIR/first-format.log"
      fi
      start_daemon hdfs namenode
      start_daemon hdfs datanode
      start_daemon hdfs secondarynamenode
      start_daemon yarn resourcemanager
      start_daemon yarn nodemanager
      if ! wait_alive 5; then
        echo "错误：守护进程仅 $(alive_count)/5，未达预期。" >&2
        exit 1
      fi
    fi
    ports_ready
    timeout 45 hdfs dfsadmin -safemode wait || true
    hdfs dfs -mkdir -p /ev-charging/ods /ev-charging/dwd /ev-charging/dws /ev-charging/ads /ev-charging/quality /ev-charging/forecast "/user/$(id -un)"
    echo "已启动本机单节点伪分布式（守护进程 $(alive_count)/5）：HDFS http://localhost:9870 / YARN http://localhost:8088"
    ;;
  stop)
    if [[ "$EV_PART2_CLUSTER_MODE" == "system" ]]; then
      echo "形态：system（执行身份：$EV_PART2_HADOOP_USER）"
      as_hadoop "$HADOOP_HOME/sbin/stop-yarn.sh" || true
      as_hadoop "$HADOOP_HOME/sbin/stop-dfs.sh" || true
      # 兜底：启停脚本可能因 PID 目录不一致而漏停，按进程真实存在性再收一次
      if ! wait_dead; then
        for pid in $(as_hadoop 'jps' 2>/dev/null | awk '/NameNode|DataNode|SecondaryNameNode|ResourceManager|NodeManager/ {print $1}'); do
          as_hadoop "kill $pid" || true
        done
        wait_dead || echo "警告：仍有 $(alive_count) 个守护进程未退出，请人工检查。" >&2
      fi
    else
      echo '形态：local'
      yarn --daemon stop nodemanager || true
      yarn --daemon stop resourcemanager || true
      hdfs --daemon stop secondarynamenode || true
      hdfs --daemon stop datanode || true
      hdfs --daemon stop namenode || true
      wait_dead || echo "警告：仍有 $(alive_count) 个守护进程未退出。" >&2
    fi
    echo '已停止第二阶段 Hadoop/YARN 进程；数据保留。'
    ;;
  status)
    echo "形态：$EV_PART2_CLUSTER_MODE（执行身份：$EV_PART2_HADOOP_USER）"
    echo "守护进程：$(alive_count)/5"
    as_hadoop 'jps -l' || true
    hdfs dfsadmin -report 2>/dev/null | head -20 || true
    yarn node -list 2>/dev/null || true
    ;;
  *) exit 2 ;;
esac
