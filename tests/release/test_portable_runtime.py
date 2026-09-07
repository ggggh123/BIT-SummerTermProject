"""便携发行运行时的外部行为测试。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
from types import SimpleNamespace

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "release"))

from portable_runtime import DemoError, PortableRuntime


PROGRAMS = {
    "server": "bin/ev_admin_server",
    "simulator": "bin/ev_charger_simulator",
    "client": "bin/ev_user_client",
}


@pytest.fixture
def bundle(tmp_path):
    root = tmp_path / "发行 包"
    program = r'''#!/usr/bin/python3
import json, os, signal, socket, struct, sys, time
from datetime import datetime, timezone

role = os.path.basename(sys.argv[0])
mode = os.environ.get("PORTABLE_FIXTURE_MODE", "")
def arg(name): return sys.argv[sys.argv.index(name) + 1]

if role == "ev_admin_server":
    if mode == "server_exit": sys.exit(2)
    port = int(arg("--port"))
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen()
    print("ev_admin_server mode=headless listening on 127.0.0.1:" + str(port)
          + ", db=" + arg("--db") + ", snapshot=" + arg("--snapshot"), flush=True)
    while True:
        conn, _ = listener.accept()
        with conn:
            header = conn.recv(4)
            if not header: continue
            size = struct.unpack(">I", header)[0]
            request = json.loads(conn.recv(size))
            data = {"status":"degraded", "schemaVersion":1, "snapshotVersion":0,
                    "forecastRunId":None, "serverTime":"2026-09-07T00:00:00+08:00"}
            body = json.dumps({"requestId":request["requestId"], "ok":True,
                "code":"OK", "message":"ready", "data":data}).encode()
            conn.sendall(struct.pack(">I", len(body)) + body)
elif role == "ev_charger_simulator":
    while True:
        status = {"schemaVersion":1, "pid":os.getpid(), "sessionState":"ready",
                  "updatedAt":datetime.now(timezone.utc).isoformat()}
        path = os.environ["EV_SIMULATOR_STATUS_FILE"]
        with open(path + ".tmp", "w") as stream: json.dump(status, stream)
        os.replace(path + ".tmp", path)
        time.sleep(.1)
else:
    if mode == "client_exit": sys.exit(3)
    time.sleep(60)
'''
    binaries = {}
    for role, relative in PROGRAMS.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        content = program.encode()
        path.write_bytes(content)
        path.chmod(0o755)
        binaries[role] = {
            "path": relative,
            "sha256": hashlib.sha256(content).hexdigest(),
        }
    release = {
        "schemaVersion": 1,
        "releaseId": "1.0.0-20260907-3aa9a27",
        "sourceCommit": "3aa9a2721d493584a40640d9a0147d802d9723e6",
        "platform": "ubuntu22.04-x86_64",
        "binaries": binaries,
    }
    (root / "release.json").write_text(
        json.dumps(release, ensure_ascii=False), encoding="utf-8"
    )
    (root / "config.local.ini").write_text(
        "[tencent]\nmapKey=bundle-fixture-key\n", encoding="utf-8"
    )
    (root / "runtime/golden").mkdir(parents=True)
    for name in ("core.db", "core.db.sha256"):
        shutil.copyfile(ROOT / "runtime/golden" / name, root / "runtime/golden" / name)
    (root / "database").mkdir()
    for name in ("create_runtime_copy.py", "build_golden.py", "seed_demo.py"):
        shutil.copyfile(ROOT / "database" / name, root / "database" / name)
    return root


def test_verified_binaries_need_no_build_cache(bundle, tmp_path):
    runtime = PortableRuntime(bundle, data_home=tmp_path / "用户数据")
    build, binaries = runtime.build_info(bundle)
    assert set(binaries) == {"server", "simulator", "client"}
    assert build == bundle.resolve()
    assert not (bundle / "CMakeCache.txt").exists()
    assert binaries["server"]["sha256"] == hashlib.sha256(
        (bundle / "bin/ev_admin_server").read_bytes()
    ).hexdigest()


def test_tampered_binary_is_rejected_before_start(bundle, tmp_path, monkeypatch):
    (bundle / "bin/ev_user_client").write_bytes(b"#!/bin/sh\nexit 3\n")
    monkeypatch.setenv("DISPLAY", ":fixture")
    runtime = PortableRuntime(bundle, data_home=tmp_path / "数据")
    with pytest.raises(DemoError) as caught:
        runtime.start(args(port=free_port()))
    assert caught.value.code == "BINARY_MISMATCH"
    assert not any(path.is_dir() for path in runtime.runs.iterdir())


def test_user_config_overrides_bundle_and_runtime_generates_private_token(
        bundle, tmp_path, monkeypatch):
    (bundle / "config.local.ini").write_text(
        "[tencent]\nmapKey=bundle-fixture-key\n", encoding="utf-8"
    )
    runtime = PortableRuntime(bundle, data_home=tmp_path / "用户数据")
    runtime.data_root.mkdir(parents=True)
    runtime.config_path.write_text(
        "[tencent]\nmapKey=user-fixture-key\n", encoding="utf-8"
    )
    monkeypatch.delenv("EV_TENCENT_MAP_KEY", raising=False)
    monkeypatch.delenv("EV_SIMULATOR_TOKEN", raising=False)

    key, token = runtime.configuration()

    assert key == "user-fixture-key"
    assert len(token) >= 32
    assert token != key
    assert "user-fixture-key" not in json.dumps(runtime.fingerprint())
    assert list(runtime.data_root.glob("*.json")) == []


def test_explicit_environment_key_has_highest_priority(bundle, tmp_path, monkeypatch):
    (bundle / "config.local.ini").write_text(
        "[tencent]\nmapKey=bundle-fixture-key\n", encoding="utf-8"
    )
    runtime = PortableRuntime(bundle, data_home=tmp_path / "用户数据")
    runtime.data_root.mkdir(parents=True)
    runtime.config_path.write_text(
        "[tencent]\nmapKey=user-fixture-key\n", encoding="utf-8"
    )
    monkeypatch.setenv("EV_TENCENT_MAP_KEY", "environment-fixture-key")
    monkeypatch.setenv("EV_SIMULATOR_TOKEN", "external-token-must-not-be-used")
    key, token = runtime.configuration()
    assert key == "environment-fixture-key"
    assert token != "external-token-must-not-be-used"


@pytest.mark.parametrize(
    "contents,code",
    [(None, "CONFIG_INVALID"), ("not an ini", "CONFIG_INVALID"),
     ("[tencent]\nmapKey=   \n", "CONFIG_MISSING")],
)
def test_default_config_errors_are_explicit_before_start(
        bundle, tmp_path, monkeypatch, contents, code):
    config = bundle / "config.local.ini"
    if contents is None:
        config.unlink()
    else:
        config.write_text(contents, encoding="utf-8")
    monkeypatch.setenv("DISPLAY", ":fixture")
    monkeypatch.delenv("EV_TENCENT_MAP_KEY", raising=False)
    runtime = PortableRuntime(bundle, data_home=tmp_path / "data")
    with pytest.raises(DemoError) as caught:
        runtime.start(args(port=free_port()))
    assert caught.value.code == code
    assert not any(path.is_dir() for path in runtime.runs.iterdir())


def test_data_home_isolated_by_release_id(bundle, tmp_path):
    runtime = PortableRuntime(bundle, data_home=tmp_path / "XDG 数据")
    assert runtime.data_root == (
        tmp_path / "XDG 数据/evcharging/1.0.0-20260907-3aa9a27"
    ).absolute()
    assert runtime.runs == runtime.data_root / "runs"
    assert runtime.bundle not in runtime.data_root.parents


def args(run_id="round-01", port=9100, **changes):
    values = dict(run_id=run_id, build_dir=None, port=port, seed=20260901,
                  interval_ms=1000, timeout_seconds=1, headless=False,
                  force=False, software_rendering=False)
    values.update(changes)
    return SimpleNamespace(**values)


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def test_stopped_history_survives_bundle_move_and_old_record_does_not_block(
        bundle, tmp_path):
    data_home = tmp_path / "private data"
    runtime = PortableRuntime(bundle, data_home=data_home)
    runtime.reset(args("old-round"))
    assert runtime.stop(args("old-round"))["code"] == "STOPPED"
    moved = tmp_path / "移动后的发行包"
    bundle.rename(moved)

    relocated = PortableRuntime(moved, data_home=data_home)
    status = relocated.status(args("old-round"))
    assert status["state"] == "STOPPED"
    assert status["installPathChanged"] is True
    relocated.reset(args("new-round"))
    assert (relocated.runs / "new-round/core-runtime.db").is_file()


def test_port_conflict_does_not_create_a_run_or_start_processes(
        bundle, tmp_path, monkeypatch):
    monkeypatch.setenv("DISPLAY", ":fixture")
    runtime = PortableRuntime(bundle, data_home=tmp_path / "data")
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        with pytest.raises(DemoError) as caught:
            runtime.start(args(port=listener.getsockname()[1]))
    assert caught.value.code == "PORT_IN_USE"
    assert not runtime.runs.exists() or not any(
        path.is_dir() for path in runtime.runs.iterdir()
    )


def test_client_early_exit_rolls_back_registered_processes(
        bundle, tmp_path, monkeypatch):
    import demo_processes
    monkeypatch.setenv("DISPLAY", ":fixture")
    monkeypatch.setenv("PORTABLE_FIXTURE_MODE", "client_exit")
    runtime = PortableRuntime(bundle, data_home=tmp_path / "data")
    with pytest.raises(DemoError) as caught:
        runtime.start(args(port=free_port()))
    assert caught.value.code == "CLIENT_FAILED"
    manifest = json.loads((runtime.runs / "round-01/manifest.json").read_text())
    assert manifest["state"] == "FAILED"
    assert set(manifest["processes"]) == {"server", "simulator", "client"}
    assert all(demo_processes.inspect(record) == "EXITED"
               for record in manifest["processes"].values())
    serialized = json.dumps(manifest)
    assert "bundle-fixture-key" not in serialized
    assert "EV_SIMULATOR_TOKEN" not in serialized


def test_no_desktop_is_rejected_before_creating_run(bundle, tmp_path, monkeypatch):
    monkeypatch.delenv("DISPLAY", raising=False)
    monkeypatch.delenv("WAYLAND_DISPLAY", raising=False)
    runtime = PortableRuntime(bundle, data_home=tmp_path / "data")
    with pytest.raises(DemoError) as caught:
        runtime.start(args(port=free_port()))
    assert caught.value.code == "DESKTOP_MISSING"
    assert not runtime.runs.exists()


def test_moving_running_bundle_is_reported_and_stop_still_targets_records(
        bundle, tmp_path, monkeypatch):
    monkeypatch.setenv("DISPLAY", ":fixture")
    data_home = tmp_path / "data"
    runtime = PortableRuntime(bundle, data_home=data_home)
    first = args("active-round", port=free_port())
    runtime.start(first)
    moved = tmp_path / "running bundle moved"
    bundle.rename(moved)
    relocated = PortableRuntime(moved, data_home=data_home)
    try:
        with pytest.raises(DemoError) as caught:
            relocated.start(args("new-round", port=free_port()))
        assert caught.value.code == "BUNDLE_MOVED_RUNNING"
        assert relocated.status(args("active-round"))["installPathChanged"] is True
    finally:
        assert relocated.stop(args("active-round"))["code"] == "STOPPED"


def materialize_support(bundle):
    support = bundle / "support"
    support.mkdir()
    for source in (
        ROOT / "scripts/release/portable_launcher.py",
        ROOT / "scripts/release/portable_runtime.py",
        ROOT / "scripts/demo_runtime.py",
        ROOT / "scripts/demo_processes.py",
        ROOT / "scripts/demo_protocol.py",
    ):
        shutil.copyfile(source, support / source.name)


def cli(bundle, data_home, command, *extra, env=None):
    values = os.environ.copy()
    values.update(DISPLAY=":fixture", XDG_DATA_HOME=str(data_home), PATH="")
    if env:
        values.update(env)
    return subprocess.run(
        [sys.executable, str(bundle / "support/portable_launcher.py"), command, *extra],
        cwd=bundle.parent, env=values, text=True, capture_output=True, timeout=15,
    )


def cli_result(process):
    return json.loads(process.stdout.splitlines()[-1])


def test_cli_start_status_stop_with_defaults_and_redacted_output(bundle, tmp_path):
    materialize_support(bundle)
    data_home = tmp_path / "CLI 用户数据"
    port = free_port()
    started = cli(bundle, data_home, "start", "--run-id", "cli-round",
                  "--port", str(port), "--timeout-seconds", "1",
                  "--software-rendering")
    assert started.returncode == 0, started.stdout + started.stderr
    start_result = cli_result(started)
    assert start_result["code"] == "RUNNING"
    manifest = json.loads(Path(start_result["runPath"], "manifest.json").read_text())
    assert manifest["port"] == port
    assert manifest["seed"] == 20260901
    assert manifest["intervalMs"] == 3000
    assert not (bundle / "runtime/demo-runs").exists()
    for record in manifest["processes"].values():
        environment = Path(f"/proc/{record['pid']}/environ").read_bytes().split(b"\0")
        assert b"QT_QUICK_BACKEND=software" in environment
        assert b"LIBGL_ALWAYS_SOFTWARE=1" in environment
    try:
        status = cli(bundle, data_home, "status", "--timeout-seconds", "1")
        assert status.returncode == 0, status.stdout + status.stderr
        output = status.stdout
        report = cli_result(status)
        assert report["runId"] == "cli-round"
        assert report["server"] == "服务可用"
        assert report["simulator"] == "已鉴权"
        assert report["client"] == "图形进程存活"
        assert "bundle-fixture-key" not in output
        assert "EV_SIMULATOR_TOKEN" not in output
        assert "操作通过" in output
    finally:
        stopped = cli(bundle, data_home, "stop", "--timeout-seconds", "1")
        assert stopped.returncode == 0, stopped.stdout + stopped.stderr
        assert cli_result(stopped)["code"] == "STOPPED"
