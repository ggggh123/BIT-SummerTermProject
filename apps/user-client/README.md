# Qt 用户端

## 前置环境

- CMake 3.25+、Ninja 和支持 C++17 的编译器。
- Qt 6.2+，包含 Core、Gui、Widgets、Network、WebEngineWidgets 和 Test 组件。
- Node.js 18+，仅用于本地导航 HTML 合同测试。
- 运行中的项目 Qt 服务端，以及本机自行申请的腾讯地图 WebService/Web JavaScript API Key。

Key 只能放在本机环境变量或已忽略的 `config.local.ini`，不得提交。以下命令均从仓库根目录执行；可在该目录创建不含真实密钥的本地配置：

```ini
[server]
host=127.0.0.1
port=9100

[tencent]
mapKey=<your-local-tencent-map-key>
```

也可用 `EV_SERVER_HOST`、`EV_SERVER_PORT`、`EV_TENCENT_MAP_KEY` 覆盖同名配置。

## 构建、运行与测试

```bash
cmake --preset debug
cmake --build --preset debug
./build/debug/apps/user-client/ev_user_client
ctest --preset debug -R '^user_' --output-on-failure
node --test apps/user-client/tests/test_navigation_html.mjs
```

## 错误、离线缓存与地图降级

- `NOT_CONNECTED` / `TRANSPORT_ERROR` 表示服务端尚未连接或连接中断；`TIMEOUT` 表示请求在规定时间内没有收到响应；`INVALID_RESPONSE` 表示服务端响应不符合冻结协议。此类错误应先确认服务端和配置，再重试。
- 已登录请求收到 `AUTH_REQUIRED` 时，客户端会清除当前内存会话并返回登录页，提示“登录已失效，请重新登录”；`FORBIDDEN`、余额不足、充电桩状态冲突等其他业务拒绝不会被当作成功，也不会自动重复写操作。
- 每次开始新的登录尝试时立即清除旧账户、历史、站桩选择及地图路线缓存，取消旧请求的本地回调/重放。新账户必须等待自己的当前订单校验完成才能进入业务页；旧响应不得修改新账户余额或推动页面跳转。取消本地请求不代表撤销服务端已经执行的写操作。
- 地图超时、地址无结果或路线加载失败只影响地址解析/导航。可以重试；导航页会保留并明确标注上一次成功路线作为降级信息，附近站点列表仍以项目服务端响应为准。
- 成功的驾车/步行路线会自动调整地图视野，以包含全部路线点和起终点，并为底部状态面板留出边距；后续失败不会替换上次路线和视野。
- 断线后，附近站点、充电桩详情、历史订单和预测只显示最近一次成功解码的内存缓存，并持续显示红色离线提示。点击“重试连接”仅请求真实重连；收到已连接事件后才刷新站点、预测、当前订单和当页历史记录。
- 预约、开始、停止、结算、取消、充值和资料修改不会在重连时自动重发。程序退出后 token 与缓存均不落盘。

## 答辩演示

1. 使用手机号 `13800138000` 登录。
2. 起点选择 `北京理工大学中关村校区`，等待服务端返回附近站点。
3. 在返回结果中选择含空闲充电桩 ID `1001` 的站点，并由操作人现场确认响应中的站点名称和充电桩编码；不要预设站点名称。
4. 依次展示桩详情、腾讯导航、充电流程、历史订单，以及断线缓存和真实重连后的安全刷新。预测仅为可选项，未启用时显示“暂无预测”，不妨碍找站和导航。

## 显式在线地图冒烟（不依赖项目服务端）

该测试复用真实 `MainWindow` 和内嵌腾讯地图，通过本地 TCP 测试响应完成登录与站桩查询，腾讯地址解析、驾车/步行和恢复重试均访问真实服务。业务数据在界面中明确标注为模拟数据，**不能作为真实服务端、数据库或充电结算联调证据**。

```bash
cmake --build --preset debug --target user_map_online_smoke
# 先通过已忽略的 config.local.ini 或 EV_TENCENT_MAP_KEY 配置本机 Key。
EV_MAP_SMOKE_OUTPUT_DIR="$PWD/runtime/map-smoke" \
  ./build/debug/apps/user-client/user_map_online_smoke
```

在可用的图形会话中运行；离屏自动化不替代截图检查。Linux 虚拟机可按实际环境使用 `QT_QPA_PLATFORM=xcb`。测试显式构建、显式运行，不加入默认 CTest，不自动消耗腾讯额度。单次正常执行包含一次地址解析、驾车/步行各一次、解除本地网络阻断后的驾车重试一次；SDK 底图资源请求不计入这一应用层次数。

结果、截图与真实后端联调待办见 [地图冒烟记录](../../docs/test/tencent-map-smoke.md) 和 [客户端收尾记录](../../docs/test/user-client-closeout-2026-09-05.md)。
