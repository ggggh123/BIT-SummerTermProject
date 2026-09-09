#!/bin/sh
# BIT-SummerTermProject 一键安装入口（由打包脚本注入安装包根目录）。
# 用法：解压安装包后在包根目录执行：
#   bash install.sh
# 等价于 python3 scripts/quickstart.py --start：
#   装依赖(apt) → 环境检查 → 编译三端 → 拉起三端演示窗口。
set -eu
installer_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -f "$installer_dir/scripts/quickstart.py" ]; then
  cd "$installer_dir"
elif [ -f "$installer_dir/../quickstart.py" ]; then
  cd "$installer_dir/../.."
else
  printf '%s\n' '[MISSING] scripts/quickstart.py；请保留完整源码安装包目录结构' >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  printf '%s\n' '[MISSING] python3；Ubuntu 22.04 默认自带，如缺失请先执行：sudo apt install -y python3' >&2
  exit 1
fi
exec python3 scripts/quickstart.py --start "$@"
