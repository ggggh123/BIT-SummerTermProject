#!/usr/bin/env bash
set -euo pipefail

# Resolve only the host toolchain, even if the caller has activated a Python environment.
PATH=/usr/bin:/bin
export PATH
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH

missing=0
for command_name in git cmake ninja qmake6 qtpaths6 python3 node npm pkg-config; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'MISSING %s\n' "$command_name"
    missing=1
  fi
done

for qt_module in Qt6Core Qt6Network Qt6Widgets Qt6WebEngineWidgets Qt6Charts Qt6Test; do
  if ! pkg-config --atleast-version=6.2 "$qt_module"; then
    printf 'MISSING %s >= 6.2\n' "$qt_module"
    missing=1
  fi
done

python3 -c 'import pytest, numpy, pandas, sklearn, joblib' 2>/dev/null || {
  printf 'MISSING python-ml-dependencies\n'
  missing=1
}

exit "$missing"
