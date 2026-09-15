# Ubuntu 22.04 三端便携发行验收记录

日期：2026-09-07。本文只记录实际执行结果；未执行项不代表通过。

## 验证边界

- 宿主机：Ubuntu 25.04 x86_64 虚拟机，glibc 2.41，原全局 Qt 6.8.3 保持不变。
- 兼容构建：本机目录中的 Ubuntu Jammy 用户空间，Qt 6.2.4、GCC 11.4、Python 3.10.12、glibc 2.35；共享宿主内核。
- 显示：本机 Xvfb `:93`，1800×1100；不代表另一台虚拟机的实际显卡驱动表现。
- 按用户要求，没有下载完整 Ubuntu 22.04 安装镜像，没有另建独立验收系统，也没有在队员的 Ubuntu 22.04 虚拟机上运行本包。
- 已下载的 Ubuntu Base 22.04.5 amd64 约 29 MB，仅用于本机兼容构建。官方 `SHA256SUMS.gpg` 已通过宿主 Ubuntu archive keyring 验证；归档 SHA-256：`242cd8898b33ea806ef5f13b1076ed7c76f9f989d18384452f7166692438ff1a`。
- 未对原开发服务执行停止或修改；主要发行验收使用 19100 和独立用户数据目录。后续观察原 PID 已不存在，9100 已空闲时，RC2 也成功启动并正常停止了自己登记的新轮次。端口冲突测试使用单独持有的 19101 socket，避免依赖外部进程状态。

## 构建与自动回归

| 项目 | 实际结果 |
|---|---|
| 原全局 Qt 6.8 基线 CTest | 34/34 通过，46.24 秒 |
| Jammy Release 三个生产目标 | 管理／服务端、用户端、模拟器全部编译成功 |
| Jammy 全部测试目标 | 全部编译成功 |
| Jammy 完整 CTest（兼容修正后） | 34/34 通过，47.71 秒 |
| 发行启动器和共享 Runtime 回归 | 138 项通过，41.43 秒；包含真实子进程、TCP 和真实 ELF 搬移测试 |
| 最终原生 Qt 6.8 完整串行回归 | 34/34 通过，171.93 秒，包含真实三端 live 测试 |
| 最终 Jammy Qt 6.2 完整串行回归 | 34/34 通过，181.23 秒；最后补充的动态角色发布断言另行重编译，server_threads 1/1 通过，13.29 秒 |
| 最终 Collector + portable runtime | 66 项通过；包含 XDG 路径、构建清单、非法 UTF-8、真实 import 和字体 UUID |
| 最终发行及共享脚本回归 | 187 项通过、2 项条件跳过，40.61 秒；live 项已在上述原生和 Jammy CTest 使用真实 ELF 单独通过 |

最终鉴权修正后的一轮并行 Jammy 回归曾出现 3 项失败（`user_api` 轮询时序、`server_tcp_demo_reset` 超时、`demo_runtime_unit` 超时），没有修改或放宽断言；同一代码完整串行复核通过。该记录保留并行时序敏感性，不将首次失败隐去，也不据此宣称并行执行已稳定。

本次发现并修正两处 Qt 版本兼容问题：

1. 模拟器测试使用了较新 Qt 才有的时区工厂 API，改用 Qt 6.2 已支持的等价秒偏移构造；时区含义及断言不变。
2. Qt 6.2 的 `QSaveFile::commit()` 在测试触发 `EFBIG` 时可能返回成功并发布空文件。现在提交前显式检查 `flush()`，失败时取消写入、撤销旧 ready 状态并沿既有错误信号退出。原有真实文件大小限制测试未削弱，Qt 6.2 和 6.8 的聚焦测试均通过。

## 补充在线地图测试（不是最终发行包验收）

本机兼容构建环境内运行已有 `user_map_online_smoke` 测试目标。它使用真实腾讯服务和真实客户端页面，但业务 TCP 响应由测试提供，不冒充真实服务端联调。

- 默认配置的真实地址解析成功，测试坐标约为 `39.9581, 116.313`。
- 真实驾车路线、步行路线通过。
- 主动阻断地图网络后，错误提示、重试入口、上次成功路线保留；解除阻断后重试成功。
- 结果：3 passed，0 failed，约 9.7 秒。测试主动阻断网络时的一条 JavaScript 错误为该用例预期现象。
- 第一轮测试在构建根中无法自动读到宿主 VMware 识别信息，触发 WebGL blocklist；补上现有应用在 VMware 上原本使用的 `--ignore-gpu-blocklist` 后通过。该补充测试显式关闭了 Chromium sandbox，只用于本地测试进程，不代表发行包的默认设置。
- 日志检查未发现默认地图 Key 原文。

## 最终发行包验收

最终交付为 **`EVCharging-20260907-r1-ubuntu22.04-x86_64.tar.gz`**，204,282,741 字节（约 195 MiB），解压约 459 MiB。源码／运行脚本版本：`1a08e3a9008f3689f6824e5d24f413518ab19360`；本文的后续记录提交不改变发行程序。

最终归档 SHA-256：`4587bd5097caced3c410a94478b54cd408822f5e0dafafee38b8b49cb4defedb`。

第一轮完整业务验收使用修订前归档（源码 `6de0cc5`，204,291,987 字节）；它已移出交付目录，仅保留过程证据。最终审查发现 XDG 空值／相对值边界问题后，修订了发行启动器、构建清单与错误分类，生成上述 r1 包。实际逐文件比较确认：**三端 ELF 指纹及 Qt、Python、字体、数据库等运行资源不变**；差异仅限所述脚本／清单、说明和预置字体 UUID。

两个归档均直接解压验收，而不是使用源码启动器。解压路径含中文与空格；调用方目录不在软件目录内；PATH 仅保留脚本所需的系统 `dirname`，用绝对路径 `/bin/bash` 进入，未借助全局 Python、Qt、Git、CMake 或编译器。未设置关闭 Chromium sandbox 的变量。

### 完整业务与资源验收

以下充电、营收、复位数值来自修订前的实际三端业务轮次，不冒充 r1 再次充电的结果；r1 的受影响项复验紧接下表。

| 实际操作 | 结果 |
|---|---|
| 默认包内地图配置 | 已携带授权 Key；没有手动环境注入 Key；配置文件权限 0600 |
| 一键启动和状态脚本 | 三端均 ALIVE，服务可用，模拟器已鉴权；默认暂停提示正确 |
| 实际管理／用户界面登录 | admin／123456、13800138000 成功，中文字体和主题正常 |
| 真实腾讯地址解析及导航 | 加载 6 个站点，完整底图与蓝色驾车路线显示；网络底图异步加载较路线稍晚 |
| 预约、开始、模拟器 Run、停止、结算 | 订单 432 完成，0.602652937404099 kWh，90 分；界面显示 0.603 kWh／0.90 元 |
| 余额与统计 | 50000→49910 分；今日营收 0.90 元、月营收 23.18 元、累计营收 11452.23 元 |
| 管理端恢复演示黄金数据 | 实际按钮和二次确认成功；恢复 6 站／48 桩／431 单、余额 50000 分；随后真实 TCP 登录／站点／当前订单业务检查通过 |
| 数据库完整性 | 充电结算后、复位后均 `integrity_check = ok` |
| 正常停止 | 三个已登记进程均 EXITED |
| 停止后整目录更名搬移再启动 | 正常 RUNNING，再次正常停止；不需要重新打包或修改配置 |
| 占用端口保护 | 自持 19101 socket 时返回 PORT_IN_USE，没有结束持有者或启动三端 |
| 重复启动保护 | RC2 和最终 r1 三端活跃时均返回 ACTIVE_SERVER |
| 启动前／运行中／搬移停止后文件快照 | 全部文件 SHA-256 清单完全相同，零新增 `.pyc`／`.uuid`；字体 UUID 在打包期已预置 |
| 压缩包和包内校验 | 外部 SHA-256 与内部 SHA256SUMS 全部通过；可替换配置不在内部不可变清单中 |
| 实际依赖加载 | 三端及 QtWebEngine helper 的 Qt 动态库、5 处 pak 映射均来自当前解压目录，没有混用宿主 Qt 6.8 资源 |
| ABI 与依赖边界 | 283 个唯一 ELF，最高所需 GLIBC 2.35；无绝对 RPATH；不携带 glibc／系统 loader／Mesa DRI 驱动 |
| 第三方许可 | 保存 206 个来源包的版本及许可，并保留公共许可文本 |
| 实际运行日志 | 检查所有候选／最终轮次日志，未发现默认地图 Key 原文 |

候选包联调还暴露并修正了两个源码单测不易发现的问题：服务端原先忽略启动器生成的 simulator token；QtWebEngine 子进程原先加载了宿主的不同版本 pak。现在服务端在启动时固定接收配置快照，主程序和 helper 均有包内 Qt 路径配置。Fontconfig 2.13 与 Python 的目录写入问题也已通过实际发行树快照闭环。

### r1 修订包的实际复验

- `XDG_DATA_HOME=''`：从最终压缩包解压启动，三端 RUNNING／ALIVE，服务可用、模拟器已鉴权；数据位于 `/home/hushengyuan/.local/share/evcharging/20260907-r1/runs/r1-empty-xdg`，没有写入软件目录。
- 此轮实际管理端与用户端登录、真实腾讯地址解析和完整底图／驾车路线成功；真实 TCP 的健康、登录、用户、站点、当前订单检查通过。实际 Qt／pak 映射继续全部来自 r1 解压目录。
- 正常停止后移动整个解压目录，再令 `XDG_DATA_HOME=relative-data` 启动。新的 `r1-relative-moved` 轮次仍位于默认用户数据目录；三端启动、鉴权、状态、停止均成功，旧轮次仍保留。
- 两轮均未设置关闭 Chromium sandbox 的变量，也没有可从 PATH 调用的 Python／Git／CMake／编译器。
- 最终发行树在启动前与上述两轮完成后的全文件 SHA-256 清单完全相同；未生成 `relative-data/`、`evcharging/` 或 Python bytecode 目录。
- `build-manifest.json` 的源码 SHA 与发行清单相符，并纳入包内校验。实际 CMake 缓存与编译器检测文件、`g++ -dumpmachine` 和 dpkg 已核对：Release、CMake 3.22.1、Ninja、GNU 11.4.0、x86_64-linux-gnu、Qt 6.2.4。整个工作区有无关文档修改，因此如实记录 `sourceDirty=true`，白名单发行输入均已提交；原有 README、目录说明和过程材料不混入软件。
- 最终包内真实 Key 只出现在授权的 `config.local.ini`，两轮实际日志均无 Key 原文。
- 外部归档校验、包内 `SHA256SUMS` 全部通过。操作过程中曾在压缩尚未结束时过早启动解压，得到 EOF；等待压缩退出 0 后重新完整解压并校验通过，这是验收命令顺序错误，最终交付归档并未损坏。
- 全分支审查及唯一一次最终修正复审通过，原 XDG、构建信息、UTF-8 错误分类三项发现均关闭。未因该轮 Python／元信息修改重复全部 C++ 回归；依据为生产 ELF 逐字节不变、187 项脚本回归通过及上述新归档实际复验。

交付目录：`/mnt/hgfs/Desktop/SummerTermProject/deliverables/2026-09-07-ubuntu22.04/`。腾讯 Key 仅进入本地团队发行包，不进入 Git 提交；不要将整个包公开上传。

兼容性结论：使用 Ubuntu 22.04 的工具链与用户空间构建，并验证了 ABI 下限和本机真实运行；没有独立 Ubuntu 22.04 桌面／显卡驱动实测。它是面向 Ubuntu 22.04+ x86_64 桌面的团队便携包，不是所有 Linux 发行版、ARM64、无桌面服务器或生产部署的保证。

## 过程证据位置

本机过程日志位于 `/home/hushengyuan/ev-release/ubuntu22/`：

- `build.log`、`build-tests-pass2.log`：兼容编译结果。
- `ctest-jammy-final.log`：Jammy 34/34 完整回归。
- `ctest-status-flush.log`：状态写入兼容修正聚焦验证。
- `map-online-smoke-vm-flags.log`：补充在线地图测试。
- `ctest-auth-snapshot-serial.log`、`ctest-final-test-assertion.log`：最终 Jammy 验证。

本机 `/home/hushengyuan/ev-release/` 另保留 `native-final-ctest.log`、`final-inventory-{before,running,after}.sha256` 和 `final-payload-checksum.log`；`screenshots/final-real-map-loaded.png`、`final-charging.png`、`final-settled.png` 为修订前归档的实际界面证据。

其中 `final-*` 文件名是第一轮完整业务验收时命名的历史证据；最终 r1 的直接证据为 `r1-python-regression.log`、`r1-payload-checksum.log`、`r1-inventory-{before,after}.sha256`、`screenshots/r1-real-map.png`。旧候选压缩包留在 `superseded-archives/`，不进入交付目录。

这些路径是开发机过程记录，不是接收方运行所需依赖。最终发行包中的资源必须全部自包含，系统基础库和图形驱动除外。
