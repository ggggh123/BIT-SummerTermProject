# Ubuntu 22.04 团队开发与编译指南

自 2026-09-07 起，团队默认基线为 **Ubuntu 22.04 LTS x86_64、系统 APT Qt 6.2 系列、GCC 11、CMake 3.22、Python 3.10**。本机 Ubuntu 25.04 / Qt 6.8 只是额外开发环境，不是队友必须升级到的版本。

## 1. 统一源码版本

PR #11、#12 均已合入远端 `dev`。最终 UI 交付分支为 `feat/energy-pulse-qt-ui`，已整合检查时的 `dev@a15e088`；发布范围见[最终交付说明](../release/final-2026-09-08.md)。本次 PR 合并前需要试用最终版时，在无未提交改动的独立 clone 中执行：

```bash
git fetch origin
git switch --track origin/feat/energy-pulse-qt-ui
git rev-parse HEAD
```

若已有这个本地分支，切换后用 `git pull --ff-only`；有未提交工作时先自行提交或保留，不强制覆盖。PR 合并后团队统一切回 `dev` 并 `git pull --ff-only`。不要仅凭“已经拉取”判断版本一致，应比较完整 SHA。`main` 是稳定里程碑，不保证始终包含 `dev` 最新修复。

建议 clone 到虚拟机原生磁盘（例如个人目录下的项目文件夹）。VMware 共享目录适合交换文件，但大量 C++ 小文件读写可能明显拖慢编译。不同电脑之间不要复制 `build/`、CMakeCache.txt、Qt Creator 本机配置或本机编译产物。

## 2. 一次性安装全局依赖

在仓库根目录执行：

```bash
bash scripts/bootstrap.sh
bash scripts/check_env.sh --strict
```

安装器使用 Ubuntu 官方 APT 源和系统目录，不创建虚拟环境、不安装自定义 Qt SDK、不升级发行版。若系统没有启用 universe 软件源，先在 Ubuntu“软件和更新”中启用，再执行安装。

依赖清单可用 `bash scripts/bootstrap.sh --print-packages` 查看。安装包含编译器、Ninja、Qt Widgets/Network/WebEngine/Test、OpenGL 开发文件、SQLite/SVG/TLS 运行插件、WebEngine 辅助程序与中文字体。管理端图表由 QWidget 绘制，不需要 Qt Charts。Node/npm 和 ML 科学计算包不属于核心安装；保留的 Web/ML 代码不删除。

`--strict` 要求 Ubuntu 22.04 和 Qt 6.2 系列；普通 `check_env.sh` 在较新系统上给出警告但允许继续检查。非 22.04 Ubuntu 若确需安装系统依赖，须显式使用 `bootstrap.sh --allow-other-ubuntu`，它不会把该系统变成 22.04。

新用户可用一键入口 `python3 scripts/quickstart.py --start`（本节 bootstrap → 预检 → `ubuntu22` 三程序构建 → 拉起三端），默认并行 2；低内存虚拟机使用 `--jobs 1`，完整回归编译显式选择 `--preset ubuntu22-test`。环境兼容问题的历史排查见[环境兼容性汇总](../test/ubuntu22-qt62-compatibility-2026-09-07.md)。

## 3. 日常编译：只构建三个程序

```bash
cmake --preset ubuntu22
cmake --build --preset ubuntu22
```

这是 Ninja + Release 构建，关闭测试目标，默认最多同时编译两个文件。输出为：

- `build/ubuntu22/apps/admin-server/ev_admin_server`
- `build/ubuntu22/apps/user-client/ev_user_client`
- `build/ubuntu22/simulator/ev_charger_simulator`

再次执行 build 会增量编译，不要每次清空构建目录。内存紧张时使用 `cmake --build --preset ubuntu22 --parallel 1`。本预设固定系统 `/usr/bin/g++`，但**预设名称不是系统模拟器**：在 Ubuntu 25.04 上运行它仍产生本机二进制，不能因此宣称兼容 22.04。

## 4. 完整回归：单独构建测试版本

```bash
cmake --preset ubuntu22-test
cmake --build --preset ubuntu22-test
ctest --preset ubuntu22-test
/usr/bin/python3 -m pytest database/tests tests/release -q
```

测试版位于 `build/ubuntu22-test/`，与日常版分开，避免将“没有构建测试”误报为通过。完整编译比三程序构建耗时更长。CTest 默认 offscreen、串行运行，降低虚拟机负载对定时/线程测试的干扰；不代表人工 GUI 验收已经完成。

旧 `debug` / `release` 预设为兼容已有工作保留，不再作为默认团队教程入口。不要在旧 Makefiles 构建目录中直接改用 Ninja。Qt Creator 应选择系统 Qt 6 Kit、`/usr/bin/g++` 和 `/usr/bin/cmake`，不得误选 Qt 5 或另一套下载的 Qt；日常配置设置 `BUILD_TESTING=OFF`。

导航 HTML 的 Node.js 18+ 合同测试和真实腾讯地图在线冒烟是独立检查，见[用户端说明](../../apps/user-client/README.md)。核心构建/CI 不自动请求腾讯地图，也不需要真实 Key。

## 5. 启动与配置

三端冷启动、黄金数据副本、停止与基本冒烟见[演示操作手册](../release/core-demo-runbook.md)。使用新日常构建时，把手册中的 build 路径替换为当前 clone 下 `build/ubuntu22` 的绝对路径；例如 start 的参数为 `--build-dir "$PWD/build/ubuntu22"`。

用户端地图 Key：团队已确认所用腾讯地图 Key 为**公共免费测试 Key**，源码内置默认值（`UserAppConfig::bundledTencentMapKey()`），克隆后零配置即可使用地图；仍可用已忽略的 `config.local.ini` 的 `tencent/mapKey` 或 `EV_TENCENT_MAP_KEY` 覆盖。服务端与模拟器 token 必须匹配。程序编译成功也不代表在线地图权限、配额及网络已通过验证；若 Key 配额异常，在腾讯控制台处理后仅需轮换 `bundledTencentMapKey()` 中的内置值（2026-09-08 团队确认，取代此前"源码不内置真实 Key"的口径）。

## 6. 常见错误定位

| 现象 | 优先检查 |
|---|---|
| 找不到 Qt6 / WebEngineWidgets / OpenGL | 运行 bootstrap 和严格预检；特别检查 `qt6-webengine-dev`、`libqt6opengl6-dev`，不要混用 Qt 5/6 或不同来源 Qt 模块 |
| 提示 CMake 至少 3.25 | 本项目基线为 3.22；旧用户端文档已经纠正，确认是否仍在旧 SHA 或其他子工程 |
| Qt API 不存在，如较新 QTimeZone 工厂方法 | 拉取本轮兼容修复；不得用升级全队 Qt 的方式掩盖缺少修复的旧源码 |
| `Killed signal terminated program cc1plus` | 常见原因是内存不足；改 `--parallel 1`，先构建日常版；保留完整首个错误供判断 |
| SQLite driver / SVG 图标 / QtWebEngineProcess 缺失 | 运行严格预检，安装运行插件及 WebEngine 辅助包；不能只复制三个 ELF 文件 |
| generator 与旧 cache 不一致 | 使用本指南的新目录，不复制或复用另一台机器的 CMakeCache |
| 编译特别慢 | 检查是否共享目录、是否全量测试构建、是否频繁重新配置/清理，以及内存交换；初次编译本来就慢于增量编译 |
| Node 版本不足 | Node 18+ 仅用于可选 HTML/Web 测试，不是 Qt 核心编译的前置阻塞 |

仍失败时提交：完整 SHA、`lsb_release -ds`、`bash scripts/check_env.sh` 输出、具体构建命令，以及**第一处 error 前后至少 30 行**。后续大量连锁错误不能代替首个错误。

## 7. 兼容性证据边界

GitHub 工作流在 `ubuntu-22.04` runner 上执行严格预检、日常版与测试版构建、CTest 和数据库/发行工具回归。以实际 workflow 结果为准，不以配置文件存在冒充通过。

本机已有 Jammy 用户空间可用于 Qt 6.2 / GCC 11 源码构建验证，但它共享宿主机内核，不代替队友独立 Ubuntu 22.04 桌面虚拟机的图形驱动与人工运行验收。二进制发行说明见[便携发行包](../release/portable-release.md)。
