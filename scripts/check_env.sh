#!/bin/sh
set -eu
PATH=/usr/bin:/bin
export PATH
unset VIRTUAL_ENV PYTHONHOME PYTHONPATH
if ! command -v python3 >/dev/null 2>&1; then
  printf '%s\n' '[MISSING] python3；请先运行 bash scripts/bootstrap.sh' >&2
  exit 1
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$script_dir/check_env.py" "$@"
