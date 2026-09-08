# Ubuntu 22.04 团队基线整理与验证

日期：2026-09-07。实施分支：`fix/ubuntu22-team-baseline`；整理前源码基于 `4bb7894`，本报告与环境入口修改在同次提交。远端 `dev` 在检查时为 `97c6da1`，本轮通过 PR 集成，不直接合并 main/dev。

## 发现与处理

1. 旧环境矩阵记录的是主开发机 Ubuntu 25.04 / Qt 6.8；原样移存为历史观察，新矩阵以 Ubuntu 22.04 / Qt 6.2 为团队默认要求。
2. 用户端 README 错写 CMake 3.25+；实际项目支持 Jammy 的 3.22.1，已纠正。
3. 安装脚本遗漏部分构建/运行依赖，却默认安装已退出核心范围的 Web/ML 包；补齐 GCC、OpenGL、SQLite/SVG 和 WebEngine 辅助包，移除不需要的 Qt Charts 与默认 Web/ML 安装。仅调整安装清单，不删除成果。
4. 原环境检查依赖新版 Qt 的 pkg-config 元数据及 qtpaths6 命令位置。真实 Jammy 检查发现 `.pc` 文件缺失且 qtpaths6 不在 `/usr/bin`；改为系统 qmake6 查询安装路径、读取 CMake 开发包版本，并检查实际运行资源。
5. 原默认全量构建编译测试目标，虚拟机负担较大；新增 `ubuntu22` 三程序日常版和 `ubuntu22-test` 完整回归版。两者默认构建并行 2，CTest 串行。没有通过删除测试或放宽断言消除失败。
6. Qt 6.2 QTimeZone 构造兼容和 QSaveFile 缓冲写入错误检测修复已存在于本分支祖先提交，但整理前尚未共享到 dev；本轮一起进入 PR。

没有获得队友的第一处完整 error 日志，因此上述是已核实的问题和预防措施，不能声称穷尽其电脑上的全部错误。

## 实际验证环境

复用本机已有 `/home/hushengyuan/ev-release/ubuntu22/rootfs`，在新目录 `/team-baseline-src` 中复制源码并重新构建，没有下载 Ubuntu 完整镜像，没有改动宿主 Ubuntu 25.04 的系统版本，没有覆盖此前发行包或构建目录。

- Ubuntu 22.04 Jammy 用户空间，x86_64。
- Qt 6.2.4、GCC 11.4.0、CMake 3.22.1、Python 3.10.12。
- 全局官方 APT 依赖；bootstrap 实际运行成功，已有环境仅新增 pkg-config（下载 48.2 kB）。
- 非 root builder 用户构建和测试；Qt 使用 offscreen；测试网络为本地 TCP，无真实腾讯 Key。
- 该用户空间共享宿主内核，不等同于独立 Ubuntu 22.04 桌面虚拟机。

## 结果

| 检查 | 实际结果 |
|---|---|
| `bash scripts/bootstrap.sh`（Jammy 内） | 成功，全局依赖可解析并安装 |
| `bash scripts/check_env.sh --strict`（Jammy 内） | 通过，Qt 模块与运行资源齐全 |
| `cmake --preset ubuntu22` + build 同名预设 | 通过，66 个 Ninja 步骤，三个生产程序；目标清单无测试程序 |
| `cmake --preset ubuntu22-test` + build 同名预设 | 通过，309 个 Ninja 步骤 |
| `ctest --preset ubuntu22-test` | **35/35 通过，190.01 秒**，含真实三进程启动/冒烟/停止和坏模拟器 token 回滚 |
| Jammy：`python3 -m pytest database/tests tests/release -q` | **81/81 通过，9.62 秒** |
| 宿主：数据库、发行工具和新环境脚本回归 | **98/98 通过**，其中新环境测试 17 项 |
| 新环境脚本在宿主普通模式 | 通过，明确警告 Ubuntu 25.04 / Qt 6.8 非团队基线 |
| 旧 `tests/scripts/test_check_env.sh` 入口 | 通过，委托 17 项确定性测试，不强迫宿主安装可选 Web/ML |
| CMake preset 列表、工作流 YAML 结构、`git diff --check` | 通过 |

完整本机日志：`/home/hushengyuan/ev-release/ubuntu22/team-baseline-build.log`、`team-baseline-bootstrap.log`、`team-baseline-env-final.log`。这些是验证机证据位置，不是队友需要复制使用的构建路径。

## 尚未代签的事项

- `.github/workflows/ubuntu22.yml` 新增远端 22.04 门禁；实际结果在 PR Checks 查看，本报告不预先宣称远端 CI 已通过。
- 未改变分支保护设置，未直接合并 main/dev；未动人工需求矩阵、原工作区未提交目录说明、真实 Key 或私下发行包。
- 未执行真实腾讯在线请求、可选 Node HTML 测试、可选 ML 回归或队友独立桌面 VM 人工验收；这些不被 CTest 通过代替。
- 日常构建减少的是构建任务，不承诺固定提速倍数；实际速度仍受虚拟机 CPU、内存和共享目录 I/O 影响。

队友操作以[开发指南](../development/ubuntu22.md)为准，先统一 SHA，再安装、预检和增量构建。
