#!/usr/bin/env bash
# 直接启动本机守护进程，无需改 SSH 密钥或开放跨机登录。
set -euo pipefail
if [[ $EUID -eq 0 ]]; then
  echo '请用普通用户启动／停止第二阶段服务，不要使用 sudo。' >&2
  exit 1
fi
source /usr/local/ev-part2/env.sh
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
    "$EV_PART2_PYTHON" /usr/local/ev-part2/bin/configure_local.py
    part2_name_dir="$EV_PART2_HOME/runtime/hdfs/name"
    if [[ ! -f "$part2_name_dir/current/VERSION" ]]; then
      if [[ -n "$(ls -A "$part2_name_dir")" || -n "$(ls -A "$EV_PART2_HOME/runtime/hdfs/data")" ]]; then
        echo '检测到已有 HDFS 内容但无有效 NameNode 元数据，拒绝重新格式化。' >&2
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
    "$EV_PART2_PYTHON" -c 'import socket,time
deadline=time.monotonic()+45
for port in (8020,9870,8088,8042):
    while True:
        try:
            with socket.create_connection(("127.0.0.1",port),timeout=1): pass
            break
        except OSError:
            if time.monotonic()>deadline: raise SystemExit("服务未就绪，请检查 runtime/logs/hadoop："+str(port))
            time.sleep(.5)
print("HDFS/YARN 端口已就绪。")'
    timeout 45 hdfs dfsadmin -safemode wait
    hdfs dfs -mkdir -p /ev-charging/ods /ev-charging/dwd /ev-charging/dws /ev-charging/ads /ev-charging/quality /ev-charging/forecast "/user/$(id -un)"
    echo '已启动本机单节点伪分布式：HDFS http://localhost:9870 / YARN http://localhost:8088'
    ;;
  stop)
    yarn --daemon stop nodemanager
    yarn --daemon stop resourcemanager
    hdfs --daemon stop secondarynamenode
    hdfs --daemon stop datanode
    hdfs --daemon stop namenode
    echo '已停止本用户的第二阶段 Hadoop/YARN 进程；数据保留。'
    ;;
  status)
    jps -l
    hdfs dfsadmin -report
    yarn node -list
    ;;
  *) exit 2 ;;
esac
