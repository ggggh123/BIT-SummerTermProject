#!/usr/bin/env python3
"""新环境一键部署：依赖安装 → 环境检查 → CMake 配置 → 全量编译 →（可选）拉起三端演示。

新用户拿到项目文件夹后，在 Ubuntu 虚拟机上只需：

    python3 scripts/quickstart.py          # 部署 + 编译
    python3 scripts/quickstart.py --start  # 部署 + 编译 + 自动拉起三端演示

设计说明：
- 入口用 Python 而非 shell：文件夹从 Windows 复制过来时 shell 脚本可能带 CRLF
  换行（bash 直接报错），Python 解释器不受影响，且脚本会顺手把所有 .sh 修好；
- 依赖安装只在环境检查发现缺失时触发（apt 系统包无法打包进项目文件夹，
  脚本化安装即是"开箱即用"的标准做法）；
- 全程幂等：重复运行安全，已完成的步骤自动跳过或无害重跑。
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# 核心验收所需的最小依赖集（与 scripts/check_env.sh 的核心检查项对齐）。
CORE_PACKAGES = [
    "build-essential", "cmake", "ninja-build", "pkg-config",
    "qt6-base-dev", "qt6-base-dev-tools", "qt6-tools-dev", "qt6-charts-dev",
    "qt6-webengine-dev", "libqt6webenginecore6-bin", "libqt6svg6",
    "python3-pytest",
]


def run(cmd, **kwargs):
    printable = " ".join(str(part) for part in cmd)
    print(f"  + {printable}", flush=True)
    return subprocess.run([str(part) for part in cmd], cwd=ROOT, **kwargs).returncode


def banner(step, total, text):
    print(f"\n[{step}/{total}] {text}", flush=True)


def normalize_shell_scripts():
    """把仓库内 shell 脚本的 CRLF 换行统一为 LF（Windows 复制导致）。"""
    fixed = 0
    for pattern in ("scripts/*.sh", "tests/scripts/*.sh"):
        for path in sorted(ROOT.glob(pattern)):
            data = path.read_bytes()
            normalized = data.replace(b"\r\n", b"\n")
            if normalized != data:
                path.write_bytes(normalized)
                fixed += 1
    print(f"  shell 脚本换行规范化：修正 {fixed} 个文件（CRLF → LF）")


def check_env():
    """运行 scripts/check_env.sh，返回 (是否通过, 输出文本)。"""
    result = subprocess.run(
        ["bash", "scripts/check_env.sh"], cwd=ROOT,
        capture_output=True, text=True,
    )
    return result.returncode == 0, (result.stdout + result.stderr)


def install_dependencies():
    print("  缺少依赖，执行 apt 安装（需要 sudo 密码，仅首次）……", flush=True)
    if run(["sudo", "apt-get", "update"]) != 0:
        return False
    if run(["sudo", "apt-get", "install", "-y", *CORE_PACKAGES]) != 0:
        return False
    return True


def configure_and_build(cpus):
    if run(["cmake", "--preset", "debug"]) != 0:
        return False
    if run(["cmake", "--build", "--preset", "debug", "-j", cpus]) != 0:
        return False
    return True


def start_demo(run_id):
    env = os.environ.copy()
    env.setdefault("EV_SIMULATOR_TOKEN", "demo-simulator-token")
    env.setdefault("QTWEBENGINE_DISABLE_SANDBOX", "1")
    env.setdefault("QTWEBENGINE_CHROMIUM_FLAGS", "--ignore-gpu-blocklist")
    cli = ["python3", "scripts/demo_cli.py"]
    for action in ("reset", "start"):
        cmd = cli + [action, "--run-id", run_id]
        if action == "start":
            cmd += ["--build-dir", "build/debug", "--port", "9100",
                    "--interval-ms", "3000", "--timeout-seconds", "20"]
        print(f"  + {' '.join(cmd)}", flush=True)
        result = subprocess.run(cmd, cwd=ROOT, env=env)
        if result.returncode != 0:
            return False
    return True


def main():
    parser = argparse.ArgumentParser(description="项目一键部署脚本（Ubuntu 虚拟机）")
    parser.add_argument("--start", action="store_true",
                        help="编译完成后自动拉起三端演示（服务端/管理端、模拟器、用户端）")
    parser.add_argument("--run-id", default=None,
                        help="演示运行轮次标识（默认 quickstart-时间戳，--start 时生效）")
    parser.add_argument("--skip-install", action="store_true",
                        help="跳过依赖安装（即使环境检查发现缺失）")
    args = parser.parse_args()

    if os.name == "nt":
        print("本脚本面向 Ubuntu 虚拟机；Windows 请使用 WSL 或参照 README 手动配置。")
        return 1

    total = 4
    print(f"BIT-SummerTermProject 一键部署（项目根：{ROOT}）", flush=True)

    banner(1, total, "规范化 shell 脚本换行（防 Windows CRLF 坑）")
    normalize_shell_scripts()

    banner(2, total, "环境检查（scripts/check_env.sh）")
    ok, output = check_env()
    if not ok:
        print(output.rstrip())
        if args.skip_install:
            print("  --skip-install 已指定，跳过安装；环境缺失将导致后续步骤失败。")
            return 1
        if run(["bash", "-c", "command -v apt-get >/dev/null 2>&1"]) != 0:
            print("  未检测到 apt-get，无法自动安装依赖；请手动安装后重试。")
            return 1
        if not install_dependencies():
            print("  依赖安装失败，请把上方输出反馈给团队。")
            return 1
        ok, output = check_env()
        if not ok:
            print(output.rstrip())
            print("  依赖安装后环境检查仍未通过，请把上方输出反馈给团队。")
            return 1
    print("  环境检查通过。")

    banner(3, total, "CMake 配置与全量编译（首次约数分钟）")
    cpus = str(os.cpu_count() or 2)
    if not configure_and_build(cpus):
        print("  配置或编译失败，请把上方输出反馈给团队。")
        return 1
    print("  编译完成。")

    banner(4, total, "就绪检查")
    binaries = [
        "apps/admin-server/ev_admin_server",
        "simulator/ev_charger_simulator",
        "apps/user-client/ev_user_client",
    ]
    missing = False
    for name in binaries:
        present = (ROOT / name).is_file()
        missing = missing or not present
        print(f"  {'OK ' if present else '缺失'} {name}")
    if missing:
        print("  预期二进制缺失，编译可能不完整，请把上方输出反馈给团队。")
        return 1

    if args.start:
        run_id = args.run_id or f"quickstart-{time.strftime('%m%d-%H%M%S')}"
        print(f"\n拉起三端演示（run-id: {run_id}）……")
        if not start_demo(run_id):
            print("  三端启动失败，请把上方输出反馈给团队。")
            return 1
        print("\n三端已就绪：应能看到管理端、用户端、模拟器三个窗口。")
        print(f"演示结束后关闭：python3 scripts/demo_cli.py stop --run-id {run_id}")
        return 0

    print("\n部署完成。启动三端演示：")
    print("  export QTWEBENGINE_DISABLE_SANDBOX=1")
    print('  export QTWEBENGINE_CHROMIUM_FLAGS="--ignore-gpu-blocklist"')
    print("  export EV_SIMULATOR_TOKEN=demo-simulator-token")
    print("  python3 scripts/demo_cli.py reset --run-id demo-1")
    print("  python3 scripts/demo_cli.py start --run-id demo-1 \\")
    print("    --build-dir build/debug --port 9100 --interval-ms 3000 --timeout-seconds 20")
    return 0


if __name__ == "__main__":
    sys.exit(main())
