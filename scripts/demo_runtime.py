"""独占运行副本、原子manifest以及四操作编排。"""
import contextlib
import configparser
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import socket
import time
from datetime import datetime, timezone
import demo_processes as processes
from demo_protocol import Connection, ProtocolError

BINARIES = {"server": "apps/admin-server/ev_admin_server", "simulator": "simulator/ev_charger_simulator",
            "client": "apps/user-client/ev_user_client"}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ready_status(path, record, started_at, interval_ms):
    try:
        data = json.loads(safe_path(path).read_text())
        if (not isinstance(data, dict) or type(data.get("schemaVersion")) is not int
                or type(data.get("pid")) is not int or not isinstance(data.get("updatedAt"), str)):
            raise ValueError()
        updated = datetime.fromisoformat(data["updatedAt"].replace("Z", "+00:00"))
        if updated.tzinfo is None:
            raise ValueError()
        stamp = updated.timestamp()
        valid = (data.get("schemaVersion") == 1 and data.get("pid") == record["pid"]
                 and started_at <= stamp <= time.time() and time.time() - stamp <= max(5, 3 * interval_ms / 1000))
        if valid and data.get("sessionState") == "auth_failed":
            raise DemoError("SIMULATOR_AUTH_FAILED", "模拟器认证失败")
        if valid and data.get("sessionState") == "ready" and processes.inspect(record) == "ALIVE":
            return data
    except (OSError, ValueError, KeyError, TypeError):
        pass
    raise DemoError("SIMULATOR_NOT_READY", "模拟器状态缺失、过期、身份不符或尚未ready")


class DemoError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code, self.message = code, message


def safe_path(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        if item.is_symlink():
            raise DemoError("UNSAFE_PATH", "拒绝符号链接路径")
    return path


def atomic_json(path, data):
    safe_path(path)
    temporary = path.with_name(path.name + ".tmp")
    safe_path(temporary)
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(data, stream, ensure_ascii=False, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


class Runtime:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.runs = safe_path(self.root / "runtime/demo-runs")

    @contextlib.contextmanager
    def lock(self):
        self.runs.mkdir(parents=True, exist_ok=True)
        path = safe_path(self.runs / ".lock")
        with path.open("a") as stream:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
            yield

    def run_path(self, run_id):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", run_id, flags=re.ASCII):
            raise DemoError("INVALID_RUN_ID", "运行标识不合法")
        return safe_path(self.runs / run_id)

    def reset(self, args):
        path = self.run_path(args.run_id)
        if path.exists():
            raise DemoError("RUN_EXISTS", "运行轮次已存在，请选择新标识")
        self.refuse_active_server()
        golden = safe_path(self.root / "runtime/golden/core.db")
        checksum = safe_path(self.root / "runtime/golden/core.db.sha256")
        sys.path.insert(0, str(self.root / "database"))
        from create_runtime_copy import create_runtime_copy
        digest = create_runtime_copy(golden, checksum, path / "core-runtime.db")
        manifest = {"version": 1, "runId": args.run_id, "sourceRoot": str(self.root),
                    "runPath": str(path), "state": "PREPARED", "processes": {}, "goldenSha256": digest}
        manifest.update(self.fingerprint())
        atomic_json(path / "manifest.json", manifest)
        return {"ok": True, "code": "PREPARED", "runPath": str(path)}

    def load(self, run_id):
        path = self.run_path(run_id)
        manifest = json.loads(safe_path(path / "manifest.json").read_text())
        if (not isinstance(manifest, dict) or type(manifest.get("version")) is not int
                or manifest["version"] != 1 or manifest.get("sourceRoot") != str(self.root)
                or manifest.get("runPath") != str(path) or manifest.get("runId") != run_id
                or manifest.get("state") not in ("PREPARED", "STARTING", "RUNNING", "FAILED", "STOPPED")
                or not isinstance(manifest.get("processes"), dict)
                or not set(manifest["processes"]).issubset(BINARIES)):
            raise DemoError("MANIFEST_INVALID", "运行清单版本或归属不匹配")
        return path, manifest

    def refuse_active_server(self):
        for path in self.runs.iterdir():
            if path.name == ".lock":
                continue
            safe_path(path)
            if not path.is_dir():
                continue
            if not (path / "manifest.json").exists():
                continue
            _, manifest = self.load(path.name)
            if "server" in manifest["processes"] and processes.inspect(manifest["processes"]["server"]) != "EXITED":
                raise DemoError("ACTIVE_SERVER", "本仓库已有活跃或身份不明的服务端记录")

    def stop(self, args):
        path, manifest = self.load(args.run_id)
        outcomes = processes.stop_all(manifest["processes"], args.timeout_seconds, args.force)
        ok = all(value == "EXITED" for value in outcomes.values())
        manifest["stopResults"] = outcomes
        manifest["state"] = "STOPPED" if ok else "FAILED"
        atomic_json(path / "manifest.json", manifest)
        return {"ok": ok, "code": "STOPPED" if ok else "STOP_FAILED", "processes": outcomes,
                "message": "进程已退出" if ok else "部分进程未退出或身份不匹配，保留记录"}

    def configuration(self):
        token = os.environ.get("EV_SIMULATOR_TOKEN", "").strip()
        if "EV_TENCENT_MAP_KEY" in os.environ:
            key = os.environ["EV_TENCENT_MAP_KEY"].strip()
        else:
            config = configparser.ConfigParser(interpolation=None)
            config.optionxform = str
            try:
                config.read(self.root / "config.local.ini", encoding="utf-8")
            except configparser.Error as exc:
                raise DemoError("CONFIG_INVALID", "本地INI配置格式无效") from exc
            key = config.get("tencent", "mapKey", fallback="").strip().strip('"')
        if not key or not token:
            raise DemoError("CONFIG_MISSING", "必须提供非空腾讯地图Key及EV_SIMULATOR_TOKEN")
        return key, token

    def build_info(self, directory):
        build = safe_path(directory.resolve())
        cache = safe_path(build / "CMakeCache.txt").read_text()
        source = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$", cache, re.MULTILINE)
        if not source or Path(source[1]).resolve() != self.root:
            raise DemoError("BUILD_MISMATCH", "CMake构建目录不属于当前源码根")
        files = {}
        for role, name in BINARIES.items():
            binary = safe_path(build / name)
            if not binary.is_file() or not os.access(binary, os.X_OK):
                raise DemoError("BINARY_MISSING", "三个预期二进制必须已经构建且可执行")
            files[role] = {"path": str(binary), "sha256": sha256(binary)}
        return build, files

    def fingerprint(self):
        command = ["git", "-c", "safe.directory=" + str(self.root), "-C", str(self.root)]
        commit = subprocess.run(command + ["rev-parse", "HEAD"], capture_output=True, text=True)
        dirty = subprocess.run(command + ["status", "--porcelain", "--", ".", ":(exclude,glob)*.xlsx", ":(exclude,glob)**/*.xlsx"], capture_output=True, text=True)
        return {"sourceCommit": commit.stdout.strip() if commit.returncode == 0 else None,
                "sourceDirty": bool(dirty.stdout) if dirty.returncode == 0 else None,
                "sourceDirtyScope": "代码及非矩阵文件；排除所有xlsx；二进制指纹与源码SHA分别记录，不证明对应构建"}

    def assert_alive(self, manifest):
        if set(manifest["processes"]) != set(BINARIES):
            raise DemoError("PROCESS_NOT_ALIVE", "清单未包含全部三端进程")
        if any(processes.inspect(record) != "ALIVE" for record in manifest["processes"].values()):
            raise DemoError("PROCESS_NOT_ALIVE", "至少一个进程已退出或身份不匹配")

    def smoke(self, args):
        path = self.run_path(args.run_id)
        if not path.is_dir():
            raise DemoError("RUN_MISSING", "运行轮次不存在")
        connection = None
        report = {"ok": False, "code": "SMOKE_FAILED", "runPath": str(path),
                  "checkedAt": datetime.now(timezone.utc).isoformat(),
                  "tencentNavigation": "NOT_RUN_SEPARATE_GATE",
                  "fullBusinessRehearsal": "NOT_RUN_SEPARATE_GATE", "otherMachine": "NOT_RUN_SEPARATE_GATE"}
        try:
            _, manifest = self.load(args.run_id)
            if manifest["state"] != "RUNNING":
                raise DemoError("RUN_NOT_RUNNING", "仅对RUNNING轮次进行基础冒烟")
            self.assert_alive(manifest)
            connection = Connection(manifest["port"], args.timeout_seconds)
            connection.health()
            ready_status(path / "simulator-status.json", manifest["processes"]["simulator"],
                         manifest["simulatorStartedAt"], manifest["intervalMs"])
            connection.business_smoke()
            ready_status(path / "simulator-status.json", manifest["processes"]["simulator"],
                         manifest["simulatorStartedAt"], manifest["intervalMs"])
            self.assert_alive(manifest)
            report.update(ok=True, code="BASIC_SMOKE_PASS", client="CLIENT_PROCESS_ALIVE")
        except DemoError as exc:
            report.update(code=exc.code, message=exc.message)
        except (ProtocolError, OSError, ValueError, KeyError, TypeError):
            report.update(code="SMOKE_FAILED", message="协议、业务形态或运行状态校验失败")
        report["requests"] = connection.events if connection else []
        atomic_json(path / "smoke-report.json", report)
        if report["ok"]:
            try:
                self.assert_alive(manifest)
            except DemoError as exc:
                report.update(ok=False, code=exc.code, message=exc.message)
                atomic_json(path / "smoke-report.json", report)
        return report

    def start(self, args):
        path, manifest = self.load(args.run_id)
        if manifest["state"] != "PREPARED" or manifest["processes"]:
            raise DemoError("RUN_NOT_PREPARED", "只能启动无进程记录的PREPARED轮次")
        self.refuse_active_server()
        key, token = self.configuration()
        build, binaries = self.build_info(args.build_dir)
        if not processes.supported():
            raise DemoError("PIDFD_UNSUPPORTED", "当前系统不支持pidfd，不启动进程")
        for name in ("core-runtime.db", "snapshot.json", "simulator-status.json", "server.log", "simulator.log", "client.log"):
            safe_path(path / name)
            if name != "core-runtime.db" and (path / name).exists():
                raise DemoError("ARTIFACT_EXISTS", "预备轮次已含启动产物，请使用新轮次")
        if not (path / "core-runtime.db").is_file() or sha256(path / "core-runtime.db") != manifest["goldenSha256"]:
            raise DemoError("RUNTIME_DB_CHANGED", "预备运行副本已被修改或缺失")
        with socket.socket() as probe:
            try:
                probe.bind(("127.0.0.1", args.port))
            except OSError as exc:
                raise DemoError("PORT_IN_USE", "本机端口已被占用") from exc
        manifest.update(self.fingerprint())
        manifest.update(state="STARTING", buildPath=str(build), binaries=binaries, port=args.port,
                        seed=args.seed, intervalMs=args.interval_ms, headless=args.headless)
        atomic_json(path / "manifest.json", manifest)
        environment = os.environ.copy()
        environment.update(EV_SERVER_HOST="127.0.0.1", EV_SERVER_PORT=str(args.port),
                           EV_TENCENT_MAP_KEY=key, EV_SIMULATOR_TOKEN=token,
                           EV_SIMULATOR_STATUS_FILE=str(path / "simulator-status.json"))
        children = []

        def launch(role, arguments):
            log = safe_path(path / (role + ".log"))
            # 独占日志，绝不覆盖已有运行文件。
            with log.open("xb") as stream:
                child = subprocess.Popen([binaries[role]["path"], *arguments], cwd=self.root,
                                         env=environment, stdin=subprocess.DEVNULL, stdout=stream,
                                         stderr=subprocess.STDOUT, start_new_session=True)
            children.append(child)
            manifest["processes"][role] = {"pid": child.pid, "starttime": None, "exe": None, "bootId": None}
            try:
                manifest["processes"][role] = processes.capture(child.pid)
            except (FileNotFoundError, ProcessLookupError):
                raise DemoError(role.upper() + "_FAILED", "进程在身份登记前退出")
            except OSError as exc:
                atomic_json(path / "manifest.json", manifest)
                raise DemoError("IDENTITY_UNAVAILABLE", "无法确认新进程身份；保留PID记录，不发送信号") from exc
            atomic_json(path / "manifest.json", manifest)
            return child

        try:
            server_args = ["--db", str(path / "core-runtime.db"), "--host", "127.0.0.1", "--port", str(args.port),
                           "--snapshot", str(path / "snapshot.json"), "--golden", str(self.root / "runtime/golden/core.db"),
                           "--golden-hash", manifest["goldenSha256"]]
            if args.headless:
                server_args.append("--server")
            launch("server", server_args)
            deadline = time.monotonic() + args.timeout_seconds
            expected = (f" listening on 127.0.0.1:{args.port}, db={path / 'core-runtime.db'}, "
                        f"snapshot={path / 'snapshot.json'}")
            while True:
                if processes.inspect(manifest["processes"]["server"]) != "ALIVE":
                    raise DemoError("SERVER_FAILED", "服务端启动后退出或身份变化")
                if time.monotonic() >= deadline:
                    raise DemoError("SERVER_TIMEOUT", "服务端初始化或健康检查超时")
                if expected in (path / "server.log").read_text(errors="replace"):
                    try:
                        Connection(args.port, max(.001, deadline - time.monotonic())).health()
                        break
                    except ProtocolError:
                        pass
                time.sleep(.05)
            manifest["simulatorStartedAt"] = time.time()
            launch("simulator", ["--host", "127.0.0.1", "--port", str(args.port), "--seed", str(args.seed),
                                 "--interval-ms", str(args.interval_ms)])
            deadline = time.monotonic() + args.timeout_seconds
            while True:
                if processes.inspect(manifest["processes"]["simulator"]) != "ALIVE":
                    raise DemoError("SIMULATOR_FAILED", "模拟器已退出或身份变化")
                try:
                    ready_status(path / "simulator-status.json", manifest["processes"]["simulator"],
                                 manifest["simulatorStartedAt"], args.interval_ms)
                    break
                except DemoError as exc:
                    if exc.code == "SIMULATOR_AUTH_FAILED" or time.monotonic() >= deadline:
                        raise
                time.sleep(.05)
            child = launch("client", [])
            deadline = time.monotonic() + .5
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise DemoError("CLIENT_FAILED", "用户端在500ms观察期内退出")
                time.sleep(.025)
            ready_status(path / "simulator-status.json", manifest["processes"]["simulator"],
                         manifest["simulatorStartedAt"], args.interval_ms)
            self.assert_alive(manifest)
            manifest["state"] = "RUNNING"
            atomic_json(path / "manifest.json", manifest)
            self.assert_alive(manifest)
            return {"ok": True, "code": "RUNNING", "runPath": str(path), "client": "CLIENT_PROCESS_ALIVE"}
        except (DemoError, OSError, ValueError, KeyError, TypeError) as exc:
            manifest["rollbackResults"] = processes.stop_all(manifest["processes"], args.timeout_seconds)
            manifest["state"] = "FAILED"
            atomic_json(path / "manifest.json", manifest)
            if isinstance(exc, DemoError):
                raise
            raise DemoError("START_FAILED", "启动失败，已尝试反序TERM并保留日志和进程记录") from exc
        finally:
            for child in children:
                child.poll()
