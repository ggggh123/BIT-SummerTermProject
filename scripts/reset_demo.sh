#!/usr/bin/env bash
set -eu
root="$(dirname "$(dirname "$(readlink -f "$0")")")"
exec python3 "$root/scripts/demo_cli.py" reset "$@"
