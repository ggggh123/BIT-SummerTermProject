#!/usr/bin/env bash
# 留存新批次，重复执行不会覆盖既有输入或输出。
set -euo pipefail
source /usr/local/ev-part2/env.sh
part2_repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$part2_repo"
part2_run="prl-smoke-$(date -u +%Y%m%dT%H%M%SZ)-$$"
part2_evidence="$EV_PART2_HOME/evidence/$part2_run"
part2_input="hdfs://localhost:8020/ev-charging/ods/_prl-smoke/$part2_run"
part2_output="hdfs://localhost:8020/ev-charging/quality/_smoke/$part2_run/profile"
mkdir -p "$part2_evidence"
ev-part2 python -m part2.scripts.make_fixture "$part2_evidence/input"
ev-part2 python -m part2.scripts.verify_handoff "$part2_evidence/input"
ev-part2 hdfs dfs -mkdir -p "$part2_input"
ev-part2 hdfs dfs -put "$part2_evidence"/input/* "$part2_input/"
ev-part2 spark-submit --master yarn --deploy-mode client part2/scripts/spark_hdfs_smoke.py \
  --input "$part2_input" --output "$part2_output" \
  --contract part2/contracts/ods-dwd-v0.1.json --manifest "$part2_evidence/input/manifest.json" \
  2>&1 | tee "$part2_evidence/spark.log"
echo "本次证据：$part2_evidence"
echo '该测试验证 YARN/HDFS/Python worker 和原始画像，不代表清洗模块或 DWD 已完成。'
