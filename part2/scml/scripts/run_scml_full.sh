#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

python3 data_generator/generator.py \
  --config config/part2_scml_full.yaml \
  --out handoff/ods

python3 scripts/validate_handoff.py handoff/ods
