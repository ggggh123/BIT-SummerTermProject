# 腾讯地图集成导航冒烟记录

日期：2026-09-05（Asia/Shanghai）。代码基线：`dev@261c9fa`；客户端收尾分支：`fix/user-client-session-closeout`。

环境：VMware Ubuntu 25.04、Qt 6.8.3、真实图形会话（xcb）、固定页面 `qrc:/map/navigation.html`。

状态：**客户端界面 + 腾讯在线服务通过；真实项目服务端联调仍待执行。**

## 验证边界

- 使用生产 `MainWindow`、`UserApi`、TCP 编解码、`NearbyPage`、`TencentMapClient`、`NavigationPage` 和仓库 qrc 页面。不是独立浏览器地图样例。
- 登录、当前订单与站桩数据来自测试程序内的本地 TCP 响应；站点名称明确标注“地图冒烟测试站（模拟业务数据）”。未连接队员的 Qt 服务端，未写入项目 SQLite。
- 起点为“北京理工大学中关村校区”；腾讯真实地址解析返回约 `39.9581, 116.313`，精确结果由客户端消费，坐标摘要记录于测试日志。测试目的地为 `39.969, 116.319`，不是实际运营充电站。
- 地址解析、驾车/步行规划、解除阻断后的驾车重试使用真实腾讯服务；Key 仅在本机运行环境注入，不写入源码、记录或截图。
- 测试站禁用预测，客户端显示“暂无预测”，不会为此调用预测接口，证明本场景不依赖 ML。

## 结果与截图

| 步骤 | 结果 | 证据 |
|---|---|---|
| 通过实际登录控件进入附近站点；发起真实腾讯地址解析 | 通过 | [地址解析](evidence/user-closeout-2026-09-05/01-geocode.png) |
| 选择测试站，显示站点、价格、总桩/空闲桩、暂无预测、充电桩 1001 | 通过；业务响应为本地测试数据 | [站桩详情](evidence/user-closeout-2026-09-05/02-station-detail.png) |
| 在 QWebEngineView 中显示腾讯驾车路线、起终点、底图 | 通过，已检查截图；视野包含完整路线 | [驾车](evidence/user-closeout-2026-09-05/03-driving.png) |
| 切换步行，使用腾讯步行服务，路线变化 | 通过，已检查与驾车不同的路线；原生/JS 缓存均为 walking | [步行](evidence/user-closeout-2026-09-05/04-walking.png) |
| 本地网络层阻断后再次请求驾车 | 通过：明确失败、重试按钮可用、保留上次成功步行路线 | [失败与缓存](evidence/user-closeout-2026-09-05/05-network-failure-cache.png) |
| 解除阻断，点击原生重试按钮 | 通过：重新获得真实驾车路线，隐藏重试按钮 | [恢复](evidence/user-closeout-2026-09-05/06-retry-success.png) |

在线测试日志：[online-smoke.txt](evidence/user-closeout-2026-09-05/online-smoke.txt)。QtTest 结果为一个业务测试用例通过，另含初始化/清理共 `3 passed, 0 failed`。日志中的“网络请求失败”是第五步主动阻断的预期现象，不是无故在线失败。

运行时 VMware/Qt 出现过 GBM/dma_buf 诊断；这不能替代结果判定。本次已在等待异步底图绘制后检查最终截图，底图、路线与起终点实际可见。测试成功提示出现得早于底图完成绘制，不能只凭文字成功就截取空白地图作为验收证据。

## 复现

从仓库根目录、可用图形会话运行：

```bash
cmake --preset debug
cmake --build --preset debug --target user_map_online_smoke
# 事先配置本机 Key，命令中不粘贴真实 Key。
EV_MAP_SMOKE_OUTPUT_DIR="$PWD/runtime/map-smoke" \
  ./build/debug/apps/user-client/user_map_online_smoke
```

此目标不参与默认构建或 CTest；每次运行都会消耗少量真实腾讯调用。输出目录保留 6 张截图；重新运行时建议使用新的目录，以区分每次证据。复核记录时用 `git rev-parse HEAD` 标识待交付版本，不能将本记录当作同一提交双彩排证据。

## 尚未关闭的集成项

- 使用真实 Qt 服务端完成登录、查站、查桩，再进入同一腾讯导航界面。
- 按核心验收清单跑预约至结算，核对真实订单、余额和遥测。
- 在正式演示版本上复测 Key、网络和 VMware 图形环境；本次在线通过不保证答辩当天外部服务可用。

## 历史记录说明

2026-09-02 的本文件曾记录 `PENDING`，当时工作树未注入可用 Key，仅有资源级成功与离线测试证据。该历史原因不代表 9 月 5 日 Key 或配额仍不可用。本次补充了实际界面的在线证据；离线测试、在线地图冒烟、真实后端 E2E 仍分别记录。

路线视野使用的官方 API 依据：[Map.fitBounds / FitBoundsOptions](https://lbs.qq.com/webApi/javascriptGL/glDoc/docIndexMap#3)、[LatLngBounds](https://lbs.qq.com/webApi/javascriptGL/glDoc/glDocClass#2)。
