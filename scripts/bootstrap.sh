#!/bin/sh
set -eu

core_packages='git cmake ninja-build pkg-config qt6-base-dev qt6-base-dev-tools qt6-webengine-dev qt6-charts-dev python3-pytest'
web_optional_packages='nodejs npm'
ml_optional_packages='python3-numpy python3-pandas python3-sklearn python3-joblib'

printf '%s\n' '安装的是方便开发的全量开发超集：核心包，以及可选 Web 和可选 ML 包。'
printf '%s\n' 'Node/npm 和 ML 科学计算包是可选 profile，不是核心验收前置。'

sudo apt-get update
sudo apt-get install -y $core_packages $web_optional_packages $ml_optional_packages
