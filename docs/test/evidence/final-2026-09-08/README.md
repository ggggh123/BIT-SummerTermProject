# 最终版整合复验记录

本目录记录整合 `dev@a15e088` 后的最终版交付整理验证；此前 UI 通过记录见[最后一轮 UI 反馈](../../../design/ui-feedback-followup-2026-09-08/README.md)。

当前复验环境：本机 Ubuntu 25.04 / Qt 6.8.3 / GCC 14.2 / Release / Ninja，使用现有全局环境与 `/home/hushengyuan/ev-release/build-energy-pulse`。Ubuntu 22.04 / Qt 6.2 的结果单独以 GitHub 本次 PR 的 CI 为准，不混用本机系统版本。

验证期间不直接修改用户的活动数据库；三端启停测试只创建和回收自身新轮次。如用户另有活动服务，保留门禁并单独记录未执行项，不停止用户程序来制造通过。

## 原始记录

CTest 日志入库时仅移除行尾空白以通过 Git 格式检查，不改测试输出内容、计数或结论。

- [整合后常规 CTest](ctest-local.log)
- [独立三端启停 CTest](ctest-live.log)
- [数据库与发行工具 pytest（JUnit）](python-regression.xml)

## 结果

- 整合后 Release/Ninja 完整构建成功。
- CTest **37/37 组通过**：常规 36 组 39.94 秒；独立三端启停 1 组 8.59 秒。新增 `source_delivery_tools` 包含 18 个脚本测试。
- `python3 -m pytest database/tests tests/release tests/scripts/test_quickstart.py -q`：**99 passed，4.06 秒**；原始用例与结果见上方 JUnit 文件。它与 CTest 的脚本测试有重叠，不把计数相加冒充独立用例总数。
- 外层本机新版启动包装层 **14 passed，0.19 秒**；它保留本机固定路径，不作为队友 clone 的入口。
- 黄金库六个工件、远端概要设计 Word 和需求矩阵与 `origin/dev` 比对无差异；`git diff --check` 通过。
- 源码打包器只读收集预检通过：核心 hash 匹配，收集六个黄金工件，个人 `config.local.ini` 被排除。此检查没有生成或发布最终二进制包。
- 旧 UI 证据日志采用限定目录白名单入库；历史报告中两处搬迁后的相对链接已修正，不改其历史结论。

这些结果覆盖已整合的代码和部署补充；原先未整合远端配置时的 36 组通过不再作为本次唯一证据。真正 Ubuntu 22.04 CI 请检查本次 PR，不把本机 25.04 运行或旧 dev 检查结果替代新提交的云端验证。

## PR #13 审查修正后的再次复验

自动审查的两处显示问题均通过新增用例先复现失败，再修正通过；问题依据和处理方式见[发布前审计第 6 节](../../../review/dev-final-delivery-2026-09-08.md#6-pr-13-自动审查复核)。此轮未改收费、会话或数据库结构。

- 全量增量构建通过；[常规 CTest](ctest-review-followup.log) **36/36 组，77.33 秒**；[独立三端启停](ctest-review-live.log) **1/1 组，9.66 秒**，合计仍为 37/37 组。
- [数据库／发行工具／快速部署 pytest](python-review-followup.xml)：**99 passed，3.97 秒**。
- 缺失采样用例覆盖前段截断和中间断点，确认无读数为「—」，恢复最新后权威整单电量和费用不变。
- 编号用例覆盖显示前数据到达、实际宽度增减、相同快照不重建控件，以及选中设备不丢失。
- 第一版 `b5fe2ba` 的 [Ubuntu 22.04 CI](https://github.com/ggggh123/BIT-SummerTermProject/actions/runs/34227388049) 已成功：37/37 CTest、88 项独立 pytest；**审查修正后的提交仍以 PR #13 最新 Checks 为准**，不冒用第一版绿勾。
