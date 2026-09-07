"""只读发行资源与用户可写运行状态之间的适配层。"""
from __future__ import annotations

import configparser
import contextlib
import json
import os
from pathlib import Path
import platform
import re
import secrets
import socket
import sys


HERE = Path(__file__).resolve().parent
for candidate in (HERE, HERE.parent):
    if (candidate / "demo_runtime.py").is_file():
        sys.path.insert(0, str(candidate))
        break

import demo_processes as processes
from demo_protocol import Connection, ProtocolError
from demo_runtime import (BINARIES, DemoError, Runtime, atomic_json,
                          ready_status, safe_path, sha256)


PORTABLE_BINARIES = {
    "server": "bin/ev_admin_server",
    "simulator": "bin/ev_charger_simulator",
    "client": "bin/ev_user_client",
}


class PortableRuntime(Runtime):
    """复用开发运行时的生命周期状态机，只替换发行资源定位。"""

    def __init__(self, bundle: Path, data_home: Path | None = None):
        super().__init__(bundle)
        self.bundle = self.root
        try:
            release = json.loads(safe_path(self.bundle / "release.json").read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise DemoError("RELEASE_INVALID", "发行清单缺失或格式无效") from exc
        if (not isinstance(release, dict)
                or type(release.get("schemaVersion")) is not int
                or release.get("schemaVersion") != 1
                or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", release.get("releaseId", ""))
                or not re.fullmatch(r"[0-9a-f]{40}", release.get("sourceCommit", ""))
                or release.get("platform") != "ubuntu22.04-x86_64"
                or not isinstance(release.get("binaries"), dict)
                or set(release["binaries"]) != set(PORTABLE_BINARIES)):
            raise DemoError("RELEASE_INVALID", "发行清单版本、身份、平台或程序列表无效")
        self.release = release
        base = Path(data_home) if data_home is not None else Path(
            os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")
        )
        self.data_root = safe_path(base / "evcharging" / release["releaseId"])
        self.runs = safe_path(self.data_root / "runs")
        self.config_path = safe_path(self.data_root / "config.local.ini")

    def fingerprint(self):
        return {
            "sourceCommit": self.release["sourceCommit"],
            "releaseId": self.release["releaseId"],
            "sourceDirty": False,
            "sourceDirtyScope": "发行清单与二进制指纹",
        }

    def configuration(self):
        if "EV_TENCENT_MAP_KEY" in os.environ:
            key = os.environ["EV_TENCENT_MAP_KEY"].strip()
        else:
            source = self.config_path if self.config_path.exists() else safe_path(
                self.bundle / "config.local.ini"
            )
            config = configparser.ConfigParser(interpolation=None)
            config.optionxform = str
            try:
                with source.open(encoding="utf-8") as stream:
                    config.read_file(stream)
            except (OSError, UnicodeError, configparser.Error) as exc:
                raise DemoError("CONFIG_INVALID", "本地INI配置缺失或格式无效") from exc
            key = config.get("tencent", "mapKey", fallback="").strip().strip('"').strip()
        if not key:
            raise DemoError("CONFIG_MISSING", "必须提供非空腾讯地图Key")
        token = getattr(self, "_configuration_token", None) or secrets.token_urlsafe(32)
        return key, token

    def reset(self, args):
        self.runs.mkdir(parents=True, exist_ok=True)
        return Runtime.reset(self, args)

    def load(self, run_id):
        path = self.run_path(run_id)
        try:
            manifest = json.loads(safe_path(path / "manifest.json").read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise DemoError("MANIFEST_INVALID", "运行清单缺失或格式无效") from exc
        if (not isinstance(manifest, dict)
                or type(manifest.get("version")) is not int
                or manifest.get("version") != 1
                or manifest.get("releaseId") != self.release["releaseId"]
                or manifest.get("runPath") != str(path)
                or manifest.get("runId") != run_id
                or manifest.get("state") not in (
                    "PREPARED", "STARTING", "RUNNING", "FAILED", "STOPPED")
                or not isinstance(manifest.get("sourceRoot"), str)
                or not isinstance(manifest.get("processes"), dict)
                or not set(manifest["processes"]).issubset(BINARIES)):
            raise DemoError("MANIFEST_INVALID", "运行清单版本或发行归属不匹配")
        return path, manifest

    def refuse_active_server(self):
        self.runs.mkdir(parents=True, exist_ok=True)
        for path in self.runs.iterdir():
            if path.name == ".lock" or not path.is_dir():
                continue
            safe_path(path)
            manifest_path = path / "manifest.json"
            if not manifest_path.exists():
                continue
            try:
                _, manifest = self.load(path.name)
            except DemoError as exc:
                try:
                    raw = json.loads(manifest_path.read_text(encoding="utf-8"))
                except (OSError, json.JSONDecodeError):
                    raw = None
                if isinstance(raw, dict) and raw.get("state") == "STOPPED":
                    continue
                raise DemoError("ACTIVE_SERVER", "存在无法确认身份的未停止发行记录") from exc
            if manifest["state"] == "STOPPED":
                continue
            active = {}
            for role, record in manifest["processes"].items():
                state = processes.inspect(record)
                if state != "EXITED":
                    active[role] = state
            if active:
                if manifest["sourceRoot"] != str(self.bundle):
                    raise DemoError(
                        "BUNDLE_MOVED_RUNNING", "发行包在进程运行期间被移动；请移回原位置后停止"
                    )
                raise DemoError("ACTIVE_SERVER", "本发行版已有活跃或身份不明的三端进程记录")

    @contextlib.contextmanager
    def _launch_environment(self, software_rendering):
        additions = {
            "LD_LIBRARY_PATH": str(self.bundle / "lib") + (
                ":" + os.environ["LD_LIBRARY_PATH"]
                if os.environ.get("LD_LIBRARY_PATH") else ""),
            "QT_PLUGIN_PATH": str(self.bundle / "plugins"),
            "QTWEBENGINEPROCESS_PATH": str(self.bundle / "libexec/QtWebEngineProcess"),
            "QTWEBENGINE_RESOURCES_PATH": str(self.bundle / "resources"),
            "QTWEBENGINE_LOCALES_PATH": str(self.bundle / "translations/qtwebengine_locales"),
        }
        if software_rendering:
            additions.update(QT_QUICK_BACKEND="software", LIBGL_ALWAYS_SOFTWARE="1")
        previous = {name: os.environ.get(name) for name in additions}
        os.environ.update(additions)
        try:
            yield
        finally:
            for name, value in previous.items():
                if value is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = value

    def start(self, args):
        if platform.machine().lower() not in ("x86_64", "amd64"):
            raise DemoError("PLATFORM_UNSUPPORTED", "本发行包仅支持x86_64 Linux")
        if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
            raise DemoError("DESKTOP_MISSING", "未检测到桌面显示会话，三端图形程序不会启动")
        self.refuse_active_server()
        self._configuration_token = secrets.token_urlsafe(32)
        try:
            self.configuration()
            self.build_info(self.bundle)
            with socket.socket() as probe:
                try:
                    probe.bind(("127.0.0.1", args.port))
                except OSError as exc:
                    raise DemoError("PORT_IN_USE", "本机端口已被占用，不启动任何进程") from exc
            args.build_dir = self.bundle
            args.headless = False
            self.reset(args)
            self.data_root.mkdir(parents=True, exist_ok=True)
            atomic_json(self.data_root / "current.json", {
                "schemaVersion": 1,
                "releaseId": self.release["releaseId"],
                "runId": args.run_id,
            })
            with self._launch_environment(getattr(args, "software_rendering", False)):
                return Runtime.start(self, args)
        finally:
            self._configuration_token = None

    def stop(self, args):
        _, manifest = self.load(args.run_id)
        if manifest["state"] != "STOPPED" and manifest["sourceRoot"] != str(self.bundle):
            states = {
                role: processes.inspect(record)
                for role, record in manifest["processes"].items()
            }
            if any(state != "EXITED" for state in states.values()):
                return {
                    "ok": False,
                    "code": "BUNDLE_MOVED_RUNNING",
                    "processes": states,
                    "message": "发行包在进程运行期间被移动；请移回原位置后再停止："
                               + manifest["sourceRoot"],
                }
        return Runtime.stop(self, args)

    def status(self, args):
        _, manifest = self.load(args.run_id)
        states = {
            role: processes.inspect(record)
            for role, record in manifest["processes"].items()
        }
        server = "未运行"
        if states.get("server") == "ALIVE":
            try:
                Connection(manifest["port"], args.timeout_seconds).health()
                server = "服务可用"
            except (ProtocolError, OSError, ValueError, KeyError, TypeError):
                server = "进程存活但服务不可用"
        simulator = "未运行"
        telemetry = "未运行"
        if states.get("simulator") == "ALIVE":
            try:
                ready_status(self.run_path(args.run_id) / "simulator-status.json",
                             manifest["processes"]["simulator"],
                             manifest["simulatorStartedAt"], manifest["intervalMs"])
                simulator = "已鉴权"
                telemetry = "需在模拟器界面确认；默认暂停，请点击Run开始上报"
            except DemoError:
                simulator = "进程存活但未确认鉴权"
                telemetry = "尚未确认"
        return {
            "ok": True,
            "code": "STATUS",
            "message": "运行状态已读取",
            "releaseId": self.release["releaseId"],
            "runId": args.run_id,
            "state": manifest["state"],
            "processes": states,
            "server": server,
            "simulator": simulator,
            "telemetry": telemetry,
            "client": "图形进程存活" if states.get("client") == "ALIVE" else "未运行",
            "runPath": manifest["runPath"],
            "logPath": manifest["runPath"],
            "installPathChanged": manifest["sourceRoot"] != str(self.bundle),
        }

    def build_info(self, directory):
        build = safe_path(Path(directory).resolve())
        if build != self.bundle:
            raise DemoError("BUILD_MISMATCH", "程序目录不属于当前发行包")
        files = {}
        for role, expected in PORTABLE_BINARIES.items():
            record = self.release["binaries"].get(role)
            if (not isinstance(record, dict) or record.get("path") != expected
                    or not re.fullmatch(r"[0-9a-f]{64}", record.get("sha256", ""))):
                raise DemoError("RELEASE_INVALID", "发行清单中的程序路径或指纹无效")
            binary = safe_path(self.bundle / expected)
            if not binary.is_file() or not os.access(binary, os.X_OK):
                raise DemoError("BINARY_MISSING", "发行包中的三个程序必须存在且可执行")
            actual = sha256(binary)
            if actual != record["sha256"]:
                raise DemoError("BINARY_MISMATCH", "发行程序完整性校验失败，拒绝启动")
            files[role] = {"path": str(binary), "sha256": actual}
        return build, files
