"""真实三程序offscreen组合；运行目录和日志永久留作证据，不等于人工GUI验收。"""
import hashlib
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import time
import uuid

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import demo_processes

BUILD = os.environ.get("EV_DEMO_BUILD_DIR")
pytestmark = pytest.mark.skipif(not BUILD, reason="EV_DEMO_BUILD_DIR未配置，真实三端门禁未运行")


def run(command, run_id, *extra, bad_token=False):
    env = os.environ.copy()
    env.update(EV_TENCENT_MAP_KEY="invalid-offline-placeholder", EV_SIMULATOR_TOKEN="invalid-simulator-test" if bad_token else "demo-simulator-token",
               QT_QPA_PLATFORM="offscreen")
    script = "smoke_test.sh" if command == "smoke" else command + "_demo.sh"
    process = subprocess.run([str(ROOT / "scripts" / script), "--run-id", run_id, *extra],
                             cwd="/tmp", env=env, text=True, capture_output=True, timeout=55)
    return process, json.loads(process.stdout.splitlines()[-1])


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


@pytest.mark.parametrize("bad_token", [False, True], ids=["real-three-programs", "bad-simulator-token-rollback"])
def test_live_reset_start_smoke_stop(bad_token):
    run_id = "live-" + time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    path = ROOT / "runtime/demo-runs" / run_id
    golden = ROOT / "runtime/golden/core.db"
    golden_hash = hashlib.sha256(golden.read_bytes()).hexdigest()
    optional = ROOT / "runtime/golden/demo.db"
    optional_hash = hashlib.sha256(optional.read_bytes()).hexdigest()
    before_runs = {entry.name for entry in (ROOT / "runtime/demo-runs").iterdir()} if (ROOT / "runtime/demo-runs").exists() else set()
    reset, _ = run("reset", run_id)
    assert reset.returncode == 0, reset.stdout + reset.stderr
    try:
        started, output = run("start", run_id, "--build-dir", BUILD, "--headless", "--port", str(port()),
                              "--interval-ms", "1000", "--timeout-seconds", "5", bad_token=bad_token)
        if bad_token:
            assert started.returncode != 0
            assert output["code"] == "SIMULATOR_AUTH_FAILED", output
            manifest = json.loads((path / "manifest.json").read_text())
            assert manifest["state"] == "FAILED"
            assert set(manifest["processes"]) == {"server", "simulator"}
            assert all(demo_processes.inspect(record) == "EXITED" for record in manifest["processes"].values())
        else:
            assert started.returncode == 0, started.stdout + started.stderr
            first_ready = json.loads((path / "simulator-status.json").read_text())["updatedAt"]
            # 超过fresh最小5秒，证实paused时状态也周期刷新。
            time.sleep(5.2)
            assert json.loads((path / "simulator-status.json").read_text())["updatedAt"] > first_ready
            smoke, report = run("smoke", run_id, "--timeout-seconds", "5")
            assert smoke.returncode == 0, smoke.stdout + smoke.stderr
            assert report["code"] == "BASIC_SMOKE_PASS"
            assert report["tencentNavigation"] == "NOT_RUN_SEPARATE_GATE"
    finally:
        stopped, output = run("stop", run_id, "--timeout-seconds", "5")
        assert stopped.returncode == 0, output
    manifest = json.loads((path / "manifest.json").read_text())
    assert manifest["state"] == "STOPPED"
    assert all(demo_processes.inspect(record) == "EXITED" for record in manifest["processes"].values())
    assert before_runs <= {entry.name for entry in path.parent.iterdir()}
    assert hashlib.sha256(golden.read_bytes()).hexdigest() == golden_hash
    assert hashlib.sha256(optional.read_bytes()).hexdigest() == optional_hash
    with sqlite3.connect((path / "core-runtime.db").as_uri() + "?mode=ro", uri=True) as database:
        assert database.execute("PRAGMA integrity_check").fetchone() == ("ok",)
        assert database.execute("PRAGMA foreign_key_check").fetchall() == []
        assert database.execute("SELECT COUNT(*) FROM stations").fetchone() == (6,)
        assert database.execute("SELECT COUNT(*) FROM chargers").fetchone() == (48,)
        if not bad_token:
            assert database.execute("SELECT COUNT(*) FROM request_log WHERE action='simulator.status'").fetchone()[0] > 0
    print(f"保留真实运行证据：{path}；三端进程均已确认退出；黄金SHA256未变")
