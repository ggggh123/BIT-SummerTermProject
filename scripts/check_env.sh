#!/usr/bin/env bash
set -euo pipefail

missing=0
for command_name in git cmake ninja qmake6 python3 node npm; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'MISSING %s\n' "$command_name"
    missing=1
  fi
done

python3 -c 'import pytest, numpy, pandas, sklearn, joblib' 2>/dev/null || {
  printf 'MISSING python-ml-dependencies\n'
  missing=1
}

exit "$missing"
