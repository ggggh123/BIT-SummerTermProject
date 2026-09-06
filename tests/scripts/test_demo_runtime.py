"""运行入口外部契约；仅修改 tmp_path 精确复制的测试仓库。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import signal
import time
from types import SimpleNamespace
import socket
import struct
import threading
import contextlib

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))


@pytest.fixture
def repo(tmp_path):
    root = tmp_path / "repo with spaces"
    for name in ("scripts", "database"):
        shutil.copytree(ROOT / name, root / name, ignore=shutil.ignore_patterns("__pycache__", "tests", "*.xlsx"))
    (root / "runtime/golden").mkdir(parents=True)
    for name in ("core.db", "core.db.sha256"):
        shutil.copyfile(ROOT / "runtime/golden" / name, root / "runtime/golden" / name)
    return root


def cli(repo, command, *args, env=None, cwd=None):
    return subprocess.run(["bash", str(repo / "scripts" / (command + "_demo.sh" if command != "smoke" else "smoke_test.sh")), *args],
                          cwd=cwd or repo.parent, env=env, text=True, capture_output=True, timeout=30)


def result(proc):
    return json.loads(proc.stdout.splitlines()[-1])


def test_reset_new_copy_and_duplicate_preserves_db(repo):
    first = cli(repo, "reset", "--run-id", "round-01")
    assert first.returncode == 0, first.stderr
    db = repo / "runtime/demo-runs/round-01/core-runtime.db"
    digest = hashlib.sha256(db.read_bytes()).hexdigest()
    assert digest == (repo / "runtime/golden/core.db.sha256").read_text().strip()
    assert result(first)["ok"] is True
    again = cli(repo, "reset", "--run-id", "round-01")
    assert again.returncode != 0
    assert hashlib.sha256(db.read_bytes()).hexdigest() == digest


@pytest.mark.parametrize("command", ["reset", "start", "stop", "smoke"])
def test_help_and_unknown_parameter(repo, command):
    assert cli(repo, command, "--help").returncode == 0
    bad = cli(repo, command, "--not-an-option")
    assert bad.returncode != 0
    assert result(bad)["ok"] is False


@pytest.mark.parametrize("run_id", ["..", ".", "../escape", "/tmp/escape", "é", "a" * 65])
def test_reject_run_path_escape(repo, run_id):
    assert cli(repo, "reset", "--run-id", run_id).returncode != 0
    assert not (repo / "runtime/demo-runs" / "escape").exists()


@pytest.mark.parametrize("damage", ["hash", "sidecar", "symlink", "existing_db"])
def test_reset_rejects_damaged_or_existing_targets(repo, damage):
    target = repo / "runtime/demo-runs/round-01"
    if damage == "hash":
        (repo / "runtime/golden/core.db.sha256").write_text("0" * 64)
    elif damage == "sidecar":
        (repo / "runtime/golden/core.db-wal").touch()
    elif damage == "symlink":
        (repo / "runtime/demo-runs").symlink_to(repo.parent, target_is_directory=True)
    else:
        target.mkdir(parents=True)
        (target / "core-runtime.db").write_bytes(b"keep")
    bad = cli(repo, "reset", "--run-id", "round-01")
    assert bad.returncode != 0
    if damage == "existing_db":
        assert (target / "core-runtime.db").read_bytes() == b"keep"


def test_shell_symlink_arbitrary_cwd(repo, tmp_path):
    link = tmp_path / "入口 link.sh"
    link.symlink_to(repo / "scripts/reset_demo.sh")
    proc = subprocess.run(["bash", str(link), "--run-id", "linked"], cwd="/tmp", capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    assert (repo / "runtime/demo-runs/linked/manifest.json").exists()


@pytest.fixture
def sleeper():
    proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    time.sleep(.05)
    yield proc
    if proc.poll() is None:
        proc.terminate()
    proc.wait(timeout=3)


@pytest.mark.parametrize("field,value", [("pid", 99999999), ("starttime", "0"), ("bootId", "bad"), ("exe", "/wrong")])
def test_identity_mismatch_never_signals_bystander(sleeper, field, value):
    import demo_processes as processes
    record = processes.capture(sleeper.pid)
    record[field] = value
    outcome = processes.stop_one(record, .1, True)
    assert outcome in ("WRONG_PID", "EXITED")
    assert sleeper.poll() is None


def test_stop_timeout_no_kill_and_force_checked():
    import demo_processes as processes
    proc = subprocess.Popen([sys.executable, "-c", "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); print('ready',flush=True); time.sleep(60)"], stdout=subprocess.PIPE, text=True)
    assert proc.stdout.readline().strip() == "ready"
    record = processes.capture(proc.pid)
    try:
        assert processes.stop_one(record, .1, False) == "STOP_TIMEOUT"
        assert proc.poll() is None
        assert processes.stop_one(record, .1, True) == "EXITED"
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()


def test_reset_refuses_active_or_unknown_server_and_stop_continues(repo, sleeper):
    import demo_processes as processes
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    path = repo / "runtime/demo-runs/a/manifest.json"
    manifest = json.loads(path.read_text())
    record = processes.capture(sleeper.pid)
    record["starttime"] = "0"
    manifest["processes"] = {"server": record}
    path.write_text(json.dumps(manifest))
    assert result(cli(repo, "reset", "--run-id", "b"))["code"] == "ACTIVE_SERVER"
    assert result(cli(repo, "stop", "--run-id", "a", "--force"))["code"] == "STOP_FAILED"
    assert sleeper.poll() is None
    assert json.loads(path.read_text())["state"] != "STOPPED"


def test_prepared_stop_is_idempotent_but_not_reusable(repo):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    assert result(cli(repo, "stop", "--run-id", "a"))["code"] == "STOPPED"
    assert cli(repo, "stop", "--run-id", "a").returncode == 0
    assert cli(repo, "reset", "--run-id", "a").returncode != 0


def test_commands_hold_same_repository_flock(repo):
    import demo_runtime
    runtime = demo_runtime.Runtime(repo)
    with runtime.lock():
        proc = subprocess.Popen(["bash", str(repo / "scripts/reset_demo.sh"), "--run-id", "locked"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(.2)
        assert proc.poll() is None
        assert not (repo / "runtime/demo-runs/locked").exists()
    assert proc.wait(timeout=5) == 0


@contextlib.contextmanager
def responder(reply):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        port = listener.getsockname()[1]
        def serve():
            with listener.accept()[0] as conn:
                size = struct.unpack(">I", conn.recv(4))[0]
                request = json.loads(conn.recv(size))
                packet = reply(request)
                try:
                    conn.sendall(packet)
                except OSError:
                    pass
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        yield port
        thread.join(timeout=2)


def packet(data):
    body = json.dumps(data).encode()
    return struct.pack(">I", len(body)) + body


def envelope(request, **changes):
    value = {"requestId": request["requestId"], "ok": True, "code": "OK", "message": "ready", "data": {"status": "degraded", "schemaVersion": 1, "snapshotVersion": 1, "forecastRunId": None, "serverTime": "2026-09-06T00:00:00+08:00"}}
    value.update(changes)
    return value


@pytest.mark.parametrize("damage", ["id", "ok_type", "data", "code", "oversize", "zero", "broken_json"])
def test_protocol_rejects_wrong_id_and_bad_envelope(damage):
    import demo_protocol
    def reply(request):
        if damage == "oversize":
            return struct.pack(">I", 1048577)
        if damage == "zero":
            return struct.pack(">I", 0)
        if damage == "broken_json":
            return b"\x00\x00\x00\x01{"
        changes = {"id": {"requestId": "wrong"}, "ok_type": {"ok": 1}, "data": {"data": []}, "code": {"code": "ERROR"}}
        return packet(envelope(request, **changes[damage]))
    with responder(reply) as port, pytest.raises(demo_protocol.ProtocolError):
        demo_protocol.Connection(port, 1).health()


def test_protocol_accepts_degraded_without_forecast_and_redacts_summary():
    import demo_protocol
    with responder(lambda request: packet(envelope(request))) as port:
        client = demo_protocol.Connection(port, 1)
        assert client.health()["status"] == "degraded"
        assert set(client.events[0]) == {"action", "code", "requestId"}


@pytest.mark.parametrize("code", ["auth_required", "BAD-CODE", "A" * 65])
def test_protocol_rejected_response_does_not_record_unsupported_code(code):
    import demo_protocol
    with responder(lambda request: packet(envelope(
            request, ok=False, code=code, message="PRIVATE_MESSAGE_TEST",
            data={"token": "PRIVATE_SESSION_TEST"}))) as port:
        client = demo_protocol.Connection(port, 1)
        with pytest.raises(demo_protocol.ProtocolError):
            client.health()
        assert client.events[0]["code"] == "PROTOCOL_ERROR"
        assert set(client.events[0]) == {"action", "code", "requestId"}
        assert "PRIVATE_" not in json.dumps(client.events)


@pytest.mark.parametrize("status", ["starting", "waiting", "error", None])
def test_health_rejects_nonready_state(status):
    import demo_protocol
    with responder(lambda request: packet(envelope(request, data={"status": status}))) as port:
        with pytest.raises(demo_protocol.ProtocolError):
            demo_protocol.Connection(port, 1).health()


@pytest.fixture
def build(repo):
    """独立可执行替身只用于阶段/失败控制；真实Qt组合另有live测试。"""
    directory = repo.parent / "native build"
    directory.mkdir()
    (directory / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={repo}\n")
    program = '''#!/usr/bin/python3
import os,sys,time,json,socket,struct,threading,signal
from datetime import datetime,timezone
role=os.path.basename(sys.argv[0])
mode=os.environ.get("FIXTURE_MODE", "")
def arg(name): return sys.argv[sys.argv.index(name)+1]
if role == "ev_admin_server":
    if mode == "server_fail": sys.exit(2)
    port=int(arg("--port"))
    s=socket.socket(); s.bind(("127.0.0.1",port)); s.listen()
    if mode != "no_startup":
        print("ev_admin_server mode=headless listening on 127.0.0.1:"+str(port)+", db="+arg("--db")+", snapshot="+arg("--snapshot"),flush=True)
    while True:
        c,_=s.accept()
        with c:
            if mode == "health_timeout": time.sleep(5); continue
            header=c.recv(4)
            if not header: continue
            n=struct.unpack(">I",header)[0]; req=json.loads(c.recv(n))
            data={"status":"degraded","schemaVersion":1,"snapshotVersion":0,"forecastRunId":None,"serverTime":"2026-09-06T00:00:00+08:00"}
            packet=json.dumps({"requestId":req["requestId"],"ok":True,"code":"OK","message":"ready","data":data}).encode()
            try: c.sendall(struct.pack(">I",len(packet))+packet)
            except OSError: pass
elif role == "ev_charger_simulator":
    if mode == "sim_reject_ignores_term": signal.signal(signal.SIGTERM,signal.SIG_IGN)
    while True:
        status={"schemaVersion":1,"pid":os.getpid(),"sessionState":"auth_failed" if mode.startswith("sim_reject") else "ready","updatedAt":datetime.now(timezone.utc).isoformat()}
        path=os.environ["EV_SIMULATOR_STATUS_FILE"]
        with open(path+".tmp","w") as f: json.dump(status,f)
        os.replace(path+".tmp",path)
        time.sleep(.2)
else:
    if mode == "client_exit": sys.exit(3)
    time.sleep(60)
'''
    for relative in ("apps/admin-server/ev_admin_server", "simulator/ev_charger_simulator", "apps/user-client/ev_user_client"):
        path = directory / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(program)
        path.chmod(0o755)
    return directory


def fake_env(**changes):
    env = os.environ.copy()
    env.update(EV_TENCENT_MAP_KEY="invalid-offline-placeholder", EV_SIMULATOR_TOKEN="demo-simulator-token", QT_QPA_PLATFORM="offscreen")
    env.update(changes)
    return env


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def start(repo, build, *extra, env=None, port=None):
    return cli(repo, "start", "--run-id", "a", "--build-dir", str(build), "--headless", "--port", str(port or free_port()), "--timeout-seconds", "1", *extra, env=env or fake_env())


@pytest.mark.parametrize("key", ["EV_TENCENT_MAP_KEY", "EV_SIMULATOR_TOKEN"])
@pytest.mark.parametrize("value", [None, "", "   "])
def test_start_missing_or_blank_credentials_before_processes(repo, build, key, value):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    env = fake_env()
    if value is None:
        env.pop(key)
    else:
        env[key] = value
    proc = start(repo, build, env=env)
    assert proc.returncode != 0
    manifest = json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())
    assert manifest["processes"] == {}
    assert result(proc)["code"] == "CONFIG_MISSING"


@pytest.mark.parametrize("ini_value", ['""', '"   "'])
def test_start_blank_quoted_ini_key_fails_before_processes(repo, build, ini_value):
    (repo / "config.local.ini").write_text(f"[tencent]\nmapKey={ini_value}\n")
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    env = fake_env()
    env.pop("EV_TENCENT_MAP_KEY")
    proc = start(repo, build, env=env)
    assert proc.returncode != 0
    assert result(proc)["code"] == "CONFIG_MISSING"
    path = repo / "runtime/demo-runs/a"
    manifest = json.loads((path / "manifest.json").read_text())
    assert manifest["state"] == "PREPARED"
    assert manifest["processes"] == {}
    assert not list(path.glob("*.log"))


@pytest.mark.parametrize("option,value", [("--port", "0"), ("--port", "65536"), ("--seed", "-1"), ("--seed", "4294967296"), ("--interval-ms", "999"), ("--interval-ms", "10001"), ("--timeout-seconds", "0")])
def test_start_invalid_numbers_before_processes(repo, build, option, value):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    proc = start(repo, build, option, value)
    assert result(proc)["code"] == "ARGUMENT_ERROR"
    assert json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())["processes"] == {}


@pytest.mark.parametrize("mode,code", [("server_fail", "SERVER_FAILED"), ("no_startup", "SERVER_TIMEOUT"), ("health_timeout", "SERVER_TIMEOUT"), ("sim_reject", "SIMULATOR_AUTH_FAILED"), ("client_exit", "CLIENT_FAILED")])
def test_start_stage_failures_rollback_preserve_records(repo, build, mode, code):
    import demo_processes
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    proc = start(repo, build, env=fake_env(FIXTURE_MODE=mode))
    assert proc.returncode != 0
    assert result(proc)["code"] == code
    path = repo / "runtime/demo-runs/a"
    manifest = json.loads((path / "manifest.json").read_text())
    assert manifest["state"] == "FAILED"
    assert (path / "core-runtime.db").exists()
    for record in manifest["processes"].values():
        assert demo_processes.inspect(record) == "EXITED"


def test_start_success_duplicate_and_stop(repo, build):
    import demo_processes
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    try:
        proc = start(repo, build)
        assert proc.returncode == 0, proc.stdout + proc.stderr
        assert result(proc)["client"] == "CLIENT_PROCESS_ALIVE"
        manifest = json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())
        assert set(manifest["processes"]) == {"server", "simulator", "client"}
        assert all(demo_processes.inspect(record) == "ALIVE" for record in manifest["processes"].values())
        assert result(start(repo, build))["code"] == "RUN_NOT_PREPARED"
        assert result(cli(repo, "reset", "--run-id", "b"))["code"] == "ACTIVE_SERVER"
    finally:
        assert cli(repo, "stop", "--run-id", "a", "--timeout-seconds", "1").returncode == 0


def test_start_refuses_unrelated_port_listener(repo, build):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        assert result(start(repo, build, port=listener.getsockname()[1]))["code"] == "PORT_IN_USE"
    assert json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())["processes"] == {}


@pytest.mark.parametrize("damage", ["cache", "binary", "symlink"])
def test_start_checks_native_build_and_targets(repo, build, damage):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    if damage == "cache":
        (build / "CMakeCache.txt").write_text("CMAKE_HOME_DIRECTORY:INTERNAL=/wrong\n")
    elif damage == "binary":
        (build / "apps/user-client/ev_user_client").chmod(0o644)
    else:
        (repo / "runtime/demo-runs/a/snapshot.json").symlink_to(repo.parent / "outside")
    proc = start(repo, build)
    assert proc.returncode != 0
    assert json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())["processes"] == {}


@pytest.mark.parametrize("damage", ["old", "before_start", "wrong_pid", "waiting_auth", "disconnected", "auth_failed", "future"])
def test_ready_rejects_stale_wrong_process_and_nonready(tmp_path, sleeper, damage):
    import demo_processes
    from demo_runtime import ready_status, DemoError
    from datetime import datetime, timezone
    stamp = time.time()
    started = stamp - 1
    pid = sleeper.pid
    state = "ready"
    if damage == "old":
        started = stamp - 100
        stamp -= 40
    elif damage == "before_start":
        started = stamp + 1
    elif damage == "future":
        stamp += 100
    elif damage == "wrong_pid":
        pid += 1
    else:
        state = damage
    path = tmp_path / "status.json"
    path.write_text(json.dumps({"schemaVersion": 1, "pid": pid, "sessionState": state,
                                "updatedAt": datetime.fromtimestamp(stamp, timezone.utc).isoformat()}))
    with pytest.raises(DemoError):
        ready_status(path, demo_processes.capture(sleeper.pid), started, 1000)


@contextlib.contextmanager
def business_server(damage=""):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(.1)
        port = listener.getsockname()[1]
        stop = threading.Event()
        actions = []
        def serve():
            while not stop.is_set():
                try:
                    conn, _ = listener.accept()
                except socket.timeout:
                    continue
                with conn:
                    size = struct.unpack(">I", conn.recv(4))[0]
                    request = json.loads(conn.recv(size))
                    action = request["action"]
                    actions.append(action)
                    data = {"auth.user_login": {"token": "PRIVATE_SESSION_TEST", "user": {"userId": 7, "mobile": "13800138000"}},
                            "user.get": {"user": {"userId": 7, "mobile": "13800138000"}},
                            "station.list": {"stations": [{"stationId": 1}]},
                            "station.detail": {"station": {"stationId": 1}, "chargers": [{"chargerId": 1, "stationId": 1}]},
                            "order.current": {"order": None}}.get(action)
                    if damage == "user" and action == "user.get":
                        data["user"]["userId"] = 99
                    if damage == "charger" and action == "station.detail":
                        data["chargers"][0]["stationId"] = 99
                    if damage == "order" and action == "order.current":
                        data["order"] = {"userId": 99, "status": "charging"}
                    if damage == "order_state" and action == "order.current":
                        data["order"] = {"userId": 7, "status": "completed"}
                    if damage == "token" and action == "auth.user_login":
                        data["token"] = ""
                    if action != "system.health" and action != "auth.user_login" and request["token"] != "PRIVATE_SESSION_TEST":
                        data = {}
                    response = envelope(request) if data is None else envelope(request, data=data)
                    if damage == "request_id":
                        response["requestId"] = "wrong-id"
                    elif damage == "envelope":
                        response["ok"] = "true"
                    elif damage == "health":
                        response["data"]["status"] = "waiting"
                    elif damage == "rejected":
                        response.update(ok=False, code="AUTH_REQUIRED",
                                        message="PRIVATE_MESSAGE_TEST",
                                        data={"token": "PRIVATE_SESSION_TEST"})
                    conn.sendall(packet(response))
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        try:
            yield port, actions
        finally:
            stop.set()
            thread.join(timeout=2)


@pytest.mark.parametrize("damage", ["user", "charger", "order", "order_state", "token"])
def test_business_smoke_validates_shape_and_ownership(damage):
    import demo_protocol
    with business_server(damage) as (port, _):
        connection = demo_protocol.Connection(port, 1)
        with pytest.raises(demo_protocol.ProtocolError):
            connection.business_smoke()
        assert "PRIVATE_SESSION_TEST" not in json.dumps(connection.events)


def test_smoke_readonly_actions_and_summary():
    import demo_protocol
    with business_server() as (port, actions):
        connection = demo_protocol.Connection(port, 1)
        connection.business_smoke()
        assert actions == ["auth.user_login", "user.get", "station.list", "station.detail", "order.current"]
        assert "PRIVATE_SESSION_TEST" not in json.dumps(connection.events)


def test_smoke_failure_report_written_for_dead_process(repo):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    path = repo / "runtime/demo-runs/a"
    proc = cli(repo, "smoke", "--run-id", "a")
    assert proc.returncode != 0
    report = json.loads((path / "smoke-report.json").read_text())
    assert report["ok"] is False
    assert "BASIC_SMOKE_PASS" not in json.dumps(report)


def test_stop_continues_other_correct_records_after_wrong_pid(repo, sleeper):
    import demo_processes
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    other = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    time.sleep(.05)
    try:
        path = repo / "runtime/demo-runs/a/manifest.json"
        manifest = json.loads(path.read_text())
        wrong = demo_processes.capture(sleeper.pid)
        wrong["exe"] = "/wrong"
        manifest["processes"] = {"client": wrong, "server": demo_processes.capture(other.pid)}
        path.write_text(json.dumps(manifest))
        output = result(cli(repo, "stop", "--run-id", "a"))
        assert output["processes"] == {"client": "WRONG_PID", "server": "EXITED"}
        assert sleeper.poll() is None
        assert other.wait(timeout=2) is not None
    finally:
        if other.poll() is None:
            other.terminate()
        other.wait(timeout=2)


def test_start_failure_default_term_preserves_unexited_record(repo, build):
    import demo_processes
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    manifest_path = repo / "runtime/demo-runs/a/manifest.json"
    try:
        proc = start(repo, build, env=fake_env(FIXTURE_MODE="sim_reject_ignores_term"))
        assert proc.returncode != 0
        manifest = json.loads(manifest_path.read_text())
        assert manifest["rollbackResults"]["simulator"] == "STOP_TIMEOUT"
        assert demo_processes.inspect(manifest["processes"]["simulator"]) == "ALIVE"
        assert manifest["state"] == "FAILED"
    finally:
        assert cli(repo, "stop", "--run-id", "a", "--timeout-seconds", "1", "--force").returncode == 0


def test_port_race_external_healthy_listener_not_accepted(repo, build, monkeypatch):
    import demo_runtime
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    runtime = demo_runtime.Runtime(repo)
    env = fake_env()
    monkeypatch.setenv("EV_TENCENT_MAP_KEY", env["EV_TENCENT_MAP_KEY"])
    monkeypatch.setenv("EV_SIMULATOR_TOKEN", env["EV_SIMULATOR_TOKEN"])
    selected = free_port()
    original = subprocess.Popen
    listener = socket.socket()
    def competing_launch(*args, **kwargs):
        command = args[0]
        if command[0] != "git":
            listener.bind(("127.0.0.1", selected))
            listener.listen()
        return original(*args, **kwargs)
    monkeypatch.setattr(subprocess, "Popen", competing_launch)
    try:
        args = SimpleNamespace(run_id="a", build_dir=build, port=selected, seed=20260901, interval_ms=1000, timeout_seconds=1, headless=True)
        with runtime.lock(), pytest.raises(demo_runtime.DemoError, match="服务端"):
            runtime.start(args)
        assert listener.fileno() >= 0
        manifest = json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())
        assert manifest["state"] == "FAILED"
    finally:
        listener.close()


def test_start_existing_later_log_rejected_before_server_launch(repo, build):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    path = repo / "runtime/demo-runs/a"
    (path / "simulator.log").write_text("preserve old")
    proc = start(repo, build)
    assert proc.returncode != 0
    assert json.loads((path / "manifest.json").read_text())["processes"] == {}
    assert (path / "simulator.log").read_text() == "preserve old"


def test_identity_capture_failure_preserves_unknown_child_record(repo, build, monkeypatch):
    import demo_runtime
    import demo_processes
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    runtime = demo_runtime.Runtime(repo)
    monkeypatch.setenv("EV_TENCENT_MAP_KEY", "invalid-offline-placeholder")
    monkeypatch.setenv("EV_SIMULATOR_TOKEN", "demo-simulator-token")
    original = demo_processes.capture
    owned = []
    def unavailable(pid):
        owned.append(original(pid))
        raise PermissionError("test proc inaccessible")
    monkeypatch.setattr(demo_processes, "capture", unavailable)
    try:
        args = SimpleNamespace(run_id="a", build_dir=build, port=free_port(), seed=0, interval_ms=1000, timeout_seconds=1, headless=True)
        with runtime.lock(), pytest.raises(demo_runtime.DemoError):
            runtime.start(args)
        manifest = json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())
        assert manifest["processes"]["server"]["pid"] == owned[0]["pid"]
        assert manifest["rollbackResults"]["server"] == "WRONG_PID"
    finally:
        monkeypatch.setattr(demo_processes, "capture", original)
        for record in owned:
            assert demo_processes.stop_one(record, 1, True) == "EXITED"


def test_smoke_manifest_owner_mismatch_writes_failure_report(repo):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    path = repo / "runtime/demo-runs/a"
    manifest = json.loads((path / "manifest.json").read_text())
    manifest["sourceRoot"] = "/other-repo"
    (path / "manifest.json").write_text(json.dumps(manifest))
    proc = cli(repo, "smoke", "--run-id", "a")
    assert result(proc)["code"] == "MANIFEST_INVALID"
    assert json.loads((path / "smoke-report.json").read_text())["ok"] is False


@pytest.mark.parametrize("record", ["bad", [], {"pid": True}, {"pid": -1}])
def test_malformed_process_record_never_crashes_or_signals(record):
    import demo_processes
    assert demo_processes.stop_one(record, .1, True) == "WRONG_PID"


def test_stop_pidfd_unsupported_explicitly_no_unsafe_fallback(sleeper, monkeypatch):
    import demo_processes
    record = demo_processes.capture(sleeper.pid)
    monkeypatch.delattr(os, "pidfd_open")
    assert demo_processes.stop_one(record, .1, True) == "PIDFD_UNSUPPORTED"
    assert sleeper.poll() is None


def test_config_file_fallback_and_blank_environment_priority(repo, build):
    (repo / "config.local.ini").write_text('[tencent]\nmapKey="invalid-offline-fixture"\n')
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    env = fake_env()
    env["EV_TENCENT_MAP_KEY"] = ""
    assert result(start(repo, build, env=env))["code"] == "CONFIG_MISSING"
    env.pop("EV_TENCENT_MAP_KEY")
    try:
        assert start(repo, build, env=env).returncode == 0
    finally:
        assert cli(repo, "stop", "--run-id", "a").returncode == 0


def test_start_default_gui_host_port_seed_interval_and_redaction(repo, build):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    env = fake_env(EV_SERVER_HOST="invalid.inherited.host", EV_SERVER_PORT="1")
    port = free_port()
    try:
        proc = cli(repo, "start", "--run-id", "a", "--build-dir", str(build), "--port", str(port), env=env)
        assert proc.returncode == 0, proc.stdout + proc.stderr
        path = repo / "runtime/demo-runs/a"
        manifest = json.loads((path / "manifest.json").read_text())
        assert manifest["headless"] is False
        assert manifest["seed"] == 20260901 and manifest["intervalMs"] == 3000
        for role in ("server", "simulator", "client"):
            pid = manifest["processes"][role]["pid"]
            assert Path(f"/proc/{pid}/cwd").resolve() == repo
            # 测试自有子进程只解析受控env中的host/port，不打印环境。
            values = Path(f"/proc/{pid}/environ").read_bytes().split(b"\0")
            assert b"EV_SERVER_HOST=127.0.0.1" in values
            assert f"EV_SERVER_PORT={port}".encode() in values
        command = Path(f"/proc/{manifest['processes']['server']['pid']}/cmdline").read_bytes().split(b"\0")
        assert b"--server" not in command
        for file in [path / "manifest.json", *path.glob("*.log")]:
            content = file.read_text()
            assert "invalid-offline-placeholder" not in content
            assert "demo-simulator-token" not in content
    finally:
        assert cli(repo, "stop", "--run-id", "a").returncode == 0


def test_protocol_total_timeout_on_slow_fragmented_response():
    import demo_protocol
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        def serve():
            with listener.accept()[0] as conn:
                size = struct.unpack(">I", conn.recv(4))[0]
                request = json.loads(conn.recv(size))
                for byte in packet(envelope(request)):
                    try:
                        conn.send(bytes([byte]))
                    except OSError:
                        break
                    time.sleep(.02)
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        started = time.monotonic()
        with pytest.raises(demo_protocol.ProtocolError):
            demo_protocol.Connection(listener.getsockname()[1], .15).health()
        assert time.monotonic() - started < .5
        thread.join(timeout=2)


def test_smoke_old_process_cannot_be_rescued_by_fresh_ready(repo, sleeper):
    import demo_processes
    from datetime import datetime, timezone
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    path = repo / "runtime/demo-runs/a"
    record = demo_processes.capture(sleeper.pid)
    record["starttime"] = "0"
    manifest = json.loads((path / "manifest.json").read_text())
    manifest.update(state="RUNNING", processes={role: record for role in ("server", "simulator", "client")}, port=free_port(), intervalMs=1000, simulatorStartedAt=time.time()-1)
    (path / "manifest.json").write_text(json.dumps(manifest))
    (path / "simulator-status.json").write_text(json.dumps({"schemaVersion": 1, "pid": sleeper.pid, "sessionState": "ready", "updatedAt": datetime.now(timezone.utc).isoformat()}))
    assert result(cli(repo, "smoke", "--run-id", "a"))["code"] == "PROCESS_NOT_ALIVE"
    assert sleeper.poll() is None


@pytest.mark.parametrize("value", [[], True, None, {"version": True}, {"version": 1, "processes": []}])
def test_malformed_manifest_returns_json_failure(repo, value):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    (repo / "runtime/demo-runs/a/manifest.json").write_text(json.dumps(value))
    proc = cli(repo, "stop", "--run-id", "a")
    assert result(proc)["code"] == "MANIFEST_INVALID"
    assert "Traceback" not in proc.stderr


def test_start_rejects_prepared_with_existing_process_record(repo, build, sleeper):
    import demo_processes
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    path = repo / "runtime/demo-runs/a/manifest.json"
    manifest = json.loads(path.read_text())
    manifest["processes"] = {"client": demo_processes.capture(sleeper.pid)}
    path.write_text(json.dumps(manifest))
    assert result(start(repo, build))["code"] == "RUN_NOT_PREPARED"
    assert sleeper.poll() is None


def test_reset_target_file_symlink_and_golden_symlink_rejected(repo):
    target = repo / "runtime/demo-runs/a"
    target.mkdir(parents=True)
    (target / "core-runtime.db").symlink_to(repo.parent / "outside.db")
    assert result(cli(repo, "reset", "--run-id", "a"))["code"] == "RUN_EXISTS"
    core = repo / "runtime/golden/core.db"
    moved = repo.parent / "fixture-core.db"
    core.rename(moved)
    core.symlink_to(moved)
    assert result(cli(repo, "reset", "--run-id", "b"))["code"] == "UNSAFE_PATH"


@pytest.mark.parametrize("damage", ["request_id", "envelope", "health", "rejected", "stale", "waiting_auth", "success"])
def test_cli_smoke_reports_tcp_and_status_failures(repo, damage):
    import demo_processes
    from datetime import datetime, timezone
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    path = repo / "runtime/demo-runs/a"
    children = [subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"]) for _ in range(3)]
    time.sleep(.05)
    records = {role: demo_processes.capture(child.pid) for role, child in zip(("server", "simulator", "client"), children)}
    try:
        with business_server(damage) as (port, _):
            manifest = json.loads((path / "manifest.json").read_text())
            manifest.update(state="RUNNING", processes=records, port=port, intervalMs=1000, simulatorStartedAt=time.time()-100)
            (path / "manifest.json").write_text(json.dumps(manifest))
            stamp = time.time() - (40 if damage == "stale" else 0)
            (path / "simulator-status.json").write_text(json.dumps({"schemaVersion": 1, "pid": children[1].pid,
                "sessionState": "waiting_auth" if damage == "waiting_auth" else "ready",
                "updatedAt": datetime.fromtimestamp(stamp, timezone.utc).isoformat()}))
            proc = cli(repo, "smoke", "--run-id", "a", "--timeout-seconds", "1")
            assert (proc.returncode == 0) == (damage == "success"), proc.stdout + proc.stderr
            report = json.loads((path / "smoke-report.json").read_text())
            assert report["ok"] == (damage == "success")
            assert "PRIVATE_SESSION_TEST" not in json.dumps(report)
            if damage == "success":
                assert report["code"] == "BASIC_SMOKE_PASS"
                assert report["otherMachine"] == "NOT_RUN_SEPARATE_GATE"
            else:
                assert "BASIC_SMOKE_PASS" not in json.dumps(report)
            if damage == "rejected":
                assert proc.returncode != 0
                assert report["requests"][0]["code"] == "AUTH_REQUIRED"
                assert set(report["requests"][0]) == {"action", "code", "requestId"}
                assert "PRIVATE_" not in json.dumps(report)
    finally:
        assert all(value == "EXITED" for value in demo_processes.stop_all(records, 1, True).values())
        for child in children:
            child.wait(timeout=2)


def test_start_bad_ini_configuration_returns_json(repo, build):
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    (repo / "config.local.ini").write_text("malformed ini contents")
    env = fake_env()
    env.pop("EV_TENCENT_MAP_KEY")
    proc = start(repo, build, env=env)
    assert result(proc)["code"] == "CONFIG_INVALID"
    assert "Traceback" not in proc.stderr


def test_reset_records_source_fingerprint_already_when_prepared(repo):
    subprocess.run(["git", "-C", str(repo), "init", "-q"], check=True)
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=Runtime Test", "-c", "user.email=runtime@example.invalid", "commit", "--allow-empty", "-qm", "fixture baseline"], check=True)
    sha = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    manifest = json.loads((repo / "runtime/demo-runs/a/manifest.json").read_text())
    assert manifest["sourceCommit"] == sha
    assert manifest["sourceDirty"] is True


@pytest.mark.parametrize("field,value", [("updatedAt", 123), ("updatedAt", None), ("schemaVersion", True), ("pid", "string")])
def test_ready_malformed_fields_are_rejected_without_traceback(tmp_path, sleeper, field, value):
    from demo_runtime import ready_status, DemoError
    import demo_processes
    from datetime import datetime, timezone
    path = tmp_path / "state.json"
    data = {"schemaVersion": 1, "pid": sleeper.pid, "sessionState": "ready", "updatedAt": datetime.now(timezone.utc).isoformat()}
    data[field] = value
    path.write_text(json.dumps(data))
    with pytest.raises(DemoError):
        ready_status(path, demo_processes.capture(sleeper.pid), time.time()-1, 1000)


@pytest.mark.parametrize("record", [{}, None, [], ""], ids=["empty-object", "null", "empty-list", "empty-string"])
@pytest.mark.parametrize("operation", ["reset", "start"])
def test_empty_corrupt_server_record_blocks_new_reset_and_start(repo, build, sleeper, record, operation):
    assert cli(repo, "reset", "--run-id", "old").returncode == 0
    assert cli(repo, "reset", "--run-id", "a").returncode == 0
    old_path = repo / "runtime/demo-runs/old/manifest.json"
    old = json.loads(old_path.read_text())
    old["processes"] = {"server": record}
    old_path.write_text(json.dumps(old))
    target = repo / "runtime/demo-runs/a"
    try:
        proc = cli(repo, "reset", "--run-id", "new-b") if operation == "reset" else start(repo, build)
        assert proc.returncode != 0, proc.stdout + proc.stderr
        assert result(proc)["code"] == "ACTIVE_SERVER"
        assert not (repo / "runtime/demo-runs/new-b").exists()
        manifest = json.loads((target / "manifest.json").read_text())
        assert manifest["state"] == "PREPARED"
        assert manifest["processes"] == {}
        assert not list(target.glob("*.log"))
        assert json.loads(old_path.read_text()) == old
    finally:
        # RED时旧实现可能启动a；仅通过该测试自己的清单回收。
        assert cli(repo, "stop", "--run-id", "a", "--timeout-seconds", "1").returncode == 0
        assert sleeper.poll() is None
