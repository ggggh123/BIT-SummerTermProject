"""四个演示入口的参数与不含敏感值的中文结果。"""
import argparse
import json
from pathlib import Path
import sys

from demo_runtime import DemoError, Runtime


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise DemoError("ARGUMENT_ERROR", "参数缺失、未知或超出允许范围，请查看 --help")


def bounded(low, high):
    def convert(value):
        number = int(value)
        if not low <= number <= high:
            raise argparse.ArgumentTypeError("超出范围")
        return number
    return convert


def main():
    try:
        parser = Parser(description="核心三端运行副本管理（不代表发布验收）")
        commands = parser.add_subparsers(dest="command", required=True)
        for name in ("reset", "start", "stop", "smoke"):
            sub = commands.add_parser(name)
            sub.add_argument("--run-id", required=True, help="新的运行轮次标识；1–64位ASCII字母数字/点/下划线/横线")
            if name in ("start", "smoke", "stop"):
                sub.add_argument("--timeout-seconds", type=bounded(1, 60), default=10 if name == "stop" else 15)
            if name == "start":
                sub.add_argument("--build-dir", required=True, type=Path, help="对应当前源码的原生CMake构建目录")
                sub.add_argument("--port", type=bounded(1, 65535), default=9100)
                sub.add_argument("--seed", type=bounded(0, 4294967295), default=20260901)
                sub.add_argument("--interval-ms", type=bounded(1000, 10000), default=3000)
                sub.add_argument("--headless", action="store_true", help="仅服务端使用无GUI模式")
            if name == "stop":
                sub.add_argument("--force", action="store_true", help="TERM超时后，经再次身份校验才KILL")
        args = parser.parse_args()
        runtime = Runtime(Path(__file__).resolve().parents[1])
        with runtime.lock():
            output = getattr(runtime, args.command)(args)
    except DemoError as exc:
        output = {"ok": False, "code": exc.code, "message": exc.message}
    except (OSError, ValueError, KeyError, TypeError) as exc:
        # 不回显底层异常，避免配置值/会话payload进入CLI输出。
        output = {"ok": False, "code": "RUNTIME_ERROR", "message": "运行操作失败，请检查路径、配置与该轮日志"}
    print("操作通过" if output["ok"] else "操作失败：" + output["message"])
    print(json.dumps(output, ensure_ascii=False))
    return 0 if output["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
