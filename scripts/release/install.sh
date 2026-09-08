#!/bin/sh
# BIT-SummerTermProject 一键安装入口（由打包脚本注入安装包根目录）。
# 用法：解压安装包后在包根目录执行：
#   bash install.sh
# 等价于 python3 scripts/quickstart.py --start：
#   装依赖(apt) → 环境检查 → CMake 配置 → 全量编译 → 拉起三端演示窗口。
set -eu
if ! command -v python3 >/dev/null 2>&1; then
  printf '%s\n' '[MISSING] python3；Ubuntu 22.04 默认自带，如缺失请先执行：sudo apt install -y python3' >&2
  exit 1
fi
exec python3 scripts/quickstart.py --start "$@"
