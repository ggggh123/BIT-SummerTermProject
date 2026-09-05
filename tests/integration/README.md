# 真实 Qt 核心联调回归

`core_workflow` 是程序化 Qt 核心联调，不是完整人工窗口彩排，也不访问腾讯地图。它从封存的
`runtime/golden/core.db` 复制一份临时运行库，随机选择回环端口，启动真实
`ev_admin_server` 子进程，并使用生产 `UserApi/TcpJsonClient` 与生产
`SimulatorClient/TelemetryEngine/SimulatorWindow` 完成业务链路。

当前覆盖两条顺序共享同一运行副本的链路：

1. 新账户登录、充值、查站、预约、开始，第二个同起点模拟器取得最新权威快照，窗口运行至少
   两个有效遥测样本后暂停，再停止、结算并核对量价、余额、历史、当前订单、桩状态和管理端
   今日营收。
2. 黄金老账户开始充电并计量后，由实际模拟器窗口快速注入故障与恢复；核对事件时间严格递增、
   故障结束时间与费用保留、管理员重启前后状态同步、未结算订单阻止他人预约，最后完成结算。

测试将 `TelemetryEngine` 的业务模拟步长显式设为 60 秒，以便无需固定长等待即可产生可核对电量；
`SimulatorClient` 的周期刷新间隔则显式设为 1 秒，用来验证生产内建定时器会自动同步预约、开始与
管理员重启后的权威状态。生产默认 3 秒配置仍由 `simulator_engine` 中的配置与时间用例覆盖。测试
只通过 TCP 改变业务状态，不对运行库执行手工 SQL。测试结束时会终止服务进程并验证临时目录已经
删除。

在全新原生构建目录中运行：

```bash
cmake -S . -B /path/to/native-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /path/to/native-build --target tst_core_workflow -j4
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir /path/to/native-build -R '^core_workflow$' --output-on-failure -V
```

若需要证明修复前 RED，可让同一测试二进制仅替换外部服务端路径：

```bash
QT_QPA_PLATFORM=offscreen \
EV_CORE_SERVER_UNDER_TEST=/path/to/pre-fix/ev_admin_server \
  /path/to/native-build/tests/integration/tst_core_workflow \
  newUserCompletesRealQtWorkflow -v1
```
