#!/usr/bin/env bash
# 本机第二阶段运行时入口。
#
# 默认指向**团队冻结基线**：JDK 17 + Hadoop 3.4.1 + Spark 3.5.7 + 系统 Python。
# 兼容两种环境形态：
#   · Ubuntu 22.04（主环境）：系统 Python 3.10 可直接用
#   · Ubuntu 25.04（开发环境）：系统 Python 3.13 与 PySpark 3.5 不兼容，需显式指定 3.10/3.11
# 所有工具链路径均支持 `EV_PART2_*` 覆盖，旧基线（JDK 8 + Hadoop 3.2.1 + 自建 CPython）的机器
# 只需保留自己的环境变量即可继续工作，不必改本文件。

# ── 0. 位置与根目录 ────────────────────────────────────────────────
export EV_PART2_TOOLCHAIN="${EV_PART2_TOOLCHAIN:-/usr/local/ev-part2}"
export EV_PART2_HOME="${EV_PART2_HOME:-$HOME/ev-part2}"

# ── 1. JDK：显式变量 → 基线标准位置 → 从 PATH 反查 ─────────────────
if [[ -z "${JAVA_HOME:-}" ]]; then
  for _ev_p2_cand in /usr/local/jdk-17 /usr/local/jdk-17.*; do
    if [[ -x "$_ev_p2_cand/bin/java" ]]; then JAVA_HOME="$_ev_p2_cand"; break; fi
  done
  if [[ -z "${JAVA_HOME:-}" ]] && command -v java >/dev/null 2>&1; then
    JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
  fi
fi
export JAVA_HOME

# ── 2. Hadoop / Spark：显式变量 → 基线标准位置 ─────────────────────
export HADOOP_HOME="${HADOOP_HOME:-/usr/local/hadoop}"
export SPARK_HOME="${SPARK_HOME:-/usr/local/spark}"

# ── 3. Python：显式变量 → 候选位置 → 系统 python3 ──────────────────
if [[ -z "${EV_PART2_PYTHON:-}" ]]; then
  for _ev_p2_cand in \
      "$EV_PART2_TOOLCHAIN/python/bin/python3.10" \
      "$HOME/venvs/part2/bin/python" \
      "$HOME/venv-py311/bin/python" \
      /usr/bin/python3; do
    if [[ -x "$_ev_p2_cand" ]]; then EV_PART2_PYTHON="$_ev_p2_cand"; break; fi
  done
fi
export EV_PART2_PYTHON
export PYSPARK_PYTHON="${PYSPARK_PYTHON:-$EV_PART2_PYTHON}"
export PYSPARK_DRIVER_PYTHON="${PYSPARK_DRIVER_PYTHON:-$EV_PART2_PYTHON}"

# ── 4. 配置目录：仅当 ev-part2 自己的配置**确实可用**时才接管 ────────
# 避免两个坑：① 与系统级 /etc/profile.d/hadoop-env.sh 设置的 HADOOP_CONF_DIR 互相覆盖；
#            ② 空目录（曾创建过但未跑 ev-part2 configure）被误判为有效配置目录。
if [[ -f "$EV_PART2_HOME/config/hadoop/core-site.xml" ]]; then
  export HADOOP_CONF_DIR="$EV_PART2_HOME/config/hadoop"
elif [[ -z "${HADOOP_CONF_DIR:-}" ]] || [[ ! -f "${HADOOP_CONF_DIR}/core-site.xml" ]]; then
  export HADOOP_CONF_DIR="$HADOOP_HOME/etc/hadoop"
fi
export YARN_CONF_DIR="${YARN_CONF_DIR:-$HADOOP_CONF_DIR}"

if [[ -f "$EV_PART2_HOME/config/spark/spark-defaults.conf" ]]; then
  export SPARK_CONF_DIR="$EV_PART2_HOME/config/spark"
elif [[ -z "${SPARK_CONF_DIR:-}" ]] || [[ ! -d "${SPARK_CONF_DIR}" ]]; then
  export SPARK_CONF_DIR="$SPARK_HOME/conf"
fi

# ── 5. 运行目录与守护进程标识 ──────────────────────────────────────
export HADOOP_LOG_DIR="${HADOOP_LOG_DIR:-$HADOOP_HOME/logs}"
export YARN_LOG_DIR="${YARN_LOG_DIR:-$HADOOP_LOG_DIR}"
export HADOOP_PID_DIR="${HADOOP_PID_DIR:-$EV_PART2_HOME/runtime/pids}"
export YARN_PID_DIR="${YARN_PID_DIR:-$HADOOP_PID_DIR}"
export HADOOP_IDENT_STRING="${HADOOP_IDENT_STRING:-ev-part2-$(id -un)}"
export YARN_IDENT_STRING="${YARN_IDENT_STRING:-$HADOOP_IDENT_STRING}"
export HADOOP_STOP_TIMEOUT="${HADOOP_STOP_TIMEOUT:-20}"
export SPARK_LOCAL_DIRS="${SPARK_LOCAL_DIRS:-$EV_PART2_HOME/runtime/spark-local}"
export SPARK_LOCAL_IP="${SPARK_LOCAL_IP:-127.0.0.1}"

# ── 6. HDFS/YARN 客户端身份（伪分布式无 Kerberos）──────────────────
# 若本机守护进程由独立用户（如 hadoop）启动，客户端需以同身份访问，否则权限报错。
# 自动探测：存在 hadoop 用户且当前不是它 → 以它身份访问。
# 显式关闭：export EV_PART2_HDFS_USER=""；显式指定：export EV_PART2_HDFS_USER=<user>
if [[ -z "${EV_PART2_HDFS_USER+x}" ]]; then
  if id hadoop >/dev/null 2>&1 && [[ "$(id -un)" != "hadoop" ]]; then
    EV_PART2_HDFS_USER="hadoop"
  else
    EV_PART2_HDFS_USER=""
  fi
fi
if [[ -n "$EV_PART2_HDFS_USER" ]]; then
  export HADOOP_USER_NAME="${HADOOP_USER_NAME:-$EV_PART2_HDFS_USER}"
fi

export PATH="$JAVA_HOME/bin:$HADOOP_HOME/bin:$SPARK_HOME/bin:$PATH"
