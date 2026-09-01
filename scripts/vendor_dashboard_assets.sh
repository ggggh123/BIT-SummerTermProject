#!/usr/bin/env bash
set -euo pipefail
asset_cache="dashboard/.vendor-cache"
npm install --prefix "$asset_cache" --no-save echarts@5.6.0
cp "$asset_cache/node_modules/echarts/dist/echarts.min.js" dashboard/vendor/echarts.min.js
test -s dashboard/vendor/echarts.min.js
