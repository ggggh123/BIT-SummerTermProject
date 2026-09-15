#!/usr/bin/env bash
# 修补 /etc/profile 中的旧环境变量，并补做格式化 + 启动 + HDFS 目录
# 背景：旧环境把 JAVA_HOME/HADOOP_HOME 写在 /etc/profile 末尾（在 /etc/profile.d 之后执行），
#       会覆盖 /etc/profile.d/part2-env.sh 的新路径，导致 hadoop/hdfs 命令指向已删除的旧目录。
#
# 用法（需要 sudo）：
#   sudo bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/11-fix-profile-and-start.sh
set -u
USER_NAME=${SUDO_USER:-spiderboy}
HADOOP_DIR=/usr/local/hadoop
JDK_DIR=/usr/local/jdk-17
SPARK_DIR=/usr/local/spark

[ "$(id -u)" -eq 0 ] || { echo "请用 sudo 运行"; exit 1; }

echo "=============== 1/4 清理 /etc/profile 里的旧环境块 ==============="
cp -f /etc/profile "/etc/profile.bak-$(date +%H%M%S)"
if grep -q '^# ===== JDK 环境变量 =====' /etc/profile; then
  # 该块一直延伸到文件末尾，直接截断到标记行之前
  sed -i '/^# ===== JDK 环境变量 =====/,$d' /etc/profile
  echo "  已删除旧块（备份为 /etc/profile.bak-*）"
else
  echo "  未发现旧块，跳过"
fi
echo "  残留旧路径：$(grep -c 'jdk1.8\|hadoop-3.2.1' /etc/profile || true) 处"

echo
echo "=============== 2/4 校验登录 shell 的环境 ==============="
su - "$USER_NAME" -c 'java -version 2>&1 | head -1; hadoop version 2>/dev/null | head -1; echo "JAVA_HOME=$JAVA_HOME"; echo "HADOOP_HOME=$HADOOP_HOME"; echo "SPARK_HOME=$SPARK_HOME"'

echo
echo "=============== 3/4 格式化 NameNode 并启动 ==============="
su - "$USER_NAME" -c "$HADOOP_DIR/bin/hdfs namenode -format -force" 2>&1 | tail -3
su - "$USER_NAME" -c "$HADOOP_DIR/sbin/start-dfs.sh" 2>&1 | tail -3
su - "$USER_NAME" -c "$HADOOP_DIR/sbin/start-yarn.sh" 2>&1 | tail -3
sleep 8
echo "--- jps ---"
su - "$USER_NAME" -c "$JDK_DIR/bin/jps" | sort -k2

echo
echo "=============== 4/4 建 HDFS 目录 + 预传 Spark jars ==============="
su - "$USER_NAME" -c "
  export PATH=$HADOOP_DIR/bin:\$PATH
  for d in ods dwd dws ads quality forecast; do hdfs dfs -mkdir -p /ev-charging/\$d; done
  hdfs dfs -mkdir -p /input /spark/jars
  hdfs dfs -put -f $HADOOP_DIR/etc/hadoop/*.xml /input/ 2>/dev/null
  hdfs dfs -put -f $SPARK_DIR/jars/* /spark/jars/ 2>/dev/null
  echo '  HDFS 根目录：'; hdfs dfs -ls / | tail -6
  echo '  /spark/jars 数量：'\$(hdfs dfs -ls /spark/jars | grep -c '\.jar')
"

echo
echo "=============== 完成 ==============="
echo "接着跑验收：bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/40-verify.sh"
