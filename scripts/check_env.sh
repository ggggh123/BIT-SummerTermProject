#!/bin/sh
# POSIX 兼容：dash(sh) 不支持 pipefail，且本脚本没有管道依赖，用 set -eu 即可。
set -eu

# Resolve only the host toolchain, even if the caller has activated a Python environment.
PATH=/usr/bin:/bin
export PATH
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH

usage() {
  printf '%s\n' \
    'Usage: scripts/check_env.sh [--with-web] [--with-ml]' \
    '  --with-web  also check optional Web tools (node and npm)' \
    '  --with-ml   also check optional ML Python dependencies' \
    '  --help      show this help message'
}

with_web=0
with_ml=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --with-web)
      with_web=1
      ;;
    --with-ml)
      with_ml=1
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
  shift
done

missing=0
for command_name in git cmake make g++ qmake6 qtpaths6 python3 pkg-config; do
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

python3 -c 'import pytest' 2>/dev/null || {
  printf 'MISSING pytest\n'
  missing=1
}

if [ "$with_web" -eq 1 ]; then
  for command_name in node npm; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
      printf 'MISSING %s\n' "$command_name"
      missing=1
    fi
  done
fi

if [ "$with_ml" -eq 1 ]; then
  python3 -c 'import numpy, pandas, sklearn, joblib' 2>/dev/null || {
    printf 'MISSING python-ml-dependencies\n'
    missing=1
  }
fi

exit "$missing"
