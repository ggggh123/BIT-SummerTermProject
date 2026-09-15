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
        '安装三端开发/测试所需的全局 APT 包。团队验证基线为 Ubuntu 22.04；' \
        '其他 Ubuntu 版本自动按尽力兼容模式继续（输出警告，不冒充基线验证）。' \
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
if [ "${VERSION_ID:-}" != 22.04 ]; then
  # 22.04 是团队验证基线；其他 Ubuntu（如 25.04）自动继续安装（尽力兼容），
  # 但明确告知这不等于 22.04 兼容验证。--allow-other-ubuntu 保留为兼容参数。
  printf '当前 Ubuntu %s 不是团队验证基线 22.04；按尽力兼容模式继续安装。\n' "${VERSION_ID:-unknown}" >&2
  printf '%s\n' '注意：在非 22.04 系统上编译通过不等于团队 22.04 基线验证；如遇 Qt API/包名差异请参照 docs/development/ubuntu22.md 与环境兼容性文档。' >&2
fi
printf '%s\n' '安装三端核心依赖，不安装 Qt Charts、Node/npm 或 ML 科学计算包。'
if [ "$(id -u)" -eq 0 ]; then
  apt-get update
  apt-get install -y --no-install-recommends $core_packages
else
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends $core_packages
fi
