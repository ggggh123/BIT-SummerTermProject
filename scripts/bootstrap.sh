#!/bin/sh
# Ubuntu 22.04 官方 APT，全局安装；不升级发行版、不添加 PPA、不创建虚拟环境。
set -eu
core_packages='build-essential git cmake ninja-build pkg-config python3 python3-pytest qt6-base-dev qt6-base-dev-tools qt6-webengine-dev qt6-webengine-dev-tools libqt6webenginecore6-bin libqt6opengl6-dev libqt6svg6 libqt6sql6-sqlite libgl-dev libegl-dev libopengl-dev libxkbcommon-dev fonts-noto-cjk ca-certificates'
allow_other=0
print_packages=0
for arg in "$@"; do
  case "$arg" in
    --print-packages) print_packages=1 ;;
    --allow-other-ubuntu) allow_other=1 ;;
    --help)
      printf '%s\n' '用法: bash scripts/bootstrap.sh [--allow-other-ubuntu]' \
        '默认仅支持 Ubuntu 22.04，安装三端开发/测试所需的全局 APT 包。' \
        '--print-packages 只显示清单，不安装。Web/ML 工具不自动安装。'
      exit 0 ;;
    *) printf '未知参数: %s\n' "$arg" >&2; exit 2 ;;
  esac
done
if [ "$print_packages" -eq 1 ]; then printf '%s\n' "$core_packages"; exit 0; fi
if [ ! -r /etc/os-release ]; then
  printf '%s\n' '无法识别系统；请在 Ubuntu 22.04 中执行。' >&2; exit 1
fi
. /etc/os-release
if [ "${ID:-}" != ubuntu ]; then
  printf '%s\n' '此安装入口只支持 Ubuntu，不会尝试修改其他发行版。' >&2; exit 1
fi
if [ "${VERSION_ID:-}" != 22.04 ] && [ "$allow_other" -ne 1 ]; then
  printf '当前 Ubuntu %s 不是团队基线 22.04；未执行安装。\n' "${VERSION_ID:-unknown}" >&2
  printf '%s\n' '其他 Ubuntu 主机如需使用本机源安装，请显式加 --allow-other-ubuntu；这不等于 22.04 兼容验证。' >&2
  exit 1
fi
printf '%s\n' '安装三端核心依赖，不安装 Qt Charts、Node/npm 或 ML 科学计算包。'
if [ "$(id -u)" -eq 0 ]; then
  apt-get update
  apt-get install -y --no-install-recommends $core_packages
else
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends $core_packages
fi
