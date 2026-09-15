#!/usr/bin/env bash
# 本机全局工具链入口；不改动 /usr/bin/python3，也不启用虚拟环境。
export EV_PART2_HOME="${EV_PART2_HOME:-$HOME/ev-part2}"
export EV_PART2_TOOLCHAIN=/usr/local/ev-part2
export JAVA_HOME="$EV_PART2_TOOLCHAIN/jdk8"
export HADOOP_HOME=/usr/local/hadoop
export SPARK_HOME=/usr/local/spark
export EV_PART2_PYTHON="$EV_PART2_TOOLCHAIN/python/bin/python3.10"
export PYSPARK_PYTHON="$EV_PART2_PYTHON"
export PYSPARK_DRIVER_PYTHON="$EV_PART2_PYTHON"
export HADOOP_CONF_DIR="$EV_PART2_HOME/config/hadoop"
export YARN_CONF_DIR="$HADOOP_CONF_DIR"
export SPARK_CONF_DIR="$EV_PART2_HOME/config/spark"
export HADOOP_LOG_DIR="$EV_PART2_HOME/runtime/logs/hadoop"
export YARN_LOG_DIR="$HADOOP_LOG_DIR"
export HADOOP_PID_DIR="$EV_PART2_HOME/runtime/pids"
export YARN_PID_DIR="$HADOOP_PID_DIR"
export HADOOP_IDENT_STRING="ev-part2-$(id -un)"
export HADOOP_STOP_TIMEOUT=20
export YARN_IDENT_STRING="$HADOOP_IDENT_STRING"
export SPARK_LOCAL_DIRS="$EV_PART2_HOME/runtime/spark-local"
export SPARK_LOCAL_IP=127.0.0.1
export PATH="$JAVA_HOME/bin:$HADOOP_HOME/bin:$SPARK_HOME/bin:$PATH"
