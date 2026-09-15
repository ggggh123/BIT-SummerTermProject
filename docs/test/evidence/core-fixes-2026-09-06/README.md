# 修复后同版本验证证据

此目录只保存修复后输出；修前证据位于相邻 `core-integration-2026-09-06/`，不要覆盖或混用。

- `ctest-fixed-production.log`：生产组合 `2318d149c519f15fe478a1e700ad1ad60a2c9421` 的原生全新构建 CTest `LastTest.log`，24/24、38.73 秒。测试全部使用临时 fixture 或新运行副本，不把封存黄金库作为运行库。
- `ctest-with-core-workflow.log`：加入实际 Qt 三端测试后的代码/测试 `9faad1feed137d0610e88c88d0de0d0d2dc09a9a`，再次 clean-first 构建 180/180，CTest 25/25、39.04 秒。日志内包含真实模拟器电量/扣款/营收、故障重启后的占用检查以及运行进程/目录清理结果。
- 构建命令、其他回归计数、Qt UserApi 四组业务值、来源提交和未关闭项见 [`../../core-fixes-2026-09-06.md`](../../core-fixes-2026-09-06.md)。

日志中的测试响应、测试账户与模拟器 token 为本地测试数据，不是腾讯 Key。该记录不证明腾讯在线调用、人工三端窗口操作或双彩排已经完成。
