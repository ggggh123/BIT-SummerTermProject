# 核心交付范围重置验证证据

> 验证时间：2026-09-04 16:51–16:57（Asia/Shanghai）
> 验证主机：`hushengyuan-VMware-Virtual-Platform`（Ubuntu 虚拟机）
> 证据前提交：`fc55668b1ed92d9872dad3f0044626de61574f5e`（`docs(plan): hand off requirement matrix maintenance`）
> 工作树：`/mnt/hgfs/Desktop/SummerTermProject/worktrees/core-scope-rebaseline`
> 原生构建目录：`/tmp/ev-core-scope-build-ezVFkh`（不在 VMware 共享目录，也不进入 Git）

## 结论边界

本记录证明上述提交上的范围文档、环境 profile、数据库测试、页面合同测试以及当前 CMake/CTest 构建图通过了本页所列命令；**不证明核心真实端到端闭环已经完成或 core 已 GO**。当前服务端 P0、模拟器真实接入风险和同一提交两次彩排仍未关闭，具体见本文末尾“已知未修复事项”。

`需求矩阵-第1组-王浩恩.xlsx` 已由队员人工维护。本 Task 未读取、检查、编辑、导出、渲染或验证该工作簿；它也不构成本 PR 的自动验收条件。下列扫描和链接检查均显式排除了所有 `.xlsx` 路径。

## 范围、敏感信息与链接

执行命令：

```sh
rg -n -g '!*.xlsx' '五个系统全部可运行|五系统连续两次|五系统 V1|默认.*Web|默认.*ML' README.md docs database/README.md scripts
git grep -n -E '[A-Z0-9]{5}(-[A-Z0-9]{5}){5}' -- ':!references/**' ':!*.xlsx'
python3 - <<'PY'
import os, re, subprocess, sys
files = [p for p in subprocess.check_output(['git', 'ls-files', '*.md'], text=True).splitlines()
         if not p.lower().endswith('.xlsx')]
pat = re.compile(r'!?(?:\[[^\]]*\])\(([^)\s]+)(?:\s+["\'][^)]*["\'])?\)')
errors = []; checked = 0
for path in files:
    with open(path, encoding='utf-8') as f:
        text = f.read()
    for dest in pat.findall(text):
        dest = dest.strip('<>')
        if not dest or dest.startswith(('#', 'http://', 'https://', 'mailto:', 'tel:', 'data:')):
            continue
        target = dest.split('#', 1)[0]
        if not target:
            continue
        candidate = os.path.normpath(os.path.join(os.path.dirname(path), target))
        checked += 1
        if not os.path.exists(candidate):
            errors.append(f'{path} -> {dest} ({candidate})')
print(f'MARKDOWN_FILES={len(files)} RELATIVE_LINKS_CHECKED={checked} MISSING={len(errors)}')
for page, href in [
    ('docs/design/five-system-architecture.html', 'core-system-architecture.html'),
    ('docs/design/core-system-architecture.html', 'five-system-architecture.html'),
]:
    with open(page, encoding='utf-8') as f:
        present = href in f.read()
    exists = os.path.exists(os.path.normpath(os.path.join(os.path.dirname(page), href)))
    print(f'HTML_LINK page={page} href={href} present={present} target_exists={exists}')
    if not (present and exists):
        errors.append(f'{page} -> {href}')
if errors:
    sys.exit(1)
PY
git diff --quiet origin/dev...HEAD -- references apps shared simulator/src database/schema.sql 'database/*.py'
```

实际结果：

| 检查项 | 结果 | 分类/说明 |
|---|---:|---|
| 真实 Tencent Key 形态 | 0 命中 | 以 `[A-Z0-9]{5}(-[A-Z0-9]{5}){5}` 扫描受跟踪文本，排除 `references/` 和全部 `.xlsx`。 |
| 旧“五系统”口径 | 2 个历史命中 | `docs/plans/2026-09-01-ev-charging-platform-design.md` 第 12、470 行；文件头已明确标注为“历史 v1 基线”，正文保留为决策历史。 |
| “默认 Web/ML”相关命中 | 均为当前口径或验证命令文本 | 现行文档明确 core 默认**不要求** Web/ML；实施计划及本验证记录中的同类字符串是扫描/测试命令，非发布声明。 |
| Markdown 相对链接 | 28 个 Markdown 文件、41 条链接、0 缺失 | 忽略 URL、锚点、邮件/电话/data 链接；只检查仓库内相对目标。 |
| 新旧架构 HTML 互链 | 2/2 存在 | 两个页面均包含相对对方的链接，目标文件存在。 |
| 受限业务路径变更 | 0 | `references/`、`apps/`、`shared/`、`simulator/src/`、`database/schema.sql` 与 `database/*.py` 在 `origin/dev...HEAD` 内均未变更。 |

## 环境、数据与页面合同

执行命令：

```sh
sh tests/scripts/test_check_env.sh
sh -n scripts/check_env.sh
sh -n scripts/bootstrap.sh
scripts/check_env.sh
scripts/check_env.sh --with-web
scripts/check_env.sh --with-ml
scripts/check_env.sh --with-web --with-ml
scripts/check_env.sh --with-ml --with-web
python3 -m pytest database/tests -v
node --test dashboard/tests/*.test.mjs apps/user-client/tests/test_navigation_html.mjs
```

实际结果：

| 验证 | 通过数 | 结果 |
|---|---:|---|
| `tests/scripts/test_check_env.sh` | 13/13 | 通过；并覆盖 core、Web、ML 与两种组合顺序的 profile 边界。 |
| `sh -n`（`check_env.sh`、`bootstrap.sh`） | 2/2 | 通过。 |
| 实际环境 profile | 5/5 | default、Web、ML、Web+ML、ML+Web 均以 0 退出。 |
| `database/tests` pytest | 9/9 | `9 passed in 1.12s`。 |
| dashboard + 用户端导航 HTML Node 测试 | 33/33 | `pass 33`、`fail 0`。 |

## 原生 CMake 构建与 CTest

首次构建使用新的 Linux 原生目录，未使用共享目录中的任何已有 `build` 结果：

```sh
build_dir="$(mktemp -d /tmp/ev-core-scope-build-XXXXXX)"
printf 'BUILD_DIR=%s\n' "$build_dir"
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build_dir" --output-on-failure
```

本次 `mktemp` 实际创建：`/tmp/ev-core-scope-build-ezVFkh`。配置、`--parallel 2` 构建和 CTest 均完成；随后以同一全新目录重跑下列命令以直接复核 CTest：

```sh
cmake --build /tmp/ev-core-scope-build-ezVFkh --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir /tmp/ev-core-scope-build-ezVFkh --output-on-failure
```

第二次构建输出 `ninja: no work to do.`；CTest 实际执行并通过 13/13：

1. `user_formatters`
2. `user_webengine_runtime`
3. `user_tcpjsonclient`
4. `user_api`
5. `user_tencentmap`
6. `user_chargepage`
7. `user_recommendation`
8. `simulator_engine`
9. `simulator_client`
10. `simulator_window`
11. `contracts`
12. `framecodec`
13. `jsonenvelope`

离屏 Qt/WebEngine 运行会输出 Vulkan、GBM/dma-buf 及 `propagateSizeHints` 告警；相应 QtTest 日志均记录 `Test Passed`，CTest 未记录失败。这些主机图形后端告警不构成测试失败，也不应被误写为真实腾讯地图在线联调成功。

## 已知未修复事项与验收限制

- **服务端 P0（阻断 core GO）**：仅有 5/27 action 的实现证据；根 CMake 尚未纳入服务端 target，缺少专属测试；预约→充电→遥测→停止→结算状态机、串行写库以及余额/订单/统计一致性尚未在真实服务端闭合。
- **模拟器两个 P1/开放风险**：模拟器状态/遥测/故障事件尚未与真实服务端完成端到端接入证明；双实例/双路径的现场行为也不能由数据库或当前 CTest 推导。这些风险仍须随核心集成跟踪，不能以本页 13/13 CTest 关闭。
- **用户端会话 guard/race**：计划中已登记会话 guard 风险；本 PR 未修改用户端业务代码，不能将页面合同测试、单进程 QtTest 或现有会话测试扩展解释为真实服务端会话竞态已经消除。
- **Web/ML**：Web 尚无真实 snapshot writer/服务端快照联调；ML 尚未真实发布到服务端。二者是 optional 成果，未纳入 default core gate，不阻塞 core 范围重置，但也不得宣称已完成真实联调。
- **核心真实 E2E**：用户端、真实服务端、黄金库和模拟器尚未在同一候选提交上完成完整链路及连续两次彩排；因此当前 core 结论仍为 **NO-GO**，本记录不能替代后续验收清单的双彩排证据。

## 提交前检查

证据前提交上已执行：

```sh
git diff --check
git status --short
git rev-parse HEAD
```

结果：`git diff --check` 无输出、工作树干净、提交精确为 `fc55668b1ed92d9872dad3f0044626de61574f5e`。本文件是上述证据后新增的唯一受跟踪文件；提交前需再次执行这些命令并确认暂存范围仅为本文件。
