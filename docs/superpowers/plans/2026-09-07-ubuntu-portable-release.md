# Ubuntu 22.04 便携发行 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 生成队员可直接解压运行的 Ubuntu 22.04 x86_64 三端发行包。

**Architecture:** 22.04 独立用户空间编译，私有 Qt／Python 运行库随包分发。发行启动适配器复用现有 Runtime 与进程协议，把包内只读资源和 XDG 用户运行数据分离；最终包内注入地图默认配置。

**Tech Stack:** C++17、Qt 6、SQLite、Python 3、ELF、tar.gz、Ubuntu Jammy。

**Spec:** `docs/superpowers/specs/2026-09-07-ubuntu-portable-release-design.md`，用户已在当前对话批准执行。

## Global Constraints

- 最低兼容目标为 Ubuntu 22.04；另在本机 Ubuntu 25.04 上回归，不把未测的所有更高版本统称为已验证。
- 不执行 CMake、Ninja、Make、编译器或 Git（发行包运行阶段）。
- 默认地图 Key 只在本地打包阶段注入最终配置，不写入源码、设计文档、测试快照或公开 CI 日志。
- 本轮不改变模拟器默认暂停的业务行为。
- 不触碰人工维护的需求矩阵，不清理已有工作树、运行库或历史成果，不自动推送远端。
- 不更改本机现有全局 Qt 环境；系统 glibc 不替换。
- 用户执行中新增约束：不下载完整 Ubuntu 22.04 镜像，只做本机测试，不另建独立验收系统。已下载的约 29 MB Ubuntu Base 用于本机兼容构建。

## 文件与接口分工

| 任务 | 文件 | 职责 |
|---|---|---|
| 1 | `scripts/release/portable_runtime.py`、`portable_launcher.py`、`tests/release/test_portable_runtime.py` | 发行身份、配置、私有状态、三端启动与停止；子代理负责 |
| 2 | `scripts/release/package_release.py`、`tests/release/test_package_release.py` | 从 22.04 安装根和构建结果收集运行依赖、资源、校验与许可；主代理负责构建及打包，独立审查 |
| 3 | 必要的 C++ 兼容小修及相关既有测试 | 22.04 实际编译出现的 API／资源问题，按失败证据处理 |
| 4 | `docs/release/portable-release.md`、`docs/test/portable-release-2026-09-07.md` | 发行说明、实际包的验收证据与剩余限制 |

统一 `release.json`：`schemaVersion:1`、`releaseId`、`sourceCommit`、`platform:"ubuntu22.04-x86_64"`、`binaries`。其中 `binaries` 的键为 `server`、`simulator`、`client`，值含包内相对 `path` 与 `sha256`；路径为 `bin/ev_admin_server`、`bin/ev_charger_simulator`、`bin/ev_user_client`。允许清单携带构建日期、Qt版本、依赖清单等元信息。

启动入口调用：`python/bin/python3 support/portable_launcher.py start|stop|status [--software-rendering]`。包根由 `portable_launcher.py` 所在目录上一级定位。打包器同时复制 `scripts/demo_runtime.py`、`demo_processes.py`、`demo_protocol.py` 到 `support/`，所需数据库 Python 模块在 `database/`。

## Task 1: 发行运行适配器

**Files:** 新建 `scripts/release/portable_runtime.py`、`scripts/release/portable_launcher.py`、`tests/release/test_portable_runtime.py`。

**Interfaces:** `PortableRuntime(bundle: Path, data_home: Path | None = None)`；`release.json` 使用上述字段；包内配置为 `config.local.ini`；XDG 配置覆盖为 `<data_home>/evcharging/<releaseId>/config.local.ini`。提供 `start`、`stop`、`status` 命令，默认端口 9100、遥测间隔 3000ms、seed 20260901；使用现有健康协议，不在测试中调用腾讯真实服务。

- [ ] 写行为测试：无 Git／CMake 缓存时读取程序；篡改程序拒绝启动；配置覆盖不泄漏 Key；用户目录分离；停止后搬移包仍可读历史；已经停止的旧记录不阻塞；端口冲突不启动；进程早退有清理。测试真实临时文件，不用源码字符串断言。

```python
def test_verified_binaries_need_no_build_cache(bundle, tmp_path):
    runtime = PortableRuntime(bundle, data_home=tmp_path / "用户数据")
    build, binaries = runtime.build_info(bundle)
    assert set(binaries) == {"server", "simulator", "client"}
    assert build == bundle.resolve()
    assert not (bundle / "CMakeCache.txt").exists()
```

- [ ] `python3 -m pytest tests/release/test_portable_runtime.py -q`：先看到功能缺失导致的失败，再实现。
- [ ] 以现有 `Runtime` 为基础改写资源定位、`fingerprint`、`build_info`、配置及用户目录适配；不复制整份启动／协议状态机。允许对既有 Runtime 增加可覆盖的小型资源访问入口，若需要修改先通知主代理避免文件冲突。

```python
def fingerprint(self):
    return {"sourceCommit": self.release["sourceCommit"],
            "releaseId": self.release["releaseId"],
            "sourceDirty": False,
            "sourceDirtyScope": "发行清单与二进制指纹"}
```

- [ ] 命令行输出中文；无需外部 token，为本轮生成认证值并传给父 Runtime 的启动流程；`status` 不输出 Key 或 token。默认配置错误、无桌面、端口占用、运行中的包被搬移等情况明示原因。
- [ ] 同时运行 `tests/scripts/test_demo_runtime.py` 回归；记录 RED／GREEN、完成任务文件范围内提交，接受独立审查。

## Task 2: 兼容构建与依赖收集

**Files:** 新建 `scripts/release/package_release.py`、`tests/release/test_package_release.py`；构建文件保存在 `/home/hushengyuan/ev-release/ubuntu22/`。

**Interfaces:** 打包器 CLI 接收 `--sysroot`、`--build-dir`、`--source-dir`、`--output-dir`、`--config-file`、`--release-id`、`--source-commit`。`build-dir` 是宿主可读的构建树，`sysroot` 是 Jammy 根目录；只输出新建目录，已存在则拒绝。配置文件由用户授权的本地配置提供，不记录其内容。

- [ ] 下载 Ubuntu Base 22.04 amd64、校验官方签名与 SHA-256；在明确的独立根目录解包，安装 Jammy 的编译器、CMake、Qt6 base／WebEngine、SQLite 插件、字体和 Python 等构建／运行依赖。
- [ ] 复制显式源码文件清单（排除矩阵、配置、运行数据、Git）到本地 ext4 构建区，运行以下流程；既有测试保留，发行只收三个可执行目标。

```sh
cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /build --parallel 4
ctest --test-dir /build --output-on-failure -j 4
```

- [ ] 先写打包行为测试：拒绝覆盖、默认配置不进入元信息、缺少 WebEngine／程序资源失败、清单程序哈希正确、含空格路径、包内相对路径无逃逸。

```python
def test_package_refuses_existing_output(tmp_path):
    output = tmp_path / "existing"
    output.mkdir()
    with pytest.raises(FileExistsError):
        prepare_output(output)
```

- [ ] `python3 -m pytest tests/release/test_package_release.py -q`，确认 RED 后实现依赖收集器。程序与插件用 22.04 根内动态依赖解析，复制递归依赖；排除 glibc、loader 和图形驱动本体，不排除应用实际需要的 Qt／ICU／SSL 等运行库。复制实际 WebEngine 资源、Python 标准库和动态扩展；记录来源包和第三方版权。
- [ ] 写入局部环境启动壳与 `qt.conf`；Python 私有前缀、Qt 资源路径、字体配置只作用于应用进程。提供 `--software-rendering` 参数。QtWebEngineProcess 必须与 Qt 库版本一致。

```sh
exec "$bundle/python/bin/python3" "$bundle/support/portable_launcher.py" start "$@"
```

- [ ] 默认 Key 注入配置；生成 `release.json`、`SHA256SUMS` 与包外 SHA-256。`config.local.ini` 属于可替换配置，不参与启动时不可变内容校验。可读发行根内不带任何当前轮次文件。
- [ ] GREEN、独立任务审查；压缩包只有验收后才作为最终结果交付。

## Task 3: 必要兼容修正

**Files:** 实际失败指向的 `apps/`、`simulator/` 小范围实现与其既有测试，不预先大规模改造。

**Interfaces:** 所有协议、数据字段和 UI 行为保持不变；构建使用 Qt 最低 API。

- [ ] 捕获编译或运行失败原文与最小触发路径，先确定根因。
- [ ] 编译 API 问题用 Jammy 构建命令作为失败复现；行为问题先增加自动测试。
- [ ] 使用等价写法。例如新版本 `QDateTime` 重载不可用时采用已支持重载并保持 UTC 语义；不得调整断言来跳过失败。
- [ ] 运行具体目标／测试，再回归全套；独立审查修正后重打包。

## Task 4: 最终包验收与交付

**Files:** 新建 `docs/release/portable-release.md`、`docs/test/portable-release-2026-09-07.md`；过程日志与截图保存在本次发行构建区，选定最终证据归档。

- [ ] 不再另建 Jammy 验收根；在本机提取同一个压缩包，分别在普通、空格和中文路径启动，限制开发工具搜索路径。`readelf`／`ldd` 检查不缺库，并核对实际加载路径，不把本机验证冒充全新 22.04 虚拟机验收。
- [ ] 验证启动、停止、重复启动、端口占用、失败回收、移动包后重启；记录新运行副本和黄金校验值。
- [ ] 真实运行三端 UI 并截图，人工／自动业务动作验证登录、充电遥测增长、停止结算、历史和在线复位。
- [ ] 使用默认配置做一次真实腾讯地址解析和路线加载；只记录结果，不记录 Key。失败时区分网络、额度、权限或部署故障。
- [ ] 同包在本机 25.04 运行验证；如果验收使用共享宿主内核／显示会话，报告如实标注，不能宣称独立完整 22.04 虚拟机测试。
- [ ] 写中文使用说明：解压、`bash 启动.sh`、演示账号、点击 Run、停止、数据目录、地图网络依赖与故障日志位置。
- [ ] 最终 whole-branch 审查；修正问题后重新验收受影响项。交付包、哈希、说明、验证结果与未验证边界，不自动推送仓库或上传含 Key 包。
