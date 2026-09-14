#!/usr/bin/env bash
# 第二阶段环境体检（只读：不改配置、不装软件、不写业务数据）
#
# 用法（在 Ubuntu 虚拟机里执行；建议用 hadoop 或 root 用户，否则 jps/hdfs 看不到进程）：
#   bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-check.sh
# 想把报告回传给宿主机（Windows 这边能直接打开）：
#   bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-check.sh | tee /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-report.txt
# 若提示 $'\r': command not found，说明文件被转成了 CRLF，先执行：
#   sed -i 's/\r$//' /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-check.sh
#
# 末尾会打印「缺失清单」和对应安装命令，见同目录 README.md

have() { command -v "$1" >/dev/null 2>&1; }
ok=0; bad=0; miss=""
pass() { printf '  [OK]   %s\n' "$1"; ok=$((ok+1)); }
lack() { printf '  [缺]   %s  → %s\n' "$1" "$2"; bad=$((bad+1)); miss="$miss\n    - $1：$2"; }
info() { printf '  %-14s%s\n' "$1" "$2"; }

echo "==================== 1. 系统 ===================="
info "主机名" "$(hostname)"
codename=""; pretty=""
if [ -r /etc/os-release ]; then
  . /etc/os-release 2>/dev/null || true
  codename="${VERSION_CODENAME:-}"; pretty="${PRETTY_NAME:-}"
fi
info "系统" "$pretty   (codename=${codename:-未知})"
info "内核/架构" "$(uname -r)  $(uname -m)"
info "内存" "$(free -h 2>/dev/null | awk '/^Mem:/{print $2" 总量, "$7" 可用"}')"
info "磁盘" "$(df -h / /opt 2>/dev/null | awk 'NR>1{printf "%s 可用%s/%s  ", $6,$4,$2}')"
info "当前用户" "$(id -un)"
if [ "$codename" = "plucky" ]; then
  lack "Ubuntu 25.04 已 EOL" "apt 官方源已下架，需换 old-releases 源，见 README.md 第 0 节"
else
  pass "系统发行版非 EOL 的 25.04（apt 源可用）"
fi

echo "==================== 2. JDK ===================="
if have java; then
  jv="$(java -version 2>&1 | head -1)"
  info "java" "$jv"
  case "$jv" in *1.8*|*'"8.'*) pass "JDK 8（老师包 jdk-8u261）" ;; *) lack "JDK 版本" "期望 1.8.0_261，实际 $jv" ;; esac
else
  lack "java 命令" "JDK 未装或未进 PATH，见 README.md 第 4 节"
fi
have javac && pass "javac 可用" || lack "javac" "JDK 只装了 JRE"
info "JAVA_HOME" "${JAVA_HOME:-未设置}"

echo "==================== 3. Hadoop ===================="
info "HADOOP_HOME" "${HADOOP_HOME:-未设置}"
hd_dir=""
for d in /opt/module/hadoop-* /usr/local/hadoop /usr/local/hadoop-* /opt/hadoop*; do
  [ -d "$d" ] && hd_dir="$hd_dir $d"
done
info "发现的 Hadoop 目录" "${hd_dir:-无}"
if have hadoop; then
  pass "hadoop 命令：$(hadoop version 2>/dev/null | head -1)"
else
  lack "hadoop 命令" "Hadoop 未装或环境变量未生效，见 README.md 第 4 节"
fi
conf_dir="${HADOOP_CONF_DIR:-}"
[ -z "$conf_dir" ] && for d in /opt/module/hadoop-*/etc/hadoop /usr/local/hadoop/etc/hadoop; do [ -d "$d" ] && conf_dir="$d"; done
if [ -n "$conf_dir" ] && [ -d "$conf_dir" ]; then
  pass "Hadoop 配置目录：$conf_dir"
  info "fs.defaultFS" "$(grep -A2 'fs.defaultFS' "$conf_dir/core-site.xml" 2>/dev/null | grep -o '<value>[^<]*' | sed 's/<value>//')"
  info "dfs.replication" "$(grep -A2 'dfs.replication' "$conf_dir/hdfs-site.xml" 2>/dev/null | grep -o '<value>[^<]*' | sed 's/<value>//')"
else
  lack "core-site.xml" "找不到 Hadoop 配置目录"
fi
if have jps; then
  jp="$(jps 2>/dev/null | awk '{print $2}' | tr '\n' ' ')"
  info "jps 进程" "${jp:-无}"
  for p in NameNode DataNode SecondaryNameNode ResourceManager NodeManager; do
    case "$jp" in *"$p"*) pass "进程 $p" ;; *) lack "进程 $p" "未启动：start-all.sh（或 start-dfs.sh + start-yarn.sh）" ;; esac
  done
fi
if have hdfs; then
  if hdfs dfs -ls / >/dev/null 2>&1; then
    pass "HDFS 可访问"
    info "HDFS 业务目录" "$(hdfs dfs -ls / 2>/dev/null | awk '{print $NF}' | grep -v '^/' | tr '\n' ' ')"
    for d in /ev-charging/ods /ev-charging/dwd /ev-charging/dws /ev-charging/ads /ev-charging/quality /ev-charging/forecast; do
      hdfs dfs -test -d "$d" 2>/dev/null || lack "HDFS 目录 $d" "hdfs dfs -mkdir -p $d"
    done
  else
    lack "HDFS 读写" "服务未起或安全模式：hdfs dfsadmin -safemode get / leave"
  fi
fi
info "监听端口" "$(ss -ltn 2>/dev/null | awk 'NR>1{print $4}' | grep -oE '(9000|8020|9870|8088|8032)$' | sort -u | tr '\n' ' ')"

echo "==================== 4. Spark ===================="
info "SPARK_HOME" "${SPARK_HOME:-未设置}"
sp_dir=""
for d in /opt/module/spark-* /usr/local/spark /usr/local/spark-*; do [ -d "$d" ] && sp_dir="$sp_dir $d"; done
info "发现的 Spark 目录" "${sp_dir:-无}"
if have spark-submit; then
  pass "spark-submit：$(spark-submit --version 2>&1 | grep -m1 -oE 'version [0-9.]+')"
  if [ -n "${SPARK_HOME:-}" ] && [ -f "$SPARK_HOME/conf/core-site.xml" ]; then
    pass "Spark 已拿到 Hadoop 配置（conf/core-site.xml 存在）"
  elif [ -n "${HADOOP_CONF_DIR:-}" ]; then
    pass "Spark 通过 HADOOP_CONF_DIR 读 Hadoop 配置"
  else
    lack "Spark 与 HDFS 打通" "cp $conf_dir/core-site.xml $conf_dir/hdfs-site.xml \$SPARK_HOME/conf/"
  fi
else
  lack "Spark" "未安装，老师镜像里是 /opt/module/spark-3.4.1，见 README.md 第 1 节"
fi

echo "==================== 5. Python / PySpark ===================="
if have python3; then
  pv="$(python3 -V 2>&1)"; info "python3" "$pv"
  case "$pv" in
    *3.8*|*3.9*|*3.10*|*3.11*) pass "Python 版本与 PySpark 3.4/3.5 兼容" ;;
    *) lack "Python 版本" "$pv 与 PySpark 3.5 不兼容（需 3.8–3.11），见 README.md 第 2 节" ;;
  esac
else
  lack "python3" "未安装，见 README.md 第 2 节"
fi
have pip3 && info "pip3" "$(pip3 -V 2>&1 | cut -d' ' -f1-2)" || lack "pip3" "sudo apt install python3-pip"
if have python3; then
  for m in pyspark flask pandas pyarrow; do
    python3 -c "import $m" >/dev/null 2>&1 && pass "python 模块 $m" || lack "python 模块 $m" "pip3 install $m（或见 README.md 第 2 节）"
  done
fi

echo "==================== 6. 前端工具链 ===================="
have node && info "node" "$(node -v)" || lack "node" "老师未提供，见 README.md 第 3 节（或用老师给的 HBuilderX）"
have npm && info "npm" "$(npm -v)" || true
have node && case "$(node -v)" in v1[89]*|v2[0-9]*) pass "Node 主版本可用（>=18）" ;; *) lack "Node 版本" "$(node -v) 低于 18，Vite 跑不起来" ;; esac

echo "==================== 7. 其他（可选加分） ===================="
ls -d /opt/pycharm* "$HOME"/pycharm* /snap/pycharm-community 2>/dev/null | head -1 | grep -q . \
  && pass "PyCharm 已安装" || lack "PyCharm" "老师步骤第 2 条，见 README.md 第 4 节（也可装在宿主机）"
if [ -d /mnt/hgfs/BIT-SummerTermProject ]; then
  if touch /mnt/hgfs/BIT-SummerTermProject/.hgfs-write-test 2>/dev/null; then
    rm -f /mnt/hgfs/BIT-SummerTermProject/.hgfs-write-test
    pass "共享文件夹可读写（/mnt/hgfs/BIT-SummerTermProject）"
  else
    lack "共享文件夹可写" "VMware 里勾选 Shared Folders 的 write 权限"
  fi
else
  lack "共享文件夹挂载" "vmware-hgfsclient 查看，重新挂载 /mnt/hgfs"
fi
for url in https://pypi.org/simple/ https://registry.npmjs.org/ https://archive.apache.org/dist/spark/; do
  if curl -sI -m 6 "$url" >/dev/null 2>&1; then pass "外网可达 $url"; else lack "外网 $url" "不通：改用离线包经共享文件夹传入"; fi
done

echo
echo "==================== 结论 ===================="
echo "  通过 $ok 项，缺失 $bad 项"
if [ "$bad" -gt 0 ]; then
  printf '  缺失清单：%b\n' "$miss"
  echo "  安装命令见 /mnt/hgfs/BIT-SummerTermProject/scripts/part2/README.md"
else
  echo "  环境齐备，可以开始跑第二阶段作业"
fi
