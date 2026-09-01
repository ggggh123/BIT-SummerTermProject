#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install -y git cmake ninja-build pkg-config nodejs npm python3-venv \
  qt6-base-dev qt6-base-dev-tools qt6-webengine-dev qt6-charts-dev
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements-dev.txt
