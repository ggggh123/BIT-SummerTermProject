#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOCAL_ODS="${1:-$ROOT_DIR/handoff/ods}"
HDFS_ODS="${2:-/ev-charging/ods}"

if ! command -v hdfs >/dev/null 2>&1; then
  echo "hdfs command not found. Source /etc/profile.d/ev-second-project.sh first." >&2
  exit 1
fi

test -f "$LOCAL_ODS/manifest.json"
test -f "$LOCAL_ODS/injection_log.json"

hdfs dfs -mkdir -p "$HDFS_ODS"
hdfs dfs -rm -r -f "$HDFS_ODS"/*
hdfs dfs -put "$LOCAL_ODS"/* "$HDFS_ODS"/
hdfs dfs -touchz "$HDFS_ODS/_SUCCESS"
hdfs dfs -ls "$HDFS_ODS"
