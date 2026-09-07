"""便携发行包的中文 start／stop／status 命令入口。"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import secrets
import sys

from portable_runtime import DemoError, PortableRuntime, safe_path


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise DemoError("ARGUMENT_ERROR", "参数缺失、未知或超出允许范围，请查看 --help")


def bounded(low, high):
    def convert(value):
        try:
            number = int(value)
        except ValueError as exc:
            raise argparse.ArgumentTypeError("必须是整数") from exc
        if not low <= number <= high:
            raise argparse.ArgumentTypeError("超出范围")
        return number
    return convert


def generated_run_id():
    stamp = datetime.now(timezone.utc).strftime("run-%Y%m%dT%H%M%SZ")
    return stamp + "-" + secrets.token_hex(4)


def current_run_id(runtime):
    try:
        value = json.loads(safe_path(runtime.data_root / "current.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DemoError("RUN_MISSING", "没有当前运行轮次，请先执行start") from exc
    if (not isinstance(value, dict) or value.get("schemaVersion") != 1
            or value.get("releaseId") != runtime.release["releaseId"]
            or not isinstance(value.get("runId"), str)):
        raise DemoError("MANIFEST_INVALID", "当前运行轮次指针无效")
    runtime.run_path(value["runId"])
    return value["runId"]


def parser_for_cli():
    parser = Parser(description="EVCharging Ubuntu便携发行运行管理")
    commands = parser.add_subparsers(dest="command", required=True)
    start = commands.add_parser("start", help="创建独立数据副本并启动三端")
    start.add_argument("--run-id", help="可选运行轮次；默认自动生成")
    start.add_argument("--port", type=bounded(1, 65535), default=9100)
    start.add_argument("--seed", type=bounded(0, 4294967295), default=20260901)
    start.add_argument("--interval-ms", type=bounded(1000, 10000), default=3000)
    start.add_argument("--timeout-seconds", type=bounded(1, 60), default=15)
    start.add_argument("--software-rendering", action="store_true",
                       help="仅对本轮三端启用虚拟机软件渲染")
    for name in ("stop", "status"):
        command = commands.add_parser(name, help="操作当前或指定运行轮次")
        command.add_argument("--run-id", help="默认读取当前运行轮次")
        command.add_argument("--timeout-seconds", type=bounded(1, 60),
                             default=10 if name == "stop" else 3)
        if name == "stop":
            command.add_argument("--force", action="store_true",
                                 help="TERM超时后再次核验身份再KILL")
    return parser


def main(argv=None, bundle=None, data_home=None):
    try:
        args = parser_for_cli().parse_args(argv)
        root = Path(bundle).resolve() if bundle is not None else Path(__file__).resolve().parent.parent
        runtime = PortableRuntime(root, data_home=data_home)
        if args.command == "start":
            args.run_id = args.run_id or generated_run_id()
        else:
            args.run_id = args.run_id or current_run_id(runtime)
        with runtime.lock():
            output = getattr(runtime, args.command)(args)
    except DemoError as exc:
        output = {"ok": False, "code": exc.code, "message": exc.message}
    except (OSError, ValueError, KeyError, TypeError):
        output = {
            "ok": False,
            "code": "RUNTIME_ERROR",
            "message": "运行操作失败，请检查发行包、用户数据、配置及该轮日志",
        }
    print("操作通过" if output["ok"] else "操作失败：" + output["message"])
    print(json.dumps(output, ensure_ascii=False))
    return 0 if output["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
