#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

spark-submit \
  --master yarn \
  warehouse/jobs/build_dim_date.py \
  --start "${1:-2026-09-01}" \
  --days "${2:-90}" \
  --out "${3:-/ev-charging/dwd/dim_date}"
