#!/usr/bin/env bash
# 默认仅生成 PRL 测试夹具；正式运行额外传 --require-full-input。
set -euo pipefail
source /usr/local/ev-part2/env.sh
part2_repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$part2_repo"
part2_run="prl-clean-$(date -u +%Y%m%dT%H%M%SZ)-$$"
part2_evidence="$EV_PART2_HOME/evidence/$part2_run"
part2_input="hdfs://localhost:8020/ev-charging/ods/_prl-clean/$part2_run"
part2_output="hdfs://localhost:8020/ev-charging/quality/batches/$part2_run"
mkdir -p "$part2_evidence"
if [[ $# -gt 0 && "$1" != --* ]]; then
  part2_source="$(realpath -- "$1")"
  shift
else
  part2_source="$part2_evidence/input"
  ev-part2 python -m part2.scripts.make_fixture "$part2_source"
fi
part2_verify_args=()
part2_spark_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --require-full-input)
      part2_verify_args+=(--require-full)
      ;;
    *)
      part2_spark_args+=("$1")
      ;;
  esac
  shift
done
ev-part2 python -m part2.scripts.verify_handoff "$part2_source" \
  --output "$part2_evidence/input-spec.json" "${part2_verify_args[@]}"
ev-part2 python -m part2.scripts.build_python_bundle "$part2_evidence/part2-code.zip"
ev-part2 hdfs dfs -mkdir -p "$part2_input"
ev-part2 hdfs dfs -put "$part2_source"/* "$part2_input/"
ev-part2 spark-submit --master yarn --deploy-mode client \
  --py-files "$part2_evidence/part2-code.zip" part2/scripts/spark_quality_clean.py \
  --input "$part2_input" --output "$part2_output" --run-id "$part2_run" \
  --manifest "$part2_evidence/input-spec.json" --contract part2/contracts/ods-dwd-v0.1.json \
  --policy part2/contracts/quality-policy-v0.1.json --reports "$part2_evidence/reports" \
  "${part2_spark_args[@]}" \
  2>&1 | tee "$part2_evidence/spark.log"
ev-part2 hdfs dfs -put "$part2_evidence/reports" "$part2_output/"
ev-part2 hdfs dfs -put "$part2_evidence/reports/run_result.json" "$part2_output/RUN_SUCCEEDED.json"
ev-part2 python -m part2.scripts.export_dwd --batch "$part2_output" --directory "$part2_evidence/dwd-handoff"
echo "本次报告：$part2_evidence/reports/quality_report.md"
echo "本地试交接包：$part2_evidence/dwd-handoff"
echo "HDFS 独立试跑批次：$part2_output"
echo '契约仍待确认；未发布到正式 /ev-charging/dwd，不会替换队友的数仓输入。'
