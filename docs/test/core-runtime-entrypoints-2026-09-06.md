# 核心四入口与真实三程序验证（2026-09-06）

本批在本机分支`feat/runtime-delivery-20260906`、源码基线`2a7465a602ab354e8791bd7098379c400953b3c4`上实现四个运行入口。运行时manifest同时记录了未提交代码脏状态与各二进制SHA-256；这些是提交前候选的测试证据，不冒充最终提交的独立重新构建或发布证明。

## 实现与实际范围

`scripts/reset_demo.sh`、`start_demo.sh`、`stop_demo.sh`、`smoke_test.sh`均可使用；流程、参数、日志与报告位置见[操作手册](../release/core-demo-runbook.md#3-当前可用的四个运行入口2026-09-06)。Python按CLI、运行编排、Linux进程身份与TCP协议四模块划分，未修改本批Task1的Qt代码，也未修改业务协议、状态机或黄金原件。

每轮仅允许全新`runtime/demo-runs/<run-id>/`。start使用已有原生CMake build，确认源码归属、二进制可执行及哈希；启动服务端后同时核对本次日志的实际绑定输出和严格health，再等待同PID、新鲜的模拟器ready，最后观察客户端至少500ms。三端均在源码根CWD运行，host/port明确覆盖继承环境。

stop及启动失败回滚使用PID/starttime/bootId/exe四项身份与pidfd。未知身份或TERM超时不丢记录、不伪装STOPPED；只有显式`stop --force`才允许核对后KILL。全部测试的旁观进程由测试创建，未对无关真实进程发信号。

smoke通过长度前缀JSON/TCP执行health及既有用户13800138000的登录、user.get、station.list、station.detail、order.current，检查基本响应形态和归属。它不打开活动SQLite，认证token仅保留在内存，摘要只有action/code/requestId。合法无active forecast不阻塞。模拟器初始paused仍周期执行simulator.status，并刷新ready状态文件。

## 真实三程序证据

构建目录：`/home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0`。实际环境为Ubuntu、Python 3.13.3及已有Qt依赖；服务端headless、其余GUI offscreen，腾讯Key均为无效离线占位值，未操作地图或调用腾讯在线服务。

| 运行目录（均位于当前源码`runtime/demo-runs/`） | 实际结果 |
|---|---|
| `live-20260906-211722-53ad76f3` | 全新reset→三程序start→等待5.2秒确认ready跨周期刷新→BASIC_SMOKE_PASS→stop |
| `live-20260906-211728-cc04a2ca` | 坏模拟器token真实返回SIMULATOR_AUTH_FAILED；只有服务端和模拟器启动，反序回滚均EXITED，之后幂等stop |
| `live-20260906-211802-ea8e7b34` | CTest中再次执行真实成功流程 |
| `live-20260906-211809-908f9fee` | CTest中再次执行坏token回滚 |
| `live-20260906-212215-f2021aa2` | 最终状态字段校验补修之后，CTest真实成功流程 |
| `live-20260906-212222-a7603139` | 同次最终CTest坏token回滚 |

成功轮次三个身份、失败轮次两个身份均已通过`/proc`核对为EXITED，manifest最终STOPPED；失败清单仍保留rollbackResults。旧run、数据库、日志和smoke报告均留存，未清空或重用任何轮次。停服后以SQLite只读连接验证integrity_check=ok、foreign_key_check为空、6站48桩；成功轮次request_log含真实simulator.status记录。

首次live执行的真实启动/smoke/stop已通过，但测试误把日志表写成`action_logs`，停服核对阶段报`sqlite3.OperationalError: no such table: action_logs`，当次结果1 failed/1 passed。查阅已有schema确认实际表名为`request_log`后，仅修正测试查询，再执行结果2 passed。首次失败证据目录`live-20260906-210829-087dc9c2`及配对坏token轮次`live-20260906-210836-28fcd53a`同样保留且进程已退出，未通过删目录掩盖失败。

## 验证命令与结果

```bash
python3 -m pytest tests/scripts/test_demo_runtime.py -q
EV_DEMO_BUILD_DIR=/home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 \
  QT_QPA_PLATFORM=offscreen python3 -m pytest tests/scripts/test_demo_runtime_live.py -q -s
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
python3 -m pytest database/tests -q
```

首次完整回归为unit102 passed、真实live2 passed、CTest32/32（42.16秒）、数据库15 passed（1.13秒），构建退出0且`ninja: no work to do.`。自审补PREPARED立即记录源码指纹后，定向全unit103 passed（18.15秒）和live2 passed（8.23秒）。随后发现状态文件updatedAt为数字/空值会产生未捕获异常、schemaVersion=true被误收，先确认3个RED，再加入严格字段校验，4项GREEN。

最终生产修订的全量CTest再次32/32通过（39.51秒）：其中`demo_runtime_unit`实际107 passed（18.73秒），`demo_runtime_live`实际2 passed（8.37秒）。这是最后源码修改之后的证据；其后只整理中文文档。首次CMake重配置有现有XKB/Cups可选依赖未找到提示，但配置及构建成功，未安装任何依赖。

## 失败边界覆盖

| 边界 | 关键测试名（`tests/scripts/test_demo_runtime.py`） |
|---|---|
| 任意CWD、空格路径、脚本符号链接、帮助/未知参数 | `test_shell_symlink_arbitrary_cwd`、`test_help_and_unknown_parameter` |
| 重复run保留DB、越界/符号链接、坏hash、源sidecar、已有DB | `test_reset_new_copy_and_duplicate_preserves_db`、`test_reject_run_path_escape`、`test_reset_rejects_damaged_or_existing_targets`、`test_reset_target_file_symlink_and_golden_symlink_rejected` |
| 缺/空Key和token、INI环境优先、非法数值/build | `test_start_missing_or_blank_credentials_before_processes`、`test_config_file_fallback_and_blank_environment_priority`、`test_start_invalid_numbers_before_processes`、`test_start_checks_native_build_and_targets` |
| 启动重复/已有记录、默认GUI与host参数 | `test_start_success_duplicate_and_stop`、`test_start_rejects_prepared_with_existing_process_record`、`test_start_default_gui_host_port_seed_interval_and_redaction` |
| 旁观监听、预检后抢占端口、服务端失败/health超时、sim拒绝、client即退 | `test_start_refuses_unrelated_port_listener`、`test_port_race_external_healthy_listener_not_accepted`、`test_start_stage_failures_rollback_preserve_records` |
| PID/starttime/bootId/exe不匹配不杀旁观者，并继续其他端 | `test_identity_mismatch_never_signals_bystander`、`test_stop_continues_other_correct_records_after_wrong_pid` |
| TERM默认不KILL、force身份校验、回滚未退出记录保留 | `test_stop_timeout_no_kill_and_force_checked`、`test_start_failure_default_term_preserves_unexited_record`、`test_identity_capture_failure_preserves_unknown_child_record` |
| 同仓库互斥、pidfd不支持明确失败、PREPARED停服不复用 | `test_commands_hold_same_repository_flock`、`test_stop_pidfd_unsupported_explicitly_no_unsafe_fallback`、`test_prepared_stop_is_idempotent_but_not_reusable` |
| 错requestId/坏envelope/长度上限/总超时/非ready health | `test_protocol_rejects_wrong_id_and_bad_envelope`、`test_protocol_total_timeout_on_slow_fragmented_response`、`test_health_rejects_nonready_state` |
| ready过期/旧PID/非ready、smoke失败报告、只读动作与业务归属 | `test_ready_rejects_stale_wrong_process_and_nonready`、`test_smoke_old_process_cannot_be_rescued_by_fresh_ready`、`test_cli_smoke_reports_tcp_and_status_failures`、`test_business_smoke_validates_shape_and_ownership`、`test_smoke_readonly_actions_and_summary` |
| 坏manifest/INI不回显内容，归属错误smoke仍报告 | `test_malformed_manifest_returns_json_failure`、`test_start_bad_ini_configuration_returns_json`、`test_smoke_manifest_owner_mismatch_writes_failure_report` |
| PREPARED源码指纹、状态JSON严格类型 | `test_reset_records_source_fingerprint_already_when_prepared`、`test_ready_malformed_fields_are_rejected_without_traceback` |

测试替身只位于tmp_path精确测试仓库中，用于可控退出、拒绝、超时和竞态；生产模块没有test-only参数。真实三程序live为独立门禁，不能由替身结果替代。

黄金库SHA-256在本批前后均为`5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7`；optional `runtime/golden/demo.db`仅只读计算摘要，前后均为`97138f35a68e5f0963dd715e570a2bb27696731d883e22df80d339fd62ca12ad`。没有读取、检查或编辑任何xlsx/人工需求矩阵；源码脏状态采集显式排除xlsx。

## 仍未通过的独立门禁

人工管理员/用户端登录、实际模拟器Run计量演示、腾讯驾车/步行导航、完整充电/故障/结算双彩排、另一台机器独立clone与构建均不由此批通过。`rehearse_demo.sh`和`release_check.sh`仍未实现。本批不输出发布GO，不推送、不建PR、不合并。文件系统若持续禁止写manifest，工具不能保证持久化新状态；其正常回滚会保留可确认身份且默认只TERM，操作人须依据失败输出与已有记录处理，不将旧观测文件当作持续健康证明。
