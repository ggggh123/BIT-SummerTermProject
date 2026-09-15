#!/usr/bin/env bash
# =============================================================================
# 50-install-shims.sh —— 在「没有全局运行时」的机器上装本机工具链 shim
# =============================================================================
# 背景
# ----
# 官方链路（part2/scripts/run_pipeline.sh、part2/scml/scripts/*）默认依赖全局运行时
# `/usr/local/ev-part2`（由 part2/scripts/install_global_runtime.sh + sudo 安装，
# 内含 JDK 8 + Python 3.10）。成员机上往往**没有 sudo 口令**，装不了那一套。
#
# 本脚本装两个 shim，把官方脚本对全局运行时的调用映射到本机既有工具链：
#   ~/ev-shim/ev-part2              把 `ev-part2 python|hdfs|spark-submit` 映射到
#                                   机器上已装好的 venv Python / Hadoop / Spark
#   ~/ev-shim/spark-submit-derby.sh 把 Spark driver 的 Derby 元数据库与 warehouse
#                                   落到本机磁盘（$HOME/derby、$HOME/ev-warehouse），
#                                   避免默认写在共享目录 /mnt/hgfs 上出现锁与权限问题
#
# 前置（本机已装好，见 scripts/part2/README.md 的版本基线）：
#   JDK 17 / Hadoop 3.4.1 / Spark 3.5.7 + 一个能 import pyspark 的 venv
#
# 用法：
#   bash scripts/part2/50-install-shims.sh
#   PART2_VENV=~/venv-py311 bash scripts/part2/50-install-shims.sh
#
# 装完之后，本目录的 51/52/53 会默认把 $HOME/ev-shim 放在 PATH 最前。
# =============================================================================
set -uo pipefail

SHIM_DIR="${EV_SHIM_DIR:-$HOME/ev-shim}"
VENV="${PART2_VENV:-$HOME/venvs/part2}"

echo "===== 安装本机工具链 shim ====="
echo "  shim 目录 : $SHIM_DIR"
echo "  venv      : $VENV"
echo

if [ ! -x "$VENV/bin/python" ]; then
  echo "[fail] 找不到 venv 解释器 $VENV/bin/python"
  echo "       用 PART2_VENV=<路径> 指定，或先建 venv 并 pip install pyspark"
  exit 1
fi
if ! "$VENV/bin/python" -c 'import pyspark' >/dev/null 2>&1; then
  echo "[fail] $VENV 里没有 pyspark（PySpark 3.5 不支持 Python 3.13，注意解释器版本）"
  exit 1
fi

mkdir -p "$SHIM_DIR"

# ---------- shim 1：把 ev-part2 的调用映射到本机工具链 ----------
cat >"$SHIM_DIR/ev-part2" <<EOF
#!/usr/bin/env bash
# 由 scripts/part2/50-install-shims.sh 生成，请勿手改。
set -euo pipefail
export PYSPARK_PYTHON="$VENV/bin/python"
export PYSPARK_DRIVER_PYTHON="$VENV/bin/python"
case "\${1:-help}" in
  python) shift; exec "$VENV/bin/python" "\$@" ;;
  hdfs|hadoop|yarn|spark-submit|pyspark|spark-sql|java|javac|jps) exec "\$@" ;;
  start|stop|status) echo "shim ev-part2: \$1 -> 本机请用 start-dfs.sh / start-yarn.sh" >&2; exit 0 ;;
  configure) echo "shim ev-part2: configure 跳过（本机工具链已装好）" >&2; exit 0 ;;
  *) echo "shim ev-part2: 未知命令 \$1" >&2; exit 2 ;;
esac
EOF

# ---------- shim 2：Derby 与 warehouse 落本机磁盘 ----------
cat >"$SHIM_DIR/spark-submit-derby.sh" <<'EOF'
#!/usr/bin/env bash
# 由 scripts/part2/50-install-shims.sh 生成，请勿手改。
# 把 Spark driver 的 Derby 元数据库与 warehouse 落到本机磁盘：
# 默认 CWD 常是共享目录（/mnt/hgfs），Derby 在那里会出现锁与权限问题。
set -euo pipefail
mkdir -p "$HOME/derby" "$HOME/ev-warehouse"
exec spark-submit \
  --conf "spark.driver.extraJavaOptions=-Dderby.system.home=$HOME/derby -Dderby.stream.error.file=$HOME/derby/derby.log" \
  --conf "spark.sql.warehouse.dir=file://$HOME/ev-warehouse" \
  "$@"
EOF

chmod +x "$SHIM_DIR/ev-part2" "$SHIM_DIR/spark-submit-derby.sh"
echo "[ok] $SHIM_DIR/ev-part2"
echo "[ok] $SHIM_DIR/spark-submit-derby.sh"
echo

echo "===== 自检 ====="
"$SHIM_DIR/ev-part2" python -c 'import sys, pyspark; print("  python", sys.version.split()[0], "| pyspark", pyspark.__version__)'
# 非 login shell 的 PATH 里通常没有 JDK/Hadoop/Spark（它们来自 /etc/profile.d），
# 这一条会"找不到命令"——用 `bash -l` 跑本脚本即可。
if command -v hdfs >/dev/null 2>&1; then
  hdfs version 2>/dev/null | head -1 | sed 's/^/  /'
else
  echo "  [warn] PATH 里没有 hdfs —— 非 login shell 的常见现象，用 bash -l 重跑本脚本"
fi
if command -v spark-submit >/dev/null 2>&1; then
  spark-submit --version 2>&1 | grep -m1 'version' | sed 's/^/  /'
else
  echo "  [warn] PATH 里没有 spark-submit —— 同上，用 bash -l"
fi
echo
echo "下一步：bash scripts/part2/51-run-prl.sh"
