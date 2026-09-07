# 真实服务端诊断探针

`probe_server.py` 是 2026-09-06 首次检查使用的探针留档，不是发布验收测试。它连接自己启动的真实 Qt 服务端，对独立的测试数据库执行充值、预约、遥测、停止和结算，打印实际结果。

**退出码 0 只表示探针执行完成，不表示业务缺陷已修复。** 重点检查输出中的重复充值余额、重复 ACK、停止前后电量/金额/时长。`demo.reset` 已在冻结合同第 27 条定义，当前服务端未实现。此历史探针以用户 token、空 payload 调用它，输出只记录旧路由表现，并未验证管理员合法 reset；不得执行该动作来验证新的服务端而不重新确认测试库和预期。

## 运行方法

从仓库根目录执行以下命令。它会在 `/tmp` 的原生文件系统创建一个全新、空的 CMake build 目录，并只构建探针实际启动的 `ev_admin_server`；源码树内已有的 `build/debug` 不参与本次复现。探针目录中的 `build` 软链接明确指向这次配置产生的 build 目录，因此实际服务二进制来自 `$build_dir/apps/admin-server/ev_admin_server`。

```bash
repo_root=$PWD
build_dir=$(mktemp -d /tmp/ev-core-probe-build-XXXXXX)
cmake -S "$repo_root" -B "$build_dir" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" --target ev_admin_server --parallel
test -x "$build_dir/apps/admin-server/ev_admin_server"
probe_workspace=$(mktemp -d /tmp/ev-core-probe-XXXXXX)
cp docs/test/evidence/core-integration-2026-09-06/probe_server.py "$probe_workspace/"
ln -s "$build_dir" "$probe_workspace/build"
python3 "$probe_workspace/probe_server.py"
```

每次使用新的 build 目录和探针目录。不要直接在本证据目录启动脚本，也不要把封存黄金库、导出副本或用户现有运行库交给探针。脚本只支持在新探针目录中创建 `probe-fresh.db`；若该文件已存在则拒绝复用，任何参数（包括历史上的 `--existing-copy`）都会作为未知参数失败。

脚本不安装环境，不访问腾讯地图，不输出登录 token，不连接指定的外部服务器，也不会自动修改 Git。测试服务只监听回环随机端口，结束时清理自己的服务进程；数据库和脚本目录保留供检查。

## 对照验收

- 同 requestId 充值 100 分，只增加 100 分，返回首次 ACK。
- 同 requestId 遥测，只累加一次，返回首次 ACK，不因 cursor 重复而拒绝。
- 两次遥测累计 0.75 kWh；150 分/kWh 时金额为 113 分。停止前后量价保持一致，时长来自真实时间。
- 字段时间必须符合 `YYYY-MM-DDTHH:mm:ss[.fraction]+08:00`；完整客户端接受能力仍需用真实 `UserApi` 对接验证。
- 该探针没有覆盖 SQL 故障原子性、完整 Qt 窗口、模拟器实际线上编码或两轮彩排，不能代替这些门槛。
