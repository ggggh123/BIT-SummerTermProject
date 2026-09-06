# Qt 充电与结算界面收尾（2026-09-06）

## 范围与依据

在现有 `feat/core-delivery-20260906` 集成 worktree，继续用户已批准的 Linux 原生 Qt、390×844 / 390×720 竖屏青绿方向。本批是现有 ChargePage 的有限视觉落地，不生成新设计、不改变业务 API，不安装环境、不操作需求矩阵，不推送或合并远端。

参考已留档 V2《03-charging.png》及 V3《06-settlement.png》和《视觉与交互基准》；遵守基准中的统一规则而非逐像素复制生成图。主要变化：

- 充电中突出服务端已充电量；停止后突出应付金额；完成后显示已结算金额。
- 卡片保留站名、桩编码、订单 ID；时长为服务端 `elapsedSec` 格式化的累计“分:秒”，不是本机计时器，也不在 60 分钟回绕。
- 选择/预约/充电/待结算/完成/取消各显示相关操作；继续由原连接、会话、mutation、facts/reconciliation guard 决定能否操作。无关按钮隐藏，不只变灰。
- 待结算仍是 `status=charging` 且 `endedAt` 非空，不新增 wire state；点击后等待服务端确认，不先显示支付成功。
- 错误、断线旧数据、提交中、核验中都有中文提示；正常状态不保留空错误块。主要操作放在长时间凭据之前，小屏可滚动查看凭据和说明，底栏保持独立。
- 时间记录保留服务端原值与时区。订单未提供成交单价，因此不把站点现价当作本单单价；只有选桩阶段显示“站点挂牌价”。不新增 SOC、电流、电压、瞬时功率或真实支付信息。

使用现有 `:/ui/charger.svg` 和既有底栏资源、字体、主题，没有从设计稿裁切控件，也没有创建新的 Superdesign 画布。首批登录、找站、详情资源不动；历史、账户、导航的视觉收尾仍待后续。

## 测试与实际结果

1. 先补 6 种状态的真实 ChargePage 可见性测试：原实现全部失败，原因是每种状态都显示所有按钮。完成呈现层修改后，六行及既有权威字段用例通过（含初始化/清理 **9 passed，0 failed，34 ms**）。随后完整回归也包含这些用例。
2. 新 MainWindow 回环 TCP 测试从真实登录、恢复当前订单、桩归属核验进入充电/待结算；720/844 高度及长站名/长桩码共 6 行，检查金额/电量/时长、横向边界、主操作滚动可达、当前订单入口、禁止找桩、实际 stop/settle payload、提交中禁重复与无乐观成功。定向 **8 passed，0 failed，1392 ms**（含初始化/清理）。
3. 截图检查发现空错误 QLabel 留下 52 px 空隙；新增真实几何断言先 RED（52 > 32），隐藏空错误块后同用例 GREEN。非空错误仍保留在操作前，原异常与重连测试继续执行。
4. 首次 UI 全量 **28/29**：`user_api` 中同一旧响应竞态函数的 8 个数据行仍断言旧单段 meter 字符串。改为分别断言电量、时长和金额，并保留迟到响应不覆盖、随后新 poll 能更新三个值的验证；未改服务端数据、会话逻辑、乱序注入或测试时序。
5. 最终组合构建成功，CTest **29/29，39.89 秒**。同时地图 HTML 离线 **15/15，277.08 ms**、环境脚本 **13/13**、数据库 **15/15，1.13 秒**。默认 CTest 未添加腾讯在线调用。

```bash
cmake --build /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0 --output-on-failure -j4
node --test apps/user-client/tests/test_navigation_html.mjs
bash tests/scripts/test_check_env.sh
python3 -m pytest database/tests -q
```

可复现的截图入口（在仓库根运行；`EV_UI_SCREENSHOT_DIR` 应选择新目录以保留旧证据）：

```bash
QT_QPA_PLATFORM=offscreen EV_UI_SCREENSHOT_DIR="$PWD/runtime/charge-ui-new-captures" \
  /home/hushengyuan/.cache/ev-core-fixed-integration-build-SiLdj0/apps/user-client/tst_user_mobileui restoredChargeMetricsAndActionsFitPortrait
```

Qt offscreen 仍有 `propagateSizeHints()` 提示，不称作“零警告”。全部截图来自真实 Qt 控件，但业务来源为回环测试响应，不能冒充真实服务端/数据库/模拟器共同运行或人工彩排。

## 实际视觉检查

检查主数字、主题色、中文、按钮、底栏、滚动和长文本。对照同为充电中/待结算的参考状态，不拿尚未预约的旧截图作充电中对照。原稿的装饰步骤圆圈改为清晰步骤文案；无来源的订单成交单价不展示；这两点为明确的原生 Qt/真实数据适配。

| 场景 | 留档 | 检查结果 |
|---|---|---|
| 充电中，390×720 | [截图](evidence/user-charge-ui-2026-09-06/charging-390x720.png) | 大号电量、时长、金额及停止动作可见，时间凭据可继续滚动 |
| 待结算，390×720 | [截图](evidence/user-charge-ui-2026-09-06/settlement-390x720.png) | 应付金额清晰，未显示已支付，确认动作在底栏之上 |
| 充电中，390×844 | [截图](evidence/user-charge-ui-2026-09-06/charging-390x844.png) | 常规内容及说明可见，底栏固定在独立区域 |
| 待结算，390×844 | [截图](evidence/user-charge-ui-2026-09-06/settlement-390x844.png) | 停止/时间/金额一致，主次层级与批准方向一致 |
| 长站名/桩码，390×720 | [顶部](evidence/user-charge-ui-2026-09-06/settlement-long-390x720.png)、[操作位置](evidence/user-charge-ui-2026-09-06/settlement-long-action-390x720.png) | 自动换行，无横向越界，滚动后主按钮完整可点击 |
| 长站名/桩码，390×844 | [顶部](evidence/user-charge-ui-2026-09-06/settlement-long-390x844.png)、[操作位置](evidence/user-charge-ui-2026-09-06/settlement-long-action-390x844.png) | 纵向内容保持可达，不遮挡底栏 |

本批截图检查通过，不代表所有客户端页面视觉已完成，也不宣称已完成真服务器/腾讯导航联动、换机验证或同 SHA 双彩排。独立代码评审结论另行追加。
