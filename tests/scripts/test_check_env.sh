#!/bin/sh
# 保留旧测试入口；行为测试改为受控夹具，不再要求测试宿主安装可选 ML/Web。
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec /usr/bin/python3 -m pytest "$repo_root/tests/scripts/test_environment_tools.py" -q
