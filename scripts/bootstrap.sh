#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo apt-get install -y git cmake ninja-build pkg-config nodejs npm \
  qt6-base-dev qt6-base-dev-tools qt6-webengine-dev qt6-charts-dev \
  python3-pytest python3-numpy python3-pandas python3-sklearn python3-joblib
