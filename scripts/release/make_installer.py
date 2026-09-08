#!/usr/bin/env python3
"""打包项目当前进度为 Linux 源码安装包（tar.gz）。

产物：runtime/dist/BIT-SummerTermProject-linux-installer-<时间戳>.tar.gz
包内结构：
    BIT-SummerTermProject/            # 项目源码（排除 .git/build/runtime/缓存等）
    BIT-SummerTermProject/install.sh  # 一键入口

用户使用（Ubuntu 22.04 基线，其他 Ubuntu 尽力兼容）：
    tar -xzf BIT-SummerTermProject-linux-installer-*.tar.gz
    cd BIT-SummerTermProject
    bash install.sh                 # 装依赖(apt，需联网与 sudo) → 检查 → 编译 → 三端窗口

说明：apt 系统依赖无法内嵌进 tar（发行版软件包的物理限制），首次安装需联网；
除此之外全部自动化。跨平台注意：tar 在 Windows 侧打包无法设置可执行位，
因此统一用 `bash install.sh` 而非 `./install.sh`。

本脚本与 dev 的 package_release.py（便携运行时收集）互补：那个产出免编译的
自包含运行时树，需要多步 sysroot 前置；本脚本产出"源码 + 一键安装"包，
适合直接发给成员/用户。
"""
import fnmatch
import io
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = ROOT / "runtime" / "dist"

EXCLUDE_DIRS = {
    ".git", ".codebuddy", ".pytest_cache", "__pycache__",
    "build", "runtime", "node_modules", ".vs", ".vscode", "dist",
}
EXCLUDE_PATTERNS = ("*.pyc", "*.moc", "*.o", "*.tar.gz")

# runtime/ 整体排除，但 demo_cli 启动三端必需的黄金库必须随包
# （demo_runtime.py 启动时读取并校验哈希，缺它则编译完成后启动失败）。
GOLDEN_WHITELIST = (
    "runtime/golden/core.db",
    "runtime/golden/core.db.sha256",
)


def git_commit():
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
            capture_output=True, text=True,
        )
        return result.stdout.strip() or "unknown"
    except OSError:
        return "unknown"


def excluded(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    if rel in GOLDEN_WHITELIST:
        return False
    parts = Path(rel).parts
    if any(part in EXCLUDE_DIRS for part in parts):
        return True
    name = path.name
    return any(fnmatch.fnmatch(name, pattern) for pattern in EXCLUDE_PATTERNS)


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M")
    output_path = OUTPUT_DIR / f"BIT-SummerTermProject-linux-installer-{stamp}.tar.gz"
    commit = git_commit()

    install_template = ROOT / "scripts" / "release" / "install.sh"
    install_content = install_template.read_bytes().replace(b"\r\n", b"\n")

    file_count = 0
    with tarfile.open(output_path, "w:gz") as tar:
        for path in sorted(ROOT.rglob("*")):
            if not path.is_file() or excluded(path):
                continue
            arcname = "BIT-SummerTermProject/" + path.relative_to(ROOT).as_posix()
            tar.add(path, arcname=arcname)
            file_count += 1

        # 注入一键入口与安装说明（保证 LF 与包内路径固定）。
        info = tarfile.TarInfo("BIT-SummerTermProject/install.sh")
        info.size = len(install_content)
        info.mtime = int(time.time())
        tar.addfile(info, io.BytesIO(install_content))

        note = (
            "BIT-SummerTermProject Linux 安装包\n"
            f"打包时间：{time.strftime('%Y-%m-%d %H:%M')}  源码提交：{commit}\n\n"
            "使用方法（Ubuntu，团队基线 22.04；其他 Ubuntu 尽力兼容）：\n"
            "  1. tar -xzf BIT-SummerTermProject-linux-installer-*.tar.gz\n"
            "  2. cd BIT-SummerTermProject\n"
            "  3. bash install.sh\n"
            "     （自动：装依赖 → 环境检查 → 编译 → 拉起管理端/用户端/模拟器三端窗口）\n\n"
            "说明：\n"
            "  - 首次安装需要联网安装 apt 系统依赖（需要 sudo 密码）。\n"
            "  - 腾讯地图 Key 已内置，无需配置。\n"
            "  - 请用 bash install.sh 而不是 ./install.sh（打包不含可执行位）。\n"
        ).encode("utf-8")
        info = tarfile.TarInfo("BIT-SummerTermProject/INSTALL-README.txt")
        info.size = len(note)
        info.mtime = int(time.time())
        tar.addfile(info, io.BytesIO(note))

    size_mb = output_path.stat().st_size / (1024 * 1024)
    print(f"打包完成：{output_path}")
    print(f"  文件数：{file_count}  大小：{size_mb:.1f} MB  源码提交：{commit}")
    print("用户侧使用：解压 → cd 包目录 → bash install.sh")
    return 0


if __name__ == "__main__":
    sys.exit(main())
