#!/usr/bin/env bash
# =============================================================================
# 52-publish-dwd.sh —— 发布 DWD 到正式路径 + 注册 Hive 外部表 + 修分区
# =============================================================================
# 为什么要有这一步（踩过的坑）
# --------------------------
# `part2/scml/scripts/run_dws_ads.sh` **不会**执行 `dwd_contract.sql`。如果只把
# DWD 传上 HDFS 就去跑 DWS/ADS，会直接报
#   TABLE_OR_VIEW_NOT_FOUND ev_charging.dwd_station_hourly
# 顺序必须是：
#   ① 把清洗批次的 dwd/ 发布到 $HDFS_ROOT/dwd
#   ② `spark-sql -f part2/scml/warehouse/sql/dwd_contract.sql`（建库 + 注册外部表）
#   ③ 对 4 张分区事实表 `MSCK REPAIR TABLE`（否则查出来是 0 行）
#   ④ 再跑 53-run-dws-ads.sh
# 本脚本把 ①②③ 串起来，并把各表行数打出来当交付证据。
#
# 用法：
#   bash scripts/part2/52-publish-dwd.sh --run prl-clean-20260915T024010Z-55661
#   bash scripts/part2/52-publish-dwd.sh                      # 用 51 记下的 last-run-id
#   bash scripts/part2/52-publish-dwd.sh --run <id> --skip-publish   # 只注册+修分区
#
# 注意：默认会**替换** `$HDFS_ROOT/dwd`（这正是"发布"的语义）。执行前会打印即将被替换的
# 路径与当前大小；想只看不写加 `--dry-run`。
# =============================================================================
set -uo pipefail

SELF_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SELF_DIR/../.." && pwd)"
cd "$REPO_ROOT" || exit 1

VENV="${PART2_VENV:-$HOME/venvs/part2}"
SHIM_DIR="${EV_SHIM_DIR:-$HOME/ev-shim}"
HDFS_ROOT="${HDFS_ROOT:-/ev-charging}"
WORK="${PART2_WORK:-$REPO_ROOT/runtime/prl-evidence}"
RUN=""
DRY=0
SKIP_PUBLISH=0

while [ $# -gt 0 ]; do
  case "$1" in
    --run) RUN="${2:-}"; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    --skip-publish) SKIP_PUBLISH=1; shift ;;
    -h|--help) sed -n '2,28p' "$0"; exit 0 ;;
    *) echo "[warn] 未知参数：$1"; shift ;;
  esac
done

export PATH="$SHIM_DIR:$PATH"
export PYSPARK_PYTHON="$VENV/bin/python"
export PYSPARK_DRIVER_PYTHON="$VENV/bin/python"

if ! command -v hdfs >/dev/null 2>&1; then
  echo "[fail] PATH 里没有 hdfs —— 非 login shell 的常见现象，用 bash -l 跑本脚本"
  exit 1
fi
if ! command -v spark-sql >/dev/null 2>&1; then
  echo "[fail] PATH 里没有 spark-sql —— 同上，用 bash -l；或先跑 50-install-shims.sh"
  exit 1
fi

AUTHORITY="${HDFS_AUTHORITY:-$(hdfs getconf -confKey fs.defaultFS 2>/dev/null)}"
[ -z "$AUTHORITY" ] && { echo "[fail] 推导不出 HDFS 地址，用 HDFS_AUTHORITY=... 指定"; exit 1; }
[ -z "$RUN" ] && RUN="$(cat "$WORK/last-run-id" 2>/dev/null)"
[ -z "$RUN" ] && { echo "[fail] 没有 run-id：用 --run <id> 指定，或先跑 51-run-prl.sh"; exit 1; }

BATCH="$AUTHORITY$HDFS_ROOT/quality/batches/$RUN"
DWD_URI="$AUTHORITY$HDFS_ROOT/dwd"
DERBY_OPTS="-Dderby.system.home=$HOME/derby -Dderby.stream.error.file=$HOME/derby/derby.log"

# 统一入口：SparkSQL 的 Derby 元数据必须落本机磁盘（共享目录上会出锁/权限问题）
sparksql() {
  spark-sql \
    --conf "spark.driver.extraJavaOptions=$DERBY_OPTS" \
    --conf "spark.sql.warehouse.dir=file://$HOME/ev-warehouse" "$@"
}

echo "===== 发布并注册 DWD ====="
echo "  run_id  : $RUN"
echo "  批次    : $BATCH"
echo "  正式路径: $DWD_URI"
echo

echo "[1/4] 确认批次存在"
if ! hdfs dfs -test -d "$HDFS_ROOT/quality/batches/$RUN/dwd"; then
  echo "[fail] 批次里没有 dwd/：$BATCH/dwd"
  hdfs dfs -ls "$HDFS_ROOT/quality/batches" 2>/dev/null | tail -5 | sed 's/^/      /'
  exit 1
fi
hdfs dfs -du -h -s "$HDFS_ROOT/quality/batches/$RUN/dwd" | sed 's/^/      /'

echo
echo "[2/4] 导出 DWD 交接包到 handoff/dwd（本地，供 #5 等下游读）"
if [ "$DRY" = "0" ]; then
  rm -rf "$REPO_ROOT/handoff/dwd"
  "$VENV/bin/python" -m part2.scripts.export_dwd --batch "$BATCH" --directory "$REPO_ROOT/handoff/dwd" 2>&1 | tail -4
  ls "$REPO_ROOT/handoff/dwd" 2>/dev/null | tr '\n' ' ' | sed 's/^/      /'
  echo
else
  echo "      (dry-run 跳过)"
fi

if [ "$SKIP_PUBLISH" = "0" ]; then
  echo "[3/4] 发布到正式 $DWD_URI"
  if hdfs dfs -test -d "$HDFS_ROOT/dwd"; then
    echo "      将被替换的当前内容："
    hdfs dfs -du -h -s "$HDFS_ROOT/dwd" | sed 's/^/        /'
  fi
  if [ "$DRY" = "1" ]; then
    echo "      (dry-run：不执行 rm/cp)"
  else
    hdfs dfs -rm -r -f "$HDFS_ROOT/dwd" >/dev/null 2>&1
    hdfs dfs -cp "$HDFS_ROOT/quality/batches/$RUN/dwd" "$HDFS_ROOT/dwd"
    hdfs dfs -ls "$HDFS_ROOT/dwd" | awk 'NR>1 {print "        " $NF}'
  fi
else
  echo "[3/4] --skip-publish：保留现有 $DWD_URI"
fi

echo
echo "[4/4] 注册 Hive 外部表（dwd_contract.sql）+ MSCK REPAIR 分区表"
if [ "$DRY" = "1" ]; then
  echo "      (dry-run 跳过)"
else
  sparksql -f part2/scml/warehouse/sql/dwd_contract.sql 2>&1 | tail -4
  sparksql -e "
USE ev_charging;
MSCK REPAIR TABLE dwd_order_detail;
MSCK REPAIR TABLE dwd_telemetry_detail;
MSCK REPAIR TABLE dwd_station_hourly;
MSCK REPAIR TABLE dwd_event;
SELECT 'dwd_order_detail' AS tbl, COUNT(1) AS rows FROM dwd_order_detail
UNION ALL SELECT 'dwd_telemetry_detail', COUNT(1) FROM dwd_telemetry_detail
UNION ALL SELECT 'dwd_station_hourly',   COUNT(1) FROM dwd_station_hourly
UNION ALL SELECT 'dwd_event',            COUNT(1) FROM dwd_event
UNION ALL SELECT 'dim_stations',         COUNT(1) FROM dim_stations
UNION ALL SELECT 'dim_chargers',         COUNT(1) FROM dim_chargers
UNION ALL SELECT 'dim_users',            COUNT(1) FROM dim_users
UNION ALL SELECT 'dim_date',             COUNT(1) FROM dim_date;
" 2>&1 | tail -14
fi

echo
echo "===== 完成 ====="
echo "  下一步：bash scripts/part2/53-run-dws-ads.sh"
