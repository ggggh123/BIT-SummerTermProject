# Qt 用户端收尾记录（2026-09-05）

负责人：#3 PRL。基线：`dev@261c9fa`。工作分支：`fix/user-client-session-closeout`。本记录覆盖客户端代码和测试，不修改服务端、模拟器、共享协议或需求矩阵。

## 本轮修复

1. **重新登录立即清理旧会话页面。** 旧实现只在 `AUTH_REQUIRED` 时通知主窗口清理；开始另一次登录时虽已更换 API 代次，旧账户页与路线缓存仍可能保留。现统一由 `UserApi::resetSession` 清除内存用户、token、请求关联、站点快照和相关状态，通过 `sessionReset` 清理主窗口各页面。会话过期仍单独显示过期提示。
2. **本地旧请求失效。** 新登录和过期都会取消全部旧请求的本地投递及安全读重放；迟到 TCP 回包无法更新新账户。此操作不撤销服务端已经执行的充值/结算，重登录仍须读取权威当前订单，不自动重复写操作。
3. **导航成功后完整显示路线。** 原地图没有按路线调整视野，路线可能只出现在角落。现在范围覆盖路线中间点及起终点，给底部状态面板预留边距；失败时保留上次路线与视野。

## 针对性证据

- `reloginClearsPagesBeforeAuthenticationCompletes`：旧账户页有数据时发起新登录，立即回登录页并清理资料、充值输入和站桩详情；旧资料回包不能覆盖新用户。修复前明确失败于 `profilePage != loginPage`，修复后通过。
- `expiredSessionLateResponsesCannotAdvanceReloginGuard`：失效后登录新账户，通过真实 TCP 注入旧当前订单、充值、历史、鉴权错误；以同连接 health 回包作为处理屏障，断言余额和身份不变、旧结果不投递、页面仍等待新账户自己的 `order.current`。
- `sessionExpiryClearsNavigationCacheBeforeRelogin` 两个数据行：覆盖 AUTH_REQUIRED 后重登录，以及没有鉴权错误的直接重登录。新增行修复前明确失败于地图 resetCount 为 0；修复后路线、端点及重试缓存被清理。
- Node 新回归：路线视野必须包含中间绕行点与起终点，且后续失败不得重新定位。修复前没有任何 fitBounds 调用而失败；修复后通过。
- 7 处旧测试手造 `selectionGeneration=3` 改为使用真实 `chargerSelected` 携带的上下文；没有放宽生产代次检查，也没有将断言改为新的魔数。刻意构造失配代次的测试保持原样。

## 验证范围

本机使用全局 Qt/CMake/Ninja/Node，构建目录在 Linux 原生文件系统。离线验证入口：

```bash
cmake -S . -B /home/hushengyuan/.cache/ev-user-closeout-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /home/hushengyuan/.cache/ev-user-closeout-build -j 4
ctest --test-dir /home/hushengyuan/.cache/ev-user-closeout-build --output-on-failure -j 4
node --test apps/user-client/tests/test_navigation_html.mjs
```

最终文件版本重新构建通过；完整 CTest **13/13 通过、0 失败**，耗时 38.91 秒；地图 Node **15/15 通过、0 失败**。独立代码复核未发现需要修改的问题。以上为本机自动化验证，不代表真实服务端全链路验收。

在线地图单独执行，结果及 6 张截图见 [腾讯地图冒烟记录](tencent-map-smoke.md)。本记录随本次修复提交保存，交接时以 `git rev-parse HEAD` 获取对应提交，不将基线 SHA 当作修复后的版本。

## 风险与队员交接

- **R15：客户端定向修复与自动化已补齐，真实服务端重放证据待补，因此保持开放。** 不以本地协议测试响应冒充真实联调。
- **R1：本机实际界面在线成功已补证，正式演示环境仍需复验。** Key 未提交；已验证“暂无预测”不阻塞本次找站/导航路径。
- #2 需要提供与冻结合同一致的用户登录、当前订单和站桩接口；尤其 `system.health` 必须能被严格五字段解码器接受，不通过放宽客户端合同来掩盖服务端不一致。
- 真实预约—充电—遥测—停止—结算、管理端一致性与同一提交两次彩排仍属后续团队集成工作。本轮不能据此宣告整个核心系统 GO。
