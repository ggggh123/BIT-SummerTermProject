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
import hashlib
import io
import os
import subprocess
import sys
import tarfile
import time
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = ROOT / "runtime" / "dist"

EXCLUDE_DIRS = {
    ".git", ".codebuddy", ".pytest_cache", "__pycache__",
    "build", "runtime", "node_modules", ".vs", ".vscode", "dist", ".venv", "venv",
    ".superpowers", ".workbuddy", ".local-tools", ".idea", ".cache",
}
EXCLUDE_PATTERNS = (
    "*.pyc", "*.moc", "*.o", "*.tar.gz", "*.tar", "*.tgz", "*.zip", "*.7z",
    "*.db", "*.db-wal", "*.db-shm", "*.log", "*.user", "*.user.*",
    "config.local.ini", ".env", ".env.*", "CMakeUserPresets.json",
)

# runtime/ 整体排除，但 demo_cli 启动三端必需的黄金库必须随包
# （demo_runtime.py 启动时读取并校验哈希，缺它则编译完成后启动失败）。
GOLDEN_WHITELIST = (
    "runtime/golden/core.db",
    "runtime/golden/core.db.sha256",
    "runtime/golden/core.manifest.json",
    # 可选成果和完整回归使用的已封存库也保留，不收集任何活动运行副本。
    "runtime/golden/demo.db",
    "runtime/golden/demo.db.sha256",
    "runtime/golden/manifest.json",
)


def git_commit():
    try:
        result = subprocess.run(
            ["git", "-c", f"safe.directory={ROOT}", "rev-parse", "HEAD"], cwd=ROOT,
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
    if any(part in EXCLUDE_DIRS or part.startswith(("build-", "cmake-build-")) for part in parts):
        return True
    name = path.name
    if name.startswith(".") and name not in {".github", ".gitignore", ".gitattributes", ".editorconfig"}:
        return True
    return any(fnmatch.fnmatch(name, pattern) for pattern in EXCLUDE_PATTERNS)


def source_files():
    """先剪枝再遍历，避免扫描大型 build/runtime 树；拒绝把外部链接带入包。"""
    files = []
    for directory, dirs, names in os.walk(ROOT, followlinks=False):
        parent = Path(directory)
        dirs[:] = sorted(name for name in dirs if not excluded(parent / name))
        for name in dirs:
            if (parent / name).is_symlink():
                raise ValueError(f"源码目录包含符号链接，请先核对来源：{parent / name}")
        for name in sorted(names):
            path = parent / name
            if excluded(path):
                continue
            if path.is_symlink():
                raise ValueError(f"源码文件包含符号链接，请先核对来源：{path}")
            if path.is_file():
                files.append(path)
    for relative in GOLDEN_WHITELIST:
        path = ROOT / relative
        # core 三件套缺一即报错；optional 三件套在当前完整仓库中一并收集。
        if not path.is_file() and relative.startswith("runtime/golden/core."):
            raise ValueError(f"缺少核心黄金库工件：{relative}")
        if path.is_symlink() or path.parent.is_symlink() or path.parent.parent.is_symlink():
            raise ValueError(f"黄金库路径不能是符号链接：{relative}")
        if path.is_file():
            files.append(path)
    golden = ROOT / "runtime/golden/core.db"
    expected = (ROOT / "runtime/golden/core.db.sha256").read_text().split()
    if not expected or hashlib.sha256(golden.read_bytes()).hexdigest() != expected[0]:
        raise ValueError("核心黄金库 SHA-256 不一致，拒绝生成不可复现的安装包")
    return sorted(files)


def main():
    files = source_files()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output_path = OUTPUT_DIR / f"BIT-SummerTermProject-linux-installer-{stamp}.tar.gz"
    commit = git_commit()

    install_template = ROOT / "scripts" / "release" / "install.sh"
    install_content = install_template.read_bytes().replace(b"\r\n", b"\n")

    file_count = 0
    with output_path.open("xb") as output_stream, tarfile.open(fileobj=output_stream, mode="w:gz") as tar:
        for path in files:
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
            "  - 团队测试地图 Key 已内置；个人覆盖配置、活动数据库和缓存不随包分发。\n"
            "  - 默认只编译三个程序，并行数 2；低内存虚拟机可传 --jobs 1。\n"
            "  - 完整回归编译可传 --preset ubuntu22-test；此包仍是需编译的源码包，不是免安装二进制包。\n"
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
