#!/usr/bin/env bash
# 一键串联：生成 ODS -> 上传 HDFS -> 质量探查 -> 清洗 DWD -> 分层 DWS/ADS（Spark on YARN）
set -u
. /etc/profile.d/part2-env.sh 2>/dev/null || true
cd "$(dirname "$0")"
HOSTN=$(hostname)
export ODS_URI="hdfs://$HOSTN:8020/ev-charging/ods"
export DWD_URI="hdfs://$HOSTN:8020/ev-charging/dwd"
export DWS_URI="hdfs://$HOSTN:8020/ev-charging/dws"
export ADS_URI="hdfs://$HOSTN:8020/ev-charging/ads"
PY=${PYSPARK_PYTHON:-python3}
SUBMIT="spark-submit --master yarn --deploy-mode client --executor-memory 1g --driver-memory 1g"
echo "=== 1/5 生成 ODS（本地，确定性 seed=20260914）==="
$PY gen_ods.py | tail -14
echo "=== 2/5 上传 ODS 到 HDFS ==="
hdfs dfs -mkdir -p /ev-charging/ods
hdfs dfs -put -f handoff/ods/*.csv /ev-charging/ods/
echo "  已上传 $(hdfs dfs -ls /ev-charging/ods | grep -c csv) 个 CSV"
echo "=== 3/5 质量探查（老师第 3 步）==="
$SUBMIT quality_check.py 2>&1 | grep -E '画像|命中|注入' | tail -14
echo "=== 4/5 清洗到 DWD（老师第 4 步）==="
$SUBMIT clean_to_dwd.py 2>&1 | grep -E '写出|->|剔除' | tail -14
echo "=== 5/5 分层 DWS/ADS（老师第 6 步）==="
$SUBMIT build_warehouse.py 2>&1 | grep -E 'dws_|ads.db|导出' | tail -16
echo
echo "=== 产物 ==="
echo "  报告：$(pwd)/handoff/quality/   ADS：$(pwd)/handoff/ads/"
hdfs dfs -ls /ev-charging/ads | tail -6
