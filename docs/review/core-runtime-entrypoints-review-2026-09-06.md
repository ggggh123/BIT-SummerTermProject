# 核心运行入口评审与裁定记录

> 最终状态：本批运行交付的任务评审、最终修复复审和主控全量复验均已通过；生产候选 `2876575`，无开放功能阻断。后续提交仅补本文等记录。本结论不是整项目 release GO，也不表示已同步云端。

本批工作树：`worktrees/core-integration`；分支 `feat/runtime-delivery-20260906`，从 `f0b8d4a` 开始。本文记录本地工程评审，不表示已推送、合并或整项目发布 GO。

实施期间刷新远端：`origin/dev` 仍为 `97c6da1`；`origin/main` 新增 `22b146c` 图文概要设计 DOCX。此次仅核对提交和文件元信息，没有读取该 Word 文档或任意 xlsx，也没有自动同步 main/dev。本批代码进展仍在本地候选中。

## 当前评审状态

| 范围 | 提交 | 结果 |
|---|---|---|
| Task 1：模拟器运行状态与 token 环境配置 | `40d8db5..4f05de2` | 首轮发现两项 Important，进入修复 |
| Task 1：运行期写失败修复 | `4f05de2..c939cd2` | 限定复审：I1/I2 均 ADDRESSED，无新增 Critical/Important/Minor |
| Task 2：四个运行入口 | `2a7465a..22c9163` | 首轮发现 I1（空服务端记录绕过新运行检查）；M1 诊断摘要优化留最终评审 |
| Task 2：空记录修复 | `22c9163..1ce8342` | 限定复审 I1 ADDRESSED，无新增破坏；任务门禁通过 |
| 全分支运行交付评审 | `f0b8d4a..4278cc0` | With fixes：F-I1 空白 INI Key 判空；M1 保持 Minor，主控决定同波修复 |
| 最终单次修复复审 | `4278cc0..2876575` | F-I1/M1 均 ADDRESSED，无新增破坏或范围外问题 |
| 主控最终全量复验 | 生产候选 `2876575` | 构建成功、CTest32/32、数据库15/15、黄金摘要未变 |

## Task 1 评审闭环

首次评审确认就绪边界、原子 JSON、事件生命周期及 token 优先级，但发现：

1. **I1：运行期写失败可能残留刚发布的 ready。** 原实现仅非零退出，`QSaveFile` 保留旧文件。修复在成功发布后记录归属；后续写失败尽力撤销本实例发布的文件，撤销被阻止时明确报告。首次写入从未成功，不删除原有外部文件。
2. **I2：只有启动期失败测试，没有运行期自动覆盖。** 新用例先通过真实 `SimulatorClient` 和回环 TCP 收到成功状态回包，再对测试自有子进程施加写入限制。验证 writer 的失败信号、旧文件撤销、撤销受阻说明及生产模拟器的 `NormalExit + exitCode=1`，不是把信号终止当作正确错误处理。

主控核对未修改的 `SimulatorClient.cpp`：成功解析 chargers 数组后，先更新 TelemetryEngine 和通知 charger snapshot，再发出 sessionReady。状态文件只含 schemaVersion、pid、sessionState、updatedAt，不含 token/key。

修复证据：旧实现运行新增用例为 13/16（3 项预期失败）；修复后为 16/16，模拟器 CTest 4/4；完整构建及当时全量 CTest 30/30。主控核对了完整执行日志与排除 xlsx 的 diff 检查。此结果不替代 Task 2 的真实组合测试。

既有 Qt offscreen `propagateSizeHints()` 警告记录为非阻断 Minor，未声称测试输出完全无警告。最终评审还需判断本批是否应处理。

## Task 2 首轮主控复验

在 `22c9163` 上独立构建退出 0，全量 CTest **32/32（39.37 秒）**，其中脚本 unit 107 项、真实三进程 live 2 项；数据库 pytest **15/15（1.22 秒）**。精确 diff 检查通过，core/optional 黄金摘要与实施前相同。

主控生成并保留了 `live-20260906-212914-8e500a6d`（成功流程）和 `live-20260906-212921-07ed6860`（坏模拟器 token 回滚），均在 `runtime/demo-runs/` 下。测试核实停止后身份均 EXITED、SQLite 完整性/外键及 6 站48桩；日志没有被删除。这些是 Qt offscreen 和 TCP 证据，不代表人工 UI 或在线地图验收。

独立 Task 2 评审仍找到全套未覆盖的 I1：`processes.server` 存在但为空值时，旧代码依据 truthiness 跳过未知服务端阻断。该项需要补测和修复后才能通过，不能用 32/32 掩盖。另有 M1：合法失败响应的实际错误码未进入请求摘要，作为 Minor 交最终全分支评审裁定。上述测试结果只覆盖 `22c9163`，不提前覆盖其后修订。

`1ce8342` 已修复 I1：依据 server 键存在判断，不再依据记录内容的 truthiness。四种空坏记录分别通过实际 CLI 的 reset/start 测试，共 8 例先全部 RED，再 GREEN；连同已有回收和正常流程，定向 **16 passed、99 deselected**。独立限定复审确认 I1 已解决、无新增破坏，Task 2 门禁通过。最终全量验证仍需覆盖这个修订。

## 最终整批评审与单次修复波次

最终评审固定覆盖 `f0b8d4a..4278cc0` 的 26 个文件，不重开旧中止诊断或历史服务端分支。发现 F-I1：INI 的 `mapKey="   "` 在去引号后保留空格，启动器接受，但 Qt 再次 trim 后判空；用户端显示错误却不退出，故进程存活不能补救配置漏检。评审以受控内存 INI 对实际 configuration 方法复现，没有读取真实配置或启动程序。

本批最终一次修复波次将最小修复去引号后的判空，并以实际 CLI 断言无进程启动。同时处理 M1：只保留有限格式/长度的真实响应 code，不能将任意 message/data/token 写入报告。旧 offscreen 警告继续延期，评审确认其不是本批阻断。

上述波次已在 `2876575` 完成：quoted 空白 Key 先 RED 后拒绝；合法 AUTH_REQUIRED 先复现丢失，再能进入摘要。错误码仅允许 ASCII `[A-Z][A-Z0-9_]{0,63}`，坏 envelope、错误 requestId、不合格式或超长 code 仍为 PROTOCOL_ERROR；message/data/token 不落入摘要。定向 33 项通过。唯一限定复审确认 F-I1/M1 均关闭，无新增问题；此前 M1 延期待办已完成，不再作为开放项。

## 最终候选全量证据

主控在最终生产候选 `2876575` 上执行：

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
python3 -m pytest database/tests -q
sha256sum runtime/golden/core.db runtime/golden/demo.db
```

- 构建退出 0；CTest **32/32，39.34 秒**。
- CTest 内脚本 unit **121 passed，21.68 秒**；真实三程序 live **2 passed，8.27 秒**；数据库 pytest **15 passed，1.11 秒**。
- core 黄金 SHA-256：`5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7`；optional demo.db：`97138f35a68e5f0963dd715e570a2bb27696731d883e22df80d339fd62ca12ad`，均与实施前一致。
- 最终真实成功轮次 `live-20260906-215851-47aa4ccd`，坏 token 回滚轮次 `live-20260906-215858-07e088a0`。均保留在 `runtime/demo-runs/`；主控再读清单/进程身份确认 STOPPED、全部 EXITED。成功轮次覆盖跨周期 ready 刷新、BASIC_SMOKE_PASS；停止后完整性/外键、6站48桩与实际 simulator.status 记录均经测试核对。
- 四 shell 的 `sh -n` 与排除 xlsx 的本批完整 `git diff --check` 通过；Git 文件模式四项均为 100755。

没有通过以上自动测试代替人工 GUI、腾讯在线导航、换机独立 clone 或同提交两次完整业务彩排。既有 offscreen 平台警告仍明确留档；源码 SHA 与二进制指纹独立记录，不冒充独立打包或重建证明。

## 主控裁定（按发生顺序）

1. **Shell 入口 + Python 标准库编排。** 项目已有 Python，JSON、截止时间及进程身份处理可集中测试。若不适用，代价是需要维护或重写 Python 编排层；四个对外命令可以保持不变。
2. **本批限基础冒烟。** 完整业务、腾讯在线、换机和同提交双彩排仍为独立门禁。理由是本批批准的是运行入口，进程启动证据不等于答辩交付。代价是仍不能宣称 release GO，后续必须补验。
3. **状态文件是快照，不是持续健康保证。** 故障文件系统可能同时拒绝更新与删除，因此 writer 只能尽力撤销、明确报告失败并非零退出。消费者须联合检查同一活进程身份、新鲜时间，并在 start/smoke 返回前复核全部进程。若依赖者只看文件会误认旧状态；单次采样仍有时间竞态，不能描述为持续健康租约。
4. **同波修复非阻断 M1。** 它与 F-I1 同属入口失败处理，能使现场报告区分认证拒绝与协议损坏；只允许短且符合格式的错误码落盘。代价是未来不符合该格式的新错误码仍会回退为 PROTOCOL_ERROR，变更协议时需同步调整。

## 保持不变的边界

- 不读取或修改需求矩阵及任何 xlsx。
- 不调用腾讯在线服务、不输出实值密钥、不安装依赖。
- 不修改核心协议、计费/订单状态机、黄金原件或 optional demo.db。
- Web/ML 保留但不是本批门槛；无 active forecast 的 degraded health 合法。
- 模拟硬件与支付仍按演示边界说明，自动 offscreen 测试不等于人工 GUI/地图验收。
