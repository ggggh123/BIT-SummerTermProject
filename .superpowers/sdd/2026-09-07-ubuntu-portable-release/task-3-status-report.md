# Task 3：Qt 6.2 运行状态写入回归

## 范围和结论

本修正只改动 `simulator/src/app/RuntimeStatusWriter.cpp`，不调整现有故障断言、协议、状态字段或 UI 行为。现有
`runtimeWriteFailureRevokesPublishedReady` 和
`productionRuntimeWriteFailureExitsNonZeroAndRevokesReady` 已是真实行为回归测试，
因此未修改 `simulator/tests/tst_runtimestatus.cpp`。

根因是 Qt 6.2.4 中 `QSaveFile` 的缓冲写入与提交行为：
`write()` 可以在内核真正写入前返回请求的字节数；后续真正的 `write(2)` 因
`RLIMIT_FSIZE=0` 以 `EFBIG` 失败，但 Qt 6.2.4 的 `commit()` 仍然返回成功并将空临时文件替换到目标路径。
因此上层收不到 `writeFailed`，既不撤销已发布的 `ready` 状态，生产模拟器也不退出。

## RED：Qt 6.2.4 可重现证据

Jammy 全量日志
`/home/hushengyuan/ev-release/ubuntu22/ctest-jammy.log` 记录 33/34 测试通过，唯一失败为
`simulator_runtime_status`：

- `runtimeWriteFailureRevokesPublishedReady`：探针退出码为 19，表示等待 3 秒仍未收到 `writeFailed`。
- `productionRuntimeWriteFailureExitsNonZeroAndRevokesReady`：状态写入受限后，生产进程 3 秒后仍在运行。

不改源码直接以 Jammy 的动态加载器和 Qt 6.2.4 运行现有探针：

```text
/home/hushengyuan/ev-release/ubuntu22/rootfs/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 \
  --library-path <rootfs>/usr/lib/x86_64-linux-gnu:<rootfs>/lib/x86_64-linux-gnu \
  <rootfs>/build/simulator/tst_simulator_runtime_status \
  --writer-runtime-failure-probe <tmp>/runtime.json
```

结果稳定为退出码 19，并留下 0 字节 `runtime.json`。

`strace` 将故障定位到 Qt 文件层，关键系统调用顺序为：

```text
prlimit64(... RLIMIT_FSIZE, {rlim_cur=0, ...}) = 0
openat(... O_TMPFILE, 0600) = 7
write(7, "{...ready...}", 94) = -1 EFBIG (File too large)
fdatasync(7) = 0
close(7) = 0
rename(".../runtime.json.<temporary>", ".../runtime.json") = 0
```

另用临时、未入库的最小 `QSaveFile` 程序对比 Qt 6.2.4 的返回值：

```text
不调用 flush: open=1 write=6 commit=1 after=File too large exists=1 size=0
显式调用 flush: open=1 write=6 flush=0 before=File too large commit=1 exists=1 size=0
```

这证明单独信任 `write()` 的字节数和 `commit()` 的布尔值不足以在 Qt 6.2.4 捕获该故障；
`flush()` 才在提交前可观测地返回失败。

## GREEN：最小修正

在已有 `write()` 长度检查之后、`commit()` 之前显式调用并检查 `file.flush()`。
如果刷新失败，立即：

1. 用 `cancelWriting()` 取消临时写入，不允许空文件被提交。
2. 进入原有 `failWrite()` 路径，撤销本 writer 先前已发布的运行状态。
3. 保留 Qt 的错误文本，通过原有 `writeFailed` 信号传给生产退出路径。

本机 Qt 6.8.3 登记测试：

```text
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  --target tst_simulator_runtime_status -j2
ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  -R '^simulator_runtime_status$' --output-on-failure
```

结果：`1/1 Test #11: simulator_runtime_status ... Passed`，0 失败，总用时 0.67 秒。

## Jammy 验证状态

主任务已将唯一源码改动同步到 Jammy `/src`，并重建
`ev_charger_simulator` 和 `tst_simulator_runtime_status`。使用 Qt 6.2.4 执行：

```text
ctest -R '^simulator_runtime_status$' --output-on-failure
```

结果：`1/1 Test #11: simulator_runtime_status ... Passed`，0 失败，测试用时 0.57 秒，
总用时 0.58 秒。完整输出保存于
`/home/hushengyuan/ev-release/ubuntu22/ctest-status-flush.log`。
