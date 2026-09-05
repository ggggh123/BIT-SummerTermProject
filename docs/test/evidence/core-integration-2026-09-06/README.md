# 真实服务端诊断探针

`probe_server.py` 是 2026-09-06 首次检查使用的探针留档，不是发布验收测试。它连接自己启动的真实 Qt 服务端，对独立的测试数据库执行充值、预约、遥测、停止和结算，打印实际结果。

**退出码 0 只表示探针执行完成，不表示业务缺陷已修复。** 重点检查输出中的重复充值余额、重复 ACK、停止前后电量/金额/时长。`demo.reset` 未在冻结 action 表中定义，输出未知动作是接口现状记录，不是新增协议的需求。

## 运行方法

先在仓库根目录构建核心三个 Qt 程序。下面例子使用已有 `build/debug`；实际构建在其他目录时，给软链接替换为那个构建目录的绝对路径。

```bash
cmake --preset debug
cmake --build --preset debug
probe_workspace=$(mktemp -d /tmp/ev-core-probe-XXXXXX)
cp docs/test/evidence/core-integration-2026-09-06/probe_server.py "$probe_workspace/"
ln -s "$PWD/build/debug" "$probe_workspace/build"
python3 "$probe_workspace/probe_server.py"
```

每次使用新的目录。不要直接在本证据目录启动脚本，不把用户现有运行库交给 `--existing-copy`；该参数只允许用在额外导出的诊断库副本上，因为服务端启动会迁移表并写入日志。默认流程会生成新的 `probe-fresh.db`，若文件已存在则拒绝复用。

脚本不安装环境，不访问腾讯地图，不输出登录 token，不连接指定的外部服务器，也不会自动修改 Git。测试服务只监听回环随机端口，结束时清理自己的服务进程；数据库和脚本目录保留供检查。

## 对照验收

- 同 requestId 充值 100 分，只增加 100 分，返回首次 ACK。
- 同 requestId 遥测，只累加一次，返回首次 ACK，不因 cursor 重复而拒绝。
- 两次遥测累计 0.75 kWh；150 分/kWh 时金额为 113 分。停止前后量价保持一致，时长来自真实时间。
- 字段时间必须符合 `YYYY-MM-DDTHH:mm:ss[.fraction]+08:00`；完整客户端接受能力仍需用真实 `UserApi` 对接验证。
- 该探针没有覆盖 SQL 故障原子性、完整 Qt 窗口、模拟器实际线上编码或两轮彩排，不能代替这些门槛。
