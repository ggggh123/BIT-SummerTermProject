#!/usr/bin/env bash
# 第二阶段环境体检（只读，不改配置不装软件）
# 基线：Temurin JDK 17 + Hadoop 3.4.1 + Spark 3.5.7，装 /usr/local/{jdk-17,hadoop,spark}，HDFS 端口 8020
#
# 用法（在 Ubuntu 虚拟机里执行）：
#   bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-check.sh
# 报告回传宿主机（Windows 端 scripts/part2/env-report.txt）：
#   bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-check.sh | tee /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-report.txt
set -u
# 非交互 shell 不会自动加载 /etc/profile，这里显式加载基线环境变量
. /etc/profile.d/part2-env.sh 2>/dev/null || true
have() { command -v "$1" >/dev/null 2>&1; }
ok=0; bad=0; miss=""
pass() { printf '  [OK]   %s\n' "$1"; ok=$((ok+1)); }
lack() { printf '  [缺]   %s  → %s\n' "$1" "$2"; bad=$((bad+1)); miss="$miss\n    - $1：$2"; }
info() { printf '  %-16s%s\n' "$1" "$2"; }

echo "==================== 1. 系统 ===================="
host=$(hostname); info "主机名" "$host"
. /etc/os-release 2>/dev/null || true
info "系统" "${PRETTY_NAME:-未知}（${VERSION_CODENAME:-未知}）"
info "内核" "$(uname -r)"
info "内存" "$(free -h | awk '/^Mem:/{print $2" 总量, "$7" 可用"}')"
info "磁盘" "$(df -h / | awk 'NR==2{printf "%s 可用 / %s", $4, $2}')"
[ "${VERSION_CODENAME:-}" = "plucky" ] && pass "25.04 已 EOL：apt 需切 old-releases（《06》§3.3）" || pass "系统非 25.04"

echo "==================== 2. 版本基线 ===================="
if have java; then
  v=$(java -version 2>&1 | head -1); pass "java：$v"
  case "$v" in *17*) pass "JDK 17 符合基线" ;; *) lack "JDK 版本" "基线要求 Temurin 17" ;; esac
else lack "java" "JDK 17 未安装或未进 PATH"; fi
if have hadoop; then
  v=$(hadoop version 2>/dev/null | head -1); pass "hadoop：$v"
  case "$v" in *3.4.1*) pass "Hadoop 3.4.1 符合基线" ;; *) lack "Hadoop 版本" "基线要求 3.4.1" ;; esac
else lack "hadoop" "Hadoop 3.4.1 未安装"; fi
if have spark-submit; then
  v=$(spark-submit --version 2>&1 | grep -m1 -i 'version [0-9]'); pass "spark：$v"
  case "$v" in *3.5.7*) pass "Spark 3.5.7 符合基线" ;; *) lack "Spark 版本" "基线要求 3.5.7（全组同小版本）" ;; esac
else lack "spark-submit" "Spark 3.5.7 未安装"; fi
info "JAVA_HOME" "${JAVA_HOME:-未设置}"
info "HADOOP_HOME" "${HADOOP_HOME:-未设置}"

echo "==================== 3. Hadoop 配置与进程 ===================="
C=${HADOOP_CONF_DIR:-/usr/local/hadoop/etc/hadoop}
if [ -d "$C" ]; then
  pass "配置目录 $C"
  info "fs.defaultFS" "$(grep -A2 'fs.defaultFS' "$C/core-site.xml" 2>/dev/null | grep -o '<value>[^<]*' | sed 's/<value>//')"
  info "副本数" "$(grep -A2 'dfs.replication' "$C/hdfs-site.xml" 2>/dev/null | grep -o '<value>[^<]*' | sed 's/<value>//')"
  grep -q ':8020' "$C/core-site.xml" 2>/dev/null && pass "HDFS RPC 8020" || lack "HDFS 端口" "基线统一 8020（不是 9000）"
  grep -q 'add-opens' "$C/hadoop-env.sh" 2>/dev/null && pass "Java 17 --add-opens 已配" || lack "add-opens" "Java 17 必须放开反射（《06》§5.7(1)）"
else lack "Hadoop 配置目录" "找不到 $C"; fi
if have jps; then
  JP=$(jps 2>/dev/null | awk '{print $2}' | tr '\n' ' '); info "jps" "${JP:-无}"
  for p in NameNode DataNode SecondaryNameNode ResourceManager NodeManager; do
    case "$JP" in *"$p"*) pass "进程 $p" ;; *) lack "进程 $p" "未启动：start-dfs.sh + start-yarn.sh" ;; esac
  done
fi
if have hdfs; then
  if hdfs dfs -ls / >/dev/null 2>&1; then
    pass "HDFS 可访问"
    for d in ods dwd dws ads quality forecast; do
      hdfs dfs -test -d "/ev-charging/$d" 2>/dev/null || lack "HDFS 目录 /ev-charging/$d" "hdfs dfs -mkdir -p /ev-charging/$d"
    done
  else lack "HDFS 读写" "服务未起或处于安全模式"; fi
fi

echo "==================== 4. Spark / Python ===================="
info "SPARK_HOME" "${SPARK_HOME:-未设置}"
PY=${PYSPARK_PYTHON:-}
if [ -n "$PY" ] && [ -x "$PY" ]; then
  pass "PYSPARK_PYTHON → $($PY -V 2>&1)"
  case "$($PY -V 2>&1)" in *3.9*|*3.10*|*3.11*) pass "Python 版本可用" ;; *) lack "Python 版本" "PySpark 3.5 需 3.8–3.11（25.04 系统 3.13 不可用）" ;; esac
  if "$PY" -c 'import pyspark' 2>/dev/null; then pass "pyspark $($PY -c 'import pyspark;print(pyspark.__version__)')" ; else lack "pyspark 库" "uv pip install pyspark==3.5.7"; fi
else lack "PYSPARK_PYTHON" "未设置或不可执行（25.04 必须指向自建 3.11）"; fi

echo "==================== 5. 其他 ===================="
if have ssh && ssh -o BatchMode=yes -o StrictHostKeyChecking=no "$(id -un)@$host" echo ok >/dev/null 2>&1; then
  pass "SSH 免密自连"
else lack "SSH 免密" "ssh-copy-id $(id -un)@$host"; fi
[ -d /mnt/hgfs/BIT-SummerTermProject ] && pass "共享文件夹已挂载" || lack "共享文件夹" "未挂载到 /mnt/hgfs"

echo
echo "==================== 结论 ===================="
echo "  通过 $ok 项，缺失 $bad 项"
[ "$bad" -gt 0 ] && printf '  缺失清单：%b\n' "$miss" || echo "  环境齐备"
