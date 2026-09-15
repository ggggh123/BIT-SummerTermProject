#!/usr/bin/env bash
# =============================================================================
# 51-run-prl.sh —— 跑 PRL 质量检测 + 清洗（老师要求 3、4 步）
# =============================================================================
# 与官方脚本的关系
# ----------------
# 官方入口是 `part2/scripts/run_pipeline.sh`，但它 `source /usr/local/ev-part2/env.sh`
# 并把 HDFS 写死成 `hdfs://localhost:8020`，在没有全局运行时（没有 sudo）的成员机上跑不起来。
# 本脚本用 50-install-shims.sh 装的 shim 走同一条 Spark 作业与同一组契约/策略文件，
# 只把「解释器 / HDFS 地址 / 输出根」参数化，并且直接吃**正式的** `$HDFS_ROOT/ods`，
# 不生成测试夹具（那个用 `run_pipeline.sh` 不带参数即可）。
#
# 产出（HDFS）：
#   $HDFS_ROOT/quality/batches/<run-id>/{dwd/, reports/, RUN_SUCCEEDED.json}
# 注意：本脚本**不**动正式 `$HDFS_ROOT/dwd`，发布与注册交给 52-publish-dwd.sh，
# 这样清洗失败时不会污染队友的数仓输入。
#
# 用法：
#   bash scripts/part2/51-run-prl.sh                      # 校验 handoff/ods 后清洗
#   bash scripts/part2/51-run-prl.sh --runs 2             # 重复跑两次（演示可复现性）
#   bash scripts/part2/51-run-prl.sh --extra --accept-draft
#        ↑ `--extra` 之后的参数原样透传给 part2/scripts/spark_quality_clean.py
#          （Q2/Q5/Q6/Q9 清洗策略尚未冻结时，官方跑法需要 --accept-draft）
#
# 可选环境变量：
#   PART2_VENV     默认 ~/venvs/part2          （必须能 import pyspark）
#   EV_SHIM_DIR    默认 ~/ev-shim              （50-install-shims.sh 的安装位置）
#   HDFS_ROOT      默认 /ev-charging
#   HDFS_AUTHORITY 默认用 `hdfs getconf -confKey fs.defaultFS` 推导
#   ODS_DIR        默认 <repo>/handoff/ods     （本地 ODS 交接包，用于校验与首次上传）
# =============================================================================
set -uo pipefail

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SELF_DIR/../.." && pwd)"
cd "$REPO_ROOT" || exit 1

VENV="${PART2_VENV:-$HOME/venvs/part2}"
SHIM_DIR="${EV_SHIM_DIR:-$HOME/ev-shim}"
HDFS_ROOT="${HDFS_ROOT:-/ev-charging}"
ODS_DIR="${ODS_DIR:-$REPO_ROOT/handoff/ods}"
WORK="${PART2_WORK:-$REPO_ROOT/runtime/prl-evidence}"
RUNS=1
EXTRA=()

while [ $# -gt 0 ]; do
  case "$1" in
    --runs) RUNS="${2:-1}"; shift 2 ;;
    --extra) shift; EXTRA=("$@"); break ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "[warn] 未知参数：$1"; shift ;;
  esac
done

# ---------- 0) 环境 ----------
export PATH="$SHIM_DIR:$PATH"
export PYSPARK_PYTHON="$VENV/bin/python"
export PYSPARK_DRIVER_PYTHON="$VENV/bin/python"
mkdir -p "$WORK"

if ! command -v "${SPARK_SUBMIT:-spark-submit}" >/dev/null 2>&1; then
  echo "[fail] PATH 里没有 spark-submit。两种常见原因："
  echo "       ① 还没装 shim  → 先跑 scripts/part2/50-install-shims.sh"
  echo "       ② 非 login shell（PATH 没有 JDK/Hadoop/Spark）→ 用 bash -l 跑本脚本"
  exit 1
fi
if ! command -v hdfs >/dev/null 2>&1; then
  echo "[fail] PATH 里没有 hdfs —— 非 login shell 的常见现象，用 bash -l 跑本脚本"
  exit 1
fi
AUTHORITY="${HDFS_AUTHORITY:-$(hdfs getconf -confKey fs.defaultFS 2>/dev/null)}"
if [ -z "$AUTHORITY" ]; then
  echo "[fail] 推导不出 HDFS 地址，用 HDFS_AUTHORITY=hdfs://<主机名>:8020 指定"
  exit 1
fi
ODS_URI="$AUTHORITY$HDFS_ROOT/ods"

echo "===== PRL 清洗（Spark on YARN）====="
echo "  仓库     : $REPO_ROOT"
echo "  venv     : $VENV"
echo "  输入     : $ODS_URI"
echo "  输出根   : $AUTHORITY$HDFS_ROOT/quality/batches/"
echo

# ---------- 1) 校验本地 ODS 交接包（有才校验；HDFS 上已有则不要求本地存在） ----------
SPEC="$WORK/input-spec.json"
if [ -f "$ODS_DIR/manifest.json" ]; then
  echo "[1/4] 校验本地 ODS 交接包：$ODS_DIR"
  "$VENV/bin/python" -m part2.scripts.verify_handoff "$ODS_DIR" --output "$SPEC" --require-full 2>&1 | tail -6
  rc=${PIPESTATUS[0]}
  echo "      verify_handoff rc=$rc"
  [ "$rc" -ne 0 ] && echo "[warn] 交接包未通过全量校验，继续但请留意上面输出"
else
  echo "[1/4] 本地没有 $ODS_DIR/manifest.json，跳过本地校验（以 HDFS 上的 ODS 为准）"
fi

# ---------- 2) 打包 PySpark 代码 ----------
BUNDLE="$WORK/part2-code.zip"
echo "[2/4] 生成代码包：$BUNDLE"
"$VENV/bin/python" -m part2.scripts.build_python_bundle "$BUNDLE" 2>&1 | tail -3

# ---------- 3) 确认 HDFS 输入存在 ----------
if ! hdfs dfs -test -d "$HDFS_ROOT/ods"; then
  if [ -d "$ODS_DIR" ]; then
    echo "[3/4] HDFS 上没有 $HDFS_ROOT/ods，从 $ODS_DIR 上传"
    hdfs dfs -mkdir -p "$HDFS_ROOT/ods"
    hdfs dfs -put "$ODS_DIR"/* "$HDFS_ROOT/ods/" 2>&1 | tail -3
  else
    echo "[fail] HDFS 上没有 $HDFS_ROOT/ods，本地也没有 $ODS_DIR"
    exit 1
  fi
else
  echo "[3/4] HDFS 输入已存在：$ODS_URI"
  hdfs dfs -du -h -s "$HDFS_ROOT/ods" | sed 's/^/      /'
fi

# ---------- 4) 跑清洗作业 ----------
for i in $(seq 1 "$RUNS"); do
  RUN="prl-clean-$(date -u +%Y%m%dT%H%M%SZ)-$$"
  OUT="$AUTHORITY$HDFS_ROOT/quality/batches/$RUN"
  EVID="$WORK/$RUN"
  mkdir -p "$EVID/reports"
  echo
  echo "[4/4] 第 $i/$RUNS 次：run_id=$RUN"
  echo "      证据目录：$EVID"
  START=$(date +%s)
  if [ -f "$SPEC" ]; then MANIFEST_ARG=(--manifest "$SPEC"); else MANIFEST_ARG=(); fi
  "$SPARK_SUBMIT" --master yarn --deploy-mode client \
    --py-files "$BUNDLE" \
    part2/scripts/spark_quality_clean.py \
    --input "$ODS_URI" \
    --output "$OUT" \
    --run-id "$RUN" \
    "${MANIFEST_ARG[@]}" \
    --contract part2/contracts/ods-dwd-v0.1.json \
    --policy part2/contracts/quality-policy-v0.1.json \
    --reports "$EVID/reports" \
    "${EXTRA[@]}" 2>&1 | tail -25
  rc=${PIPESTATUS[0]}
  echo "      spark_quality_clean rc=$rc，耗时 $(( $(date +%s) - START )) 秒"
  [ "$rc" -ne 0 ] && { echo "[fail] 第 $i 次清洗失败"; exit "$rc"; }
  echo "      run_result.json："
  head -20 "$EVID/reports/run_result.json" 2>/dev/null | sed 's/^/      /'
  echo "$RUN" >"$WORK/last-run-id"
done

echo
echo "===== 完成 ====="
echo "  最近一次 run_id : $(cat "$WORK/last-run-id" 2>/dev/null)"
echo "  批次目录        : $AUTHORITY$HDFS_ROOT/quality/batches/$RUN"
echo "  下一步          : bash scripts/part2/52-publish-dwd.sh --run $(cat "$WORK/last-run-id" 2>/dev/null)"
