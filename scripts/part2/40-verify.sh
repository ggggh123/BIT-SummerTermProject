#!/usr/bin/env bash
# 新环境（JDK 17 + Hadoop 3.4.1 + Spark 3.5.7）验收：进程、HDFS 读写、Spark on YARN、SparkSQL
# 用法（普通用户即可，不需要 sudo）：
#   bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/40-verify.sh
set -u
. /etc/profile.d/part2-env.sh 2>/dev/null || true
HADOOP_DIR=${HADOOP_HOME:-/usr/local/hadoop}
HOSTN=$(hostname)
PY=${PYSPARK_PYTHON:-python3}
pass=0; fail=0
chk() { if echo "$2" | grep -q "$3"; then echo "  [OK]   $1"; pass=$((pass+1)); else echo "  [FAIL] $1  (实际: $2)"; fail=$((fail+1)); fi; }

echo "=== 1) 版本 ==="
chk "JDK 17"        "$(java -version 2>&1 | head -1)" "17"
chk "Hadoop 3.4.1"  "$(hadoop version 2>/dev/null | head -1)" "3.4.1"
chk "Spark 3.5.7"   "$(spark-submit --version 2>&1 | grep -m1 -i version)" "3.5.7"
chk "PySpark 3.5.7" "$($PY -c 'import pyspark;print(pyspark.__version__)' 2>/dev/null)" "3.5.7"

echo "=== 2) jps 五进程 ==="
JP=$(jps 2>/dev/null)
for p in NameNode DataNode SecondaryNameNode ResourceManager NodeManager; do chk "进程 $p" "$JP" "$p"; done

echo "=== 3) HDFS 读写 ==="
echo "smoke $(date +%F' '%T)" > /tmp/smoke.txt
chk "写入" "$(hdfs dfs -put -f /tmp/smoke.txt /ev-charging/ods/ >/dev/null 2>&1 && echo ok)" "ok"
chk "读回" "$(hdfs dfs -cat /ev-charging/ods/smoke.txt 2>/dev/null)" "smoke"
chk "安全模式" "$(hdfs dfsadmin -safemode get 2>/dev/null)" "OFF"

echo "=== 4) Spark on YARN 读 HDFS + SparkSQL ==="
$PY - <<PY 2>&1 | grep -E 'YARN|SparkSQL|Spark 版本' | sed 's/^/  /'
from pyspark.sql import SparkSession, functions as F
spark = (SparkSession.builder.master("yarn").appName("part2-verify")
         .config("spark.sql.shuffle.partitions", "8").getOrCreate())
spark.sparkContext.setLogLevel("ERROR")
print("YARN 读 HDFS 行数:", spark.read.text("hdfs://$HOSTN:8020/ev-charging/ods/smoke.txt").count())
spark.range(1000).withColumn("g", F.col("id") % 5).createOrReplaceTempView("t")
print("SparkSQL 聚合:", spark.sql("select g, count(*) c from t group by g order by g").collect())
print("Spark 版本:", spark.version)
spark.stop()
PY

echo "=== 5) YARN 应用记录 ==="
yarn application -list -appStates ALL 2>/dev/null | tail -3 | sed 's/^/  /'

echo "=== 6) Web UI =="
for u in "http://localhost:9870/" "http://localhost:8088/"; do
  code=$(wget -q -O /dev/null --server-response --spider "$u" 2>&1 | awk '/HTTP\//{c=$2} END{print c}')
  echo "  ${code:-fail}  $u"
done

echo
echo "通过 $pass 项，失败 $fail 项"
[ "$fail" -eq 0 ] && echo "验收 PASS" || echo "验收 FAIL，见《06》§8 排查"
