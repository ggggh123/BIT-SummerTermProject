"""团队 Ubuntu 22.04 基线预检；不安装软件，不读取业务配置。"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


QT_MODULES = ("Qt6Core", "Qt6Gui", "Qt6Network", "Qt6Sql", "Qt6Widgets", "Qt6WebEngineWidgets",
              "Qt6OpenGL", "Qt6Test")


def probe(args):
    try:
        result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=15)
        return result.returncode, result.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return 1, "命令不可用或超时"


def version(value):
    match = re.search(r"\d+(?:\.\d+)+", value)
    return tuple(map(int, match[0].split("."))) if match else ()


def inspect_environment(strict=False, with_web=False, with_ml=False,
                        os_release=Path("/etc/os-release")):
    rows = []

    def add(ok, message, warning=False):
        rows.append(("OK" if ok else "WARN" if warning else "MISSING", message))

    try:
        info = dict(line.split("=", 1) for line in os_release.read_text().splitlines()
                    if "=" in line and not line.startswith("#"))
        info = {k: v.strip('"') for k, v in info.items()}
    except OSError:
        info = {}
    baseline = info.get("ID") == "ubuntu" and info.get("VERSION_ID") == "22.04"
    add(baseline, f"系统 {info.get('ID', 'unknown')} {info.get('VERSION_ID', '?')}；团队基线 Ubuntu 22.04",
        warning=not strict)
    for name in ("git", "cmake", "ninja", "g++", "qmake6", "python3", "pkg-config"):
        add(bool(shutil.which(name)), f"工具 {name}")
    for name, args, minimum in (
        ("CMake", ["cmake", "--version"], (3, 22)),
        ("GCC", ["g++", "-dumpfullversion"], (11,)),
        ("Python", ["python3", "--version"], (3, 10)),
    ):
        code, output = probe(args)
        add(code == 0 and version(output) >= minimum,
            f"{name} {output.splitlines()[0] if output else '?'}；最低 {'.'.join(map(str, minimum))}")
    qt_versions = set()
    # Jammy 的 Qt 6 开发包不提供 .pc 文件，qtpaths6 也未必有 /usr/bin 链接。
    # 使用系统 qmake6 查询安装布局，再检查实际 CMake 开发包元数据。
    libs_code, libs_path = probe(["qmake6", "-query", "QT_INSTALL_LIBS"])
    for module in QT_MODULES:
        metadata = Path(libs_path) / "cmake" / module / f"{module}ConfigVersionImpl.cmake"
        config = metadata.with_name(f"{module}Config.cmake")
        try:
            content = metadata.read_text() if libs_code == 0 and Path(libs_path).is_absolute() and config.is_file() else ""
        except OSError:
            content = ""
        match = re.search(r'set\(PACKAGE_VERSION\s+"([\d.]+)"\)', content)
        output = match[1] if match else "未安装"
        v = version(output)
        add((6, 2) <= v < (7,), f"{module} {output}，要求 Qt 6.2+")
        if v:
            qt_versions.add(v)
    if qt_versions:
        add(len(qt_versions) == 1, "Qt 各模块版本必须一致，不能混用不同安装来源")
        add(all(v[:2] == (6, 2) for v in qt_versions),
            "团队验证系列为 Qt 6.2；较新 Qt 编译成功不能替代基线验证", warning=not strict)
    for query, files in (
        ("QT_INSTALL_PLUGINS", ("platforms/libqxcb.so", "sqldrivers/libqsqlite.so",
                                "iconengines/libqsvgicon.so", "tls/libqopensslbackend.so")),
        ("QT_INSTALL_LIBEXECS", ("QtWebEngineProcess",)),
        ("QT_INSTALL_DATA", ("resources/qtwebengine_resources.pak",)),
        ("QT_INSTALL_TRANSLATIONS", ("qtwebengine_locales/en-US.pak",)),
    ):
        code, output = probe(["qmake6", "-query", query])
        for relative in files:
            add(code == 0 and Path(output).is_absolute() and (Path(output) / relative).is_file(),
                f"Qt 运行资源 {query}/{relative}")
    code, _ = probe(["python3", "-c", "import pytest"])
    add(code == 0, "Python pytest（完整回归需要）")
    if with_web:
        code, output = probe(["node", "--version"])
        add(code == 0 and version(output) >= (18,), "可选 HTML/Web 测试需要 Node.js 18+；Jammy 默认源版本可能不足")
        add(bool(shutil.which("npm")), "可选工具 npm")
    if with_ml:
        code, _ = probe(["python3", "-c", "import numpy, pandas, sklearn, joblib"])
        add(code == 0, "可选 ML 科学计算包")
    return rows


def main():
    parser = argparse.ArgumentParser(description="Ubuntu 22.04 / Qt 6.2 团队环境预检")
    parser.add_argument("--strict", action="store_true", help="系统必须为 Ubuntu 22.04，Qt 必须为 6.2 系列")
    parser.add_argument("--with-web", action="store_true", help="额外检查 Node.js 18+ / npm")
    parser.add_argument("--with-ml", action="store_true", help="额外检查 ML Python 包")
    args = parser.parse_args()
    rows = inspect_environment(**vars(args))
    for status, message in rows:
        print(f"[{status}] {message}")
    if any(status == "MISSING" for status, _ in rows):
        print("预检失败：先按 docs/development/ubuntu22.md 补齐依赖或修正工具链，不要升级整套系统绕过。")
        return 1
    print("预检通过（如有 WARN，当前主机不等同于团队基线）。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
