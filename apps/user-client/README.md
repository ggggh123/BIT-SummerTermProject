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
port=9200

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
- 地图超时、地址无结果或路线加载失败只影响地址解析/导航。可以重试；导航页会保留并明确标注上一次成功路线作为降级信息，附近站点列表仍以项目服务端响应为准。
- 断线后，附近站点、充电桩详情、历史订单和预测只显示最近一次成功解码的内存缓存，并持续显示红色离线提示。点击“重试连接”仅请求真实重连；收到已连接事件后才刷新站点、预测、当前订单和当页历史记录。
- 预约、开始、停止、结算、取消、充值和资料修改不会在重连时自动重发。程序退出后 token 与缓存均不落盘。

## 答辩演示

1. 使用手机号 `13800138000` 登录。
2. 起点选择 `北京理工大学中关村校区`，等待服务端返回附近站点。
3. 在返回结果中选择含空闲充电桩 ID `1001` 的站点，并由操作人现场确认响应中的站点名称和充电桩编码；不要预设站点名称。
4. 依次展示预测排序与来源、桩详情、充电流程、历史订单，以及断线缓存和真实重连后的安全刷新。
