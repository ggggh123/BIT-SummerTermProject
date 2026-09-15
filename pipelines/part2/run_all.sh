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
echo "=== 1/8 生成 ODS ==="
# 权威入口是 #4 的 SCML 生成器（分支 feat/part2_SCML，合并后本目录可见）：
#   python3 part2/scml/data_generator/generator.py --config part2/scml/config/part2_scml_sample.yaml --out handoff/ods
#   python3 part2/scml/scripts/validate_handoff.py handoff/ods
# 注意：他的 ODS 是「每表一个目录 + _SUCCESS + sha256 manifest，表名带 ods_ 前缀」，
# 而本目录的 quality_check.py / clean_to_dwd.py 仍按 <table>.csv 读取——**切换入口时这两步要由 #3/#4 同步改**，
# 否则下游会读不到数据。在他那边接管前，这里暂时用我方的 1/10 规模生成器保持链路可跑。
if [ -f ../part2/scml/data_generator/generator.py ]; then
  echo "  检测到 #4 的 SCML 生成器，但下游读取口径尚未切换，暂不自动调用（见脚本内注释）"
fi
$PY gen_ods.py | tail -14
echo "=== 2/7 上传 ODS 到 HDFS ==="
hdfs dfs -mkdir -p /ev-charging/ods
hdfs dfs -put -f handoff/ods/*.csv /ev-charging/ods/
echo "  已上传 $(hdfs dfs -ls /ev-charging/ods | grep -c csv) 个 CSV"
echo "=== 3/7 质量探查（老师第 3 步）—— 属 #3 模块，不在本目录 ==="
# 原参考骨架 quality_check.py 已按分工撤下，实现以 #3（PRL）为准。
# 产出约定：quality_report.json（含 10 类问题检出与注入对账）→ 大屏 /api/quality/summary
echo "  [跳过] 请运行 #3 的质量作业；本目录仅保留对账与接口导出"
echo "=== 4/7 清洗到 DWD（老师第 4 步）—— 属 #3 模块，不在本目录 ==="
# 原本参考骨架 clean_to_dwd.py 已撤下。产出约定：DWD 落 /ev-charging/dwd（Parquet）
echo "  [跳过] 请运行 #3 的清洗作业；下游分层依赖 /ev-charging/dwd"
echo "=== 5/7 分层 DWS/ADS（老师第 6 步）==="
$SUBMIT build_warehouse.py 2>&1 | grep -E 'dws_|ads.db|导出' | tail -16
echo "=== 5.5/7 ADS 口径对齐 #4 的 ads_schema.sql ==="
$SUBMIT align_to_scml_ads.py 2>&1 | grep -E 'ads_|对齐' | tail -10
echo "=== 6/7 逐层对账（《04》§3.4 硬指标）==="
$SUBMIT reconcile.py 2>&1 | grep -E '\[OK\]|\[FAIL\]|对账|全绿' | tail -20
echo "=== 7/7 导出全部接口 JSON ==="
$SUBMIT export_api.py 2>&1 | grep -E '\.json|导出' | tail -24
echo
echo "=== 产物 ==="
echo "  报告：$(pwd)/handoff/quality/   ADS：$(pwd)/handoff/ads/"
hdfs dfs -ls /ev-charging/ads | tail -6
