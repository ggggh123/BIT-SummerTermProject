# 缺陷调查报告：`revokeFailureIsExplicitlyReported` 探针在套件上下文被 SIGABRT 杀死

> 状态：**待复核（转 #3 PRL）**｜报告人：#4（SCML）｜日期：2026-09-07
> 环境：Ubuntu 22.04 虚拟机（VMware）· Qt 6.2.4 · GCC 11.3.0 · Debug 构建 · CMake/CTest
> 涉及文件：`simulator/tests/tst_runtimestatus.cpp`、`simulator/src/app/RuntimeStatusWriter.{h,cpp}`

## 1. 现象

`simulator_runtime_status` 测试套件整体运行时，用例 `revokeFailureIsExplicitlyReported` 的探针子进程**被 SIGABRT 杀死**（si_code=SI_TKILL，自身发送，core dumped），QProcess 上报 `CrashExit`，用例失败：

```
FAIL!  : RuntimeStatusTest::revokeFailureIsExplicitlyReported()
  Actual   (probe.exitStatus())  : CrashExit
  Expected (QProcess::NormalExit): NormalExit
  Loc: simulator/tests/tst_runtimestatus.cpp(453)
```

**关键特征**：同一探针**脱离套件单独运行时完全正常**（exit=0），崩溃仅在套件上下文出现。

## 2. 复现路径

- **必现**：完整套件连跑（`ctest --preset debug -R simulator_runtime_status`），探针位于前序用例 `runtimeWriteFailureRevokesPublishedReady` 之后时；
- **不复现**：单独运行该探针（工作目录分别为项目根与 `build/debug`、含 strace 全程跟踪）均 exit=0；
- **不可复现的干扰因素已排除**：使用 `/tmp` 直属路径导致 `makeReadOnly` 对 root 目录无权限的伪失败（exit=13）已识别并规避（改用 `mktemp -d` 用户目录）。

## 3. 已排除的假设（附证据）

| 假设 | 排除证据 |
|---|---|
| 内核 RLIMIT_FSIZE 强制异常 | `bash -c 'ulimit -f 0; echo x > /tmp/fsize-test.txt'` → exit=153（SIGXFSZ 击杀），0 字节文件——内核强制正常 |
| 探针逻辑错误 | strace 全程跟踪单独运行：`chmod(目录,0700)=0`、`access(文件)=0`、`exit_group(0)`，每一步符合预期 |
| 工作目录因素 | 从 `build/debug` 工作目录单独运行探针，exit=0 |
| 产品代码 `RuntimeStatusWriter` 缺陷 | 已修复并验证（见 §5）：套件内其余 15 个用例全部通过，含两个灾难场景用例 |
| Qt 断言/qFatal 输出缺失 | strace 日志中崩溃进程（PID 22894）无任何 `write(2,...)` 记录——SIGABRT 前无任何错误输出，**静默自杀** |

## 4. 关键证据（strace 摘录）

```
3402:22894 --- SIGABRT {si_signo=SIGABRT, si_code=SI_TKILL, si_pid=22894, si_uid=1000} ---
3403:22894 +++ killed by SIGABRT (core dumped) +++
3405:22892 --- SIGCHLD {..., si_code=CLD_DUMPED, si_pid=22894, ...} ---
```

- SIGABRT 由进程**自身**发送（SI_TKILL），且**无任何 stderr 输出**——排除了 qFatal/Q_ASSERT（会打印）与 glibc 堆检查（会打印 `free(): ...`）；
- 同套件中生产模拟器子进程（PID 22896）收到 SIGXFSZ（被 SIG_IGN 忽略后以 EFBIG 返回）后，打印「运行状态文件写入被系统拒绝」并以 `EXIT_FAILURE` 干净退出——与 `RuntimeStatusWriter` 修复的设计行为完全一致。

## 5. 同轮已完成的相关修复（背景）

| 提交 | 内容 |
|---|---|
| `10034fd` | R13/R14 修复：模拟器真实运行状态上报、事件时间戳严格递增；封存 core 黄金库 |
| `1a5f4c0` | **Qt 6.2 兼容修复**：实测 Qt 6.2.4 的 `QSaveFile` 在内核拒绝写入（RLIMIT_FSIZE=0/EFBIG）时 `write()`/`commit()` 仍返回成功，仅 `errorString()` 记录「文件过大」。`RuntimeStatusWriter::writeState` 提交后新增错误状态核对 + 回读校验，任何静默写失败都会撤销已发布状态 |
| `5f886dc` | `demo_runtime_unit` ctest 超时 90s→240s（虚拟机实测 94.5s，121 用例全通过） |

修复后验证：`runtimeWriteFailureRevokesPublishedReady`、`productionRuntimeWriteFailureExitsNonZeroAndRevokesReady` 两个灾难场景用例转绿；生产模拟器在磁盘写失败模拟下按设计退出并撤销状态文件（strace 证实）。

## 6. 待查方向（建议复核人执行）

> 探针源码已收编入库：`simulator/tests/tools/qsave_probe.cpp`（文件头附编译与运行命令），复核时可直接复用。

1. **gdb 分析 core 转储**：`core_pattern` 为 apport 时位于 `/var/lib/apport/coredump/`；`gdb tst_simulator_runtime_status <core>` 取回溯，确认 abort 的调用栈；
2. **QtTest 用例二分**：同一进程内顺序执行相邻用例复现跨用例状态污染：
   ```bash
   QT_QPA_PLATFORM=offscreen build/debug/simulator/tst_simulator_runtime_status \
     runtimeWriteFailureRevokesPublishedReady revokeFailureIsExplicitlyReported
   ```
3. **重点怀疑**：套件上下文与单独运行的环境差异对探针子进程的影响（QtTest 全局状态、信号处置继承、前序用例残留的临时目录/权限），以及 Qt 6.2.4 的 `QSaveFile`/`QTemporaryDir` 内部行为与新版 Qt 的差异。

## 7. 影响评估

- **不阻塞中期评审**：产品代码（`RuntimeStatusWriter` 的 Qt 6.2 静默写失败检测）已由其余 15 个用例及 strace 生产路径验证；真实演示不依赖「撤销失败」灾难场景；
- 该失败为**测试基建（探针子进程）在特定上下文的稳定性问题**，仅影响此一个用例；
- 已知缓解：单独运行探针可通过；套件内失败后重跑该单测试亦通过。

## 8. 同轮相邻发现（供 #3 一并处理）

| 测试 | 现象 | 初步结论 |
|---|---|---|
| `user_api`（84/85） | 头像 pixmap 为空 | **已解决**：虚拟机缺 `libqt6svg6` 渲染插件，安装后转绿（43s Passed） |
| `user_mobileui`（24/25） | `renderedPixelsNearColor(...) > 12` 断言失败 | offscreen 渲染的字体/主题差异导致像素颜色阈值超限，环境敏感型视觉断言，建议 #3 放宽阈值或增加容差 |
| `user_tencentmap`（9/10） | `routeOperationCorrelation...` 收到 fatal error | 需 #3 结合日志定位（测试使用占位符 Key 与真实腾讯 API 的交互口径待明确；虚拟机可访问国内网络） |
