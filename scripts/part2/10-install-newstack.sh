#!/usr/bin/env bash
# 第二阶段新环境安装：Temurin JDK 17 + Hadoop 3.4.1 + Spark 3.5.7
# 依据：桌面 Part2/06-TL-Hadoop伪分布式安装部署指南.md（2026-09-14 11:32 版）
#
# 用法（需要 sudo，会要一次密码）：
#   sudo bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/10-install-newstack.sh
#
# 前置：三个 tarball 已下载到 $HOME/software/
#   jdk17.tar.gz                        （Adoptium Temurin 17，清华镜像）
#   hadoop-3.4.1.tar.gz                 （Apache 官方 / 清华镜像）
#   spark-3.5.7-bin-hadoop3.tgz         （Apache 官方 / 华为云镜像）
#
# 本机既有假设（如实记录，与指南的差异见文末）：
#   - 主机名 TimeMachine（指南建议「姓名全拼+两位数字」）
#   - 用户 spiderboy（指南示例为 hadoop）
#   - Python 3.11 虚拟环境已存在于 ~/venvs/part2（指南示例为 ~/venv-py311）
set -u

USER_NAME=${SUDO_USER:-spiderboy}
USER_HOME=$(getent passwd "$USER_NAME" | cut -d: -f6)
HOSTNAME_NOW=$(hostname)
SW="$USER_HOME/software"

JDK_DIR=/usr/local/jdk-17
HADOOP_DIR=/usr/local/hadoop
SPARK_DIR=/usr/local/spark

[ "$(id -u)" -eq 0 ] || { echo "请用 sudo 运行"; exit 1; }
[ -d "$SW" ] || { echo "找不到 $SW"; exit 1; }

echo "=============== 1/9 校验安装包 ==============="
cd "$SW"
for f in jdk17.tar.gz hadoop-3.4.1.tar.gz spark-3.5.7-bin-hadoop3.tgz; do
  [ -s "$f" ] || { echo "缺少 $f（下载未完成？）"; exit 1; }
  echo "  $f  $(du -h "$f" | cut -f1)"
done
# Hadoop / Spark 官方 sha512（下载失败则跳过校验并提示）
wget -q -O hadoop.sha512 https://archive.apache.org/dist/hadoop/common/hadoop-3.4.1/hadoop-3.4.1.tar.gz.sha512 \
  && echo "$(awk '{print $1}' hadoop.sha512)  hadoop-3.4.1.tar.gz" | sha512sum -c - || echo "  [警告] Hadoop 校验和获取失败，跳过"
wget -q -O spark.sha512 https://archive.apache.org/dist/spark/spark-3.5.7/spark-3.5.7-bin-hadoop3.tgz.sha512 \
  && echo "$(awk '{print $1}' spark.sha512)  spark-3.5.7-bin-hadoop3.tgz" | sha512sum -c - || echo "  [警告] Spark 校验和获取失败，跳过"
echo "  JDK 由清华 Adoptium 镜像下发（发布页可复核 sha256）"

echo
echo "=============== 2/9 停止旧环境（Hadoop 3.2.1，如仍在运行） ==============="
if [ -x /opt/module/hadoop-3.2.1/sbin/stop-all.sh ]; then
  su - "$USER_NAME" -c '/opt/module/hadoop-3.2.1/sbin/stop-all.sh' 2>/dev/null || true
fi
sleep 3
pgrep -f 'org.apache.hadoop' >/dev/null 2>&1 && { echo "  强制结束残留 Hadoop 进程"; pkill -f 'org.apache.hadoop' || true; sleep 2; } || echo "  无残留进程"
rm -rf /tmp/hadoop-* 2>/dev/null || true

echo
echo "=============== 3/9 安装 JDK 17 / Hadoop 3.4.1 / Spark 3.5.7 ==============="
install_tar() { # $1=tarball $2=解压后目录名 $3=目标名
  local tar="$1" inner="$2" target="$3"
  rm -rf "/usr/local/$inner" "$target"
  tar -zxf "$SW/$tar" -C /usr/local || return 1
  mv "/usr/local/$inner" "$target"
  chown -R "$USER_NAME:$USER_NAME" "$target"
  echo "  $target 就绪"
}
install_tar jdk17.tar.gz "$(tar -tzf "$SW/jdk17.tar.gz" | head -1 | cut -d/ -f1)" "$JDK_DIR"
install_tar hadoop-3.4.1.tar.gz hadoop-3.4.1 "$HADOOP_DIR"
install_tar spark-3.5.7-bin-hadoop3.tgz spark-3.5.7-bin-hadoop3 "$SPARK_DIR"

echo
echo "=============== 4/9 写 7 个 Hadoop 配置文件（含 Java 17 --add-opens） ==============="
C=$HADOOP_DIR/etc/hadoop
for f in hadoop-env.sh yarn-env.sh core-site.xml hdfs-site.xml mapred-site.xml yarn-site.xml workers; do
  [ -f "$C/$f" ] && cp -f "$C/$f" "$C/$f.orig-$(date +%H%M%S)"
done

cat > "$C/hadoop-env.sh" <<EOF
export JAVA_HOME=$JDK_DIR

# Java 17 起 JDK 强封装，Hadoop 反射访问会被拒绝（InaccessibleObjectException）
export JDK_ADD_OPENS="--add-opens java.base/java.lang=ALL-UNNAMED --add-opens java.base/java.lang.reflect=ALL-UNNAMED --add-opens java.base/java.io=ALL-UNNAMED --add-opens java.base/java.net=ALL-UNNAMED --add-opens java.base/java.nio=ALL-UNNAMED --add-opens java.base/java.util=ALL-UNNAMED --add-opens java.base/java.util.concurrent=ALL-UNNAMED --add-opens java.base/java.util.concurrent.atomic=ALL-UNNAMED --add-opens java.base/sun.nio.ch=ALL-UNNAMED --add-opens java.base/sun.security.action=ALL-UNNAMED --add-opens java.base/jdk.internal.ref=ALL-UNNAMED --add-opens java.security.jgss/sun.security.krb5=ALL-UNNAMED"

export HDFS_NAMENODE_OPTS="\$JDK_ADD_OPENS"
export HDFS_DATANODE_OPTS="\$JDK_ADD_OPENS"
export HDFS_SECONDARYNAMENODE_OPTS="\$JDK_ADD_OPENS"
export HADOOP_OPTS="\$JDK_ADD_OPENS"

# 守护进程归属用户（非 root 启动时脚本会校验，写死为当前用户避免 'can only be executed by root'）
export HDFS_NAMENODE_USER=$USER_NAME
export HDFS_DATANODE_USER=$USER_NAME
export HDFS_SECONDARYNAMENODE_USER=$USER_NAME
export YARN_RESOURCEMANAGER_USER=$USER_NAME
export YARN_NODEMANAGER_USER=$USER_NAME
EOF

cat > "$C/yarn-env.sh" <<EOF
export JAVA_HOME=$JDK_DIR
export JDK_ADD_OPENS="--add-opens java.base/java.lang=ALL-UNNAMED --add-opens java.base/java.util=ALL-UNNAMED --add-opens java.base/java.util.concurrent=ALL-UNNAMED --add-opens java.base/java.net=ALL-UNNAMED --add-opens java.base/java.nio=ALL-UNNAMED"
export YARN_RESOURCEMANAGER_OPTS="\$JDK_ADD_OPENS"
export YARN_NODEMANAGER_OPTS="\$JDK_ADD_OPENS"
EOF

echo "$HOSTNAME_NOW" > "$C/workers"

cat > "$C/core-site.xml" <<EOF
<configuration>
  <property>
    <name>fs.defaultFS</name>
    <value>hdfs://$HOSTNAME_NOW:8020</value>
  </property>
  <property>
    <name>hadoop.tmp.dir</name>
    <value>file:$HADOOP_DIR/tmp</value>
  </property>
</configuration>
EOF

cat > "$C/hdfs-site.xml" <<EOF
<configuration>
  <property>
    <name>dfs.namenode.secondary.http-address</name>
    <value>$HOSTNAME_NOW:9868</value>
  </property>
  <property>
    <name>dfs.namenode.name.dir</name>
    <value>file://$HADOOP_DIR/dfs/name</value>
  </property>
  <property>
    <name>dfs.datanode.data.dir</name>
    <value>file://$HADOOP_DIR/dfs/data</value>
  </property>
  <property>
    <name>dfs.replication</name>
    <value>1</value>
  </property>
  <!-- Web UI 绑定 0.0.0.0：宿主机浏览器可直接访问（本机 hosts 把主机名解析到 127.0.1.1） -->
  <property>
    <name>dfs.namenode.http-bind-host</name>
    <value>0.0.0.0</value>
  </property>
  <property>
    <name>dfs.namenode.rpc-bind-host</name>
    <value>0.0.0.0</value>
  </property>
</configuration>
EOF

cat > "$C/mapred-site.xml" <<EOF
<configuration>
  <property>
    <name>mapreduce.framework.name</name>
    <value>yarn</value>
  </property>
  <property>
    <name>yarn.app.mapreduce.am.command-opts</name>
    <value>-Xmx1024m --add-opens java.base/java.lang=ALL-UNNAMED</value>
  </property>
  <property>
    <name>mapreduce.map.java.opts</name>
    <value>-Xmx1024m --add-opens java.base/java.lang=ALL-UNNAMED</value>
  </property>
  <property>
    <name>mapreduce.reduce.java.opts</name>
    <value>-Xmx1024m --add-opens java.base/java.lang=ALL-UNNAMED</value>
  </property>
</configuration>
EOF

cat > "$C/yarn-site.xml" <<EOF
<configuration>
  <property>
    <name>yarn.nodemanager.aux-services</name>
    <value>mapreduce_shuffle</value>
  </property>
  <property>
    <name>yarn.nodemanager.aux-services.mapreduce.shuffle.class</name>
    <value>org.apache.hadoop.mapred.ShuffleHandler</value>
  </property>
  <property>
    <name>yarn.resourcemanager.address</name>
    <value>$HOSTNAME_NOW:8032</value>
  </property>
  <property>
    <name>yarn.resourcemanager.bind-host</name>
    <value>0.0.0.0</value>
  </property>
  <property>
    <name>yarn.nodemanager.bind-host</name>
    <value>0.0.0.0</value>
  </property>
  <!-- 本机内存 5.3GB，指南建议 4096 并允许按内存下调 -->
  <property>
    <name>yarn.nodemanager.resource.memory-mb</name>
    <value>3072</value>
  </property>
  <property>
    <name>yarn.scheduler.maximum-allocation-mb</name>
    <value>3072</value>
  </property>
  <property>
    <name>yarn.scheduler.minimum-allocation-mb</name>
    <value>512</value>
  </property>
  <property>
    <name>yarn.nodemanager.env-whitelist</name>
    <value>JAVA_HOME,HADOOP_COMMON_HOME,HADOOP_HDFS_HOME,HADOOP_CONF_DIR,CLASSPATH_PREPEND_DISTCACHE,HADOOP_YARN_HOME,HADOOP_HOME,PATH,LANG,TZ,HADOOP_MAPRED_HOME</value>
  </property>
</configuration>
EOF
echo "  7 个文件写入完成"

echo
echo "=============== 5/9 Spark 配置 ==============="
cp -f "$C/core-site.xml" "$C/hdfs-site.xml" "$SPARK_DIR/conf/" 2>/dev/null || true
cat > "$SPARK_DIR/conf/spark-env.sh" <<EOF
export JAVA_HOME=$JDK_DIR
export HADOOP_CONF_DIR=$C
export SPARK_DIST_CLASSPATH=\$($HADOOP_DIR/bin/hadoop classpath)
export PYSPARK_PYTHON=$USER_HOME/venvs/part2/bin/python
export PYSPARK_DRIVER_PYTHON=\$PYSPARK_PYTHON
EOF
cat > "$SPARK_DIR/conf/spark-defaults.conf" <<EOF
spark.sql.shuffle.partitions 8
spark.executor.memory 1g
spark.driver.memory 1g
spark.yarn.am.memory 512m
spark.driver.maxResultSize 512m
spark.yarn.jars hdfs://$HOSTNAME_NOW:8020/spark/jars/*
EOF
chown -R "$USER_NAME:$USER_NAME" "$SPARK_DIR/conf"
# 指南里的 ~/venv-py311 路径做个软链，保证文档与实机一致
su - "$USER_NAME" -c "[ -e ~/venv-py311 ] || ln -s ~/venvs/part2 ~/venv-py311" 2>/dev/null || true
echo "  spark-env.sh / spark-defaults.conf 写入完成"

echo
echo "=============== 6/9 环境变量 ==============="
cat > /etc/profile.d/part2-env.sh <<EOF
# 第二阶段环境（JDK 17 + Hadoop 3.4.1 + Spark 3.5.7）
export JAVA_HOME=$JDK_DIR
export HADOOP_HOME=$HADOOP_DIR
export HADOOP_CONF_DIR=$C
export YARN_HOME=$HADOOP_DIR
export YARN_CONF_DIR=$C
export SPARK_HOME=$SPARK_DIR
export PATH=\$JAVA_HOME/bin:\$HADOOP_HOME/bin:\$HADOOP_HOME/sbin:\$SPARK_HOME/bin:\$PATH
export PYSPARK_PYTHON=$USER_HOME/venvs/part2/bin/python
export PYSPARK_DRIVER_PYTHON=\$PYSPARK_PYTHON
export HADOOP_ROOT_LOGGER=WARN,console
EOF
chmod 644 /etc/profile.d/part2-env.sh
# 关键：旧安装会把 JAVA_HOME/HADOOP_HOME 追加在 /etc/profile 末尾，
# 而它在 /etc/profile.d 之后执行，会覆盖上面的新路径（症状：hadoop/hdfs 报
# "Cannot execute /opt/module/..."）。这里一并清掉。
if grep -q '^# ===== JDK 环境变量 =====' /etc/profile 2>/dev/null; then
  cp -f /etc/profile "/etc/profile.bak-$(date +%H%M%S)"
  sed -i '/^# ===== JDK 环境变量 =====/,$d' /etc/profile
  echo "  已清理 /etc/profile 末尾的旧环境块"
fi
# ~/.bashrc 里的旧变量块清掉，避免指向已删除的旧目录
su - "$USER_NAME" -c "sed -i '/===== 第二阶段：JDK\/Hadoop/,+3d; /===== 第二阶段：Spark \/ PySpark/,+6d' ~/.bashrc" 2>/dev/null || true
su - "$USER_NAME" -c "grep -q 'part2-env.sh' ~/.bashrc || echo '. /etc/profile.d/part2-env.sh' >> ~/.bashrc" 2>/dev/null || true
echo "  /etc/profile.d/part2-env.sh 写入，~/.bashrc 已改为引用它"

echo
echo "=============== 7/9 删除旧环境 ==============="
rm -rf /opt/module/hadoop-3.2.1 /opt/module/jdk1.8.0_261
rm -f /opt/software/hadoop-3.2.1.tar.gz /opt/software/jdk-8u261-linux-x64.tar.gz
ls -d /opt/module/* 2>/dev/null || echo "  /opt/module 已清空"

echo
echo "=============== 8/9 格式化并启动 ==============="
su - "$USER_NAME" -c "$HADOOP_DIR/bin/hdfs namenode -format -force" 2>&1 | tail -3
su - "$USER_NAME" -c "$HADOOP_DIR/sbin/start-dfs.sh" 2>&1 | tail -3
su - "$USER_NAME" -c "$HADOOP_DIR/sbin/start-yarn.sh" 2>&1 | tail -3
sleep 8
su - "$USER_NAME" -c "$JDK_DIR/bin/jps" | sort -k2

echo
echo "=============== 9/9 建 HDFS 目录 + 预传 Spark jars ==============="
su - "$USER_NAME" -c "
  export PATH=$HADOOP_DIR/bin:\$PATH
  for d in ods dwd dws ads quality forecast; do hdfs dfs -mkdir -p /ev-charging/\$d; done
  hdfs dfs -mkdir -p /input /spark/jars
  hdfs dfs -put -f $C/*.xml /input/ 2>/dev/null
  hdfs dfs -put -f $SPARK_DIR/jars/* /spark/jars/ 2>/dev/null
  echo '  HDFS 目录与 jars 上传完成'
  hdfs dfs -ls / | tail -6
"

echo
echo "================ 完成，回滚点：快照「新环境安装前-20260914」 ================"
echo "接下来跑验收：bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/40-verify.sh"
echo
echo "与本组指南的差异（已如实记录）："
echo "  - 主机名保持 TimeMachine（指南建议「姓名全拼+两位数字」）"
echo "  - 操作用户保持 $USER_NAME（指南示例为 hadoop）"
echo "  - Python 虚拟环境为 $USER_HOME/venvs/part2（并软链 ~/venv-py311 以对齐指南路径）"
echo "  - YARN 容器内存 3072MB（指南建议 4096，本机总内存 5.3GB，指南允许下调）"
