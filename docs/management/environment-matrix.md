# 团队开发环境基线

更新日期：2026-09-07。默认系统是 **Ubuntu 22.04 LTS x86_64**，不是主开发机的 Ubuntu 25.04。

| 项目 | 团队基线 | 用途 |
|---|---|---|
| Ubuntu | 22.04 LTS，x86_64 | 队友构建、核心验证与最低二进制目标 |
| GCC / C++ | GCC 11，C++17 | 系统编译器 `/usr/bin/g++` |
| CMake | 3.22+，Jammy 为 3.22.1 | 支持仓库 schema 3 构建预设 |
| Ninja | Jammy 系统包 | 日常与测试预设默认生成器 |
| Qt | 6.2 系列，Jammy 包为 6.2.4 | Core、Network、Widgets、WebEngine、OpenGL；完整测试需要 Test |
| Python | 3.10+，系统 `/usr/bin/python3` | 运行脚本、数据库与发行工具测试 |
| pytest | Jammy 系统 `python3-pytest` | Python 回归 |
| 运行依赖 | SQLite、SVG、XCB、TLS 插件；WebEngineProcess、资源和语言包；Noto CJK 字体 | 不只检查编译头文件 |
| Node.js / npm | 可选；导航 HTML 测试需要 Node 18+ | 不阻塞 Qt 三端编译 |
| NumPy / pandas / scikit-learn / joblib | 可选 ML profile | 不属于核心安装与发布硬门槛 |
| Qt Charts | 不需要 | 管理端图表为原生 QWidget 绘制 |

统一使用全局 APT，不创建项目虚拟环境。安装、构建、测试、Qt Creator 与常见错误指引见 [Ubuntu 22.04 开发指南](../development/ubuntu22.md)。

```bash
bash scripts/bootstrap.sh
bash scripts/check_env.sh --strict
cmake --preset ubuntu22
cmake --build --preset ubuntu22
```

完整回归改用 `ubuntu22-test` 配置/构建/测试预设。默认构建并行 2、测试并行 1；不得以日常版没有测试目标冒充全量回归通过。

## 基线、观察值和验证结果分开保存

- 本文件定义团队要求；实际精确补丁版本以每台机器预检及构建日志为准。
- [2026-09-02 主机环境观察](history/environment-observed-2026-09-02.md)原样归档，包含当时 Ubuntu 25.04 / Qt 6.8 和旧依赖分类；不是当前安装清单。
- 较新 Ubuntu / Qt 可以作为额外开发环境，但只在较新环境通过不构成 22.04 兼容证明。不要求改动本机操作系统。
- Jammy Qt 6 不提供新版系统的 pkg-config 元数据，预检改用系统 qmake6 的安装路径及 CMake 模块版本文件。
- `.github/workflows/ubuntu22.yml` 提供 22.04 CI；实际运行状态才是证据，工作流文件本身不是通过证明。
- 不重写旧测试报告、历史计划中的真实版本，不改人工维护的需求矩阵。
