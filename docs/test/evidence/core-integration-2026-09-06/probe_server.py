"""只连接本脚本启动的回环服务与独立诊断库；不修改仓库或真实运行库。"""
import argparse
import datetime as dt
import json
import os
import socket
import sqlite3
import struct
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.parse_args()
root = Path(__file__).resolve().parent
database = root / "probe-fresh.db"
if database.exists():
    raise SystemExit("拒绝复用已有诊断库，请使用新的导出目录")
with socket.socket() as selector:
    selector.bind(("127.0.0.1", 0))
    port = selector.getsockname()[1]
env = {**os.environ, "TZ": "Asia/Shanghai"}
process = subprocess.Popen([
    str(root / "build/apps/admin-server/ev_admin_server"), "--server",
    "--db", str(database), "--host", "127.0.0.1", "--port", str(port),
    "--snapshot", str(root / "probe-snapshot.json")
], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
results = []
connection = None
try:
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("server startup failed: " + process.stdout.read())
        try:
            connection = socket.create_connection(("127.0.0.1", port), timeout=2)
            break
        except OSError:
            time.sleep(0.05)
    if connection is None:
        raise RuntimeError("server did not listen")

    def read_exact(count):
        result = b""
        while len(result) < count:
            part = connection.recv(count - len(result))
            if not part:
                raise RuntimeError("unexpected EOF")
            result += part
        return result

    def request(action, payload=None, token="", request_id=None):
        request_id = request_id or f"inspect-{len(results) + 1}"
        message = {"version": 1, "requestId": request_id, "action": action,
                   "token": token, "payload": payload or {}}
        raw = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
        connection.sendall(struct.pack("!I", len(raw)) + raw)
        size = struct.unpack("!I", read_exact(4))[0]
        assert 0 < size <= 1024 * 1024
        response_raw = read_exact(size)
        response = json.loads(response_raw)
        assert set(response) == {"requestId", "ok", "code", "message", "data"}
        assert response["requestId"] == request_id
        results.append({"action": action, "requestId": request_id,
                        "ok": response["ok"], "code": response["code"]})
        return response, response_raw

    health, _ = request("system.health")
    assert set(health["data"]) == {"status", "schemaVersion", "snapshotVersion", "forecastRunId", "serverTime"}
    print("health:", json.dumps(health, ensure_ascii=False))
    login, _ = request("auth.user_login", {"mobile": "13800138000"})
    assert login["ok"], login
    token = login["data"]["token"]
    initial_balance = login["data"]["user"]["balanceFen"]
    stations, _ = request("station.list", {"latitude": 39.9830, "longitude": 116.3150}, token)
    current, _ = request("order.current", token=token)
    print("initial-read:", json.dumps({"stationCount": len(stations["data"]["stations"]), "currentOrder": current["data"]["order"]}, ensure_ascii=False))
    first, first_raw = request("wallet.recharge", {"amountFen": 100}, token, "inspect-same-recharge")
    again, again_raw = request("wallet.recharge", {"amountFen": 100}, token, "inspect-same-recharge")
    print("duplicate-recharge:", json.dumps({"before": initial_balance, "first": first["data"], "repeat": again["data"], "sameAckBytes": first_raw == again_raw}, ensure_ascii=False))
    status, _ = request("simulator.status", {"state": "paused", "simulatedAt": "2026-09-06T01:00:00+08:00", "eventCount": 0}, "sim-token")
    charger = next(c for c in status["data"]["chargers"] if c["status"] == "idle")
    reserve, _ = request("charge.reserve", {"chargerId": charger["chargerId"]}, token)
    order_id = reserve["data"]["order"]["orderId"]
    start, _ = request("charge.start", {"orderId": order_id}, token)
    sample = {"chargerId": charger["chargerId"], "recordedAt": "2026-09-06T01:00:05+08:00", "powerKw": 60, "energyIncrementKwh": 0.25, "status": "charging"}
    telemetry, telemetry_raw = request("telemetry.push", sample, "sim-token", "inspect-same-telemetry")
    duplicate, duplicate_raw = request("telemetry.push", sample, "sim-token", "inspect-same-telemetry")
    print("duplicate-telemetry:", json.dumps({"firstOk": telemetry["ok"], "repeatOk": duplicate["ok"], "repeatCode": duplicate["code"], "sameAckBytes": telemetry_raw == duplicate_raw}, ensure_ascii=False))
    sample.update(recordedAt="2026-09-06T01:00:10+08:00", energyIncrementKwh=0.5)
    second, _ = request("telemetry.push", sample, "sim-token")
    stopped, _ = request("charge.stop", {"orderId": order_id}, token)
    fields = ("energyKwh", "amountFen", "elapsedSec", "status", "startedAt", "endedAt")
    print("stop-preservation:", json.dumps({"beforeStop": {k: second["data"]["order"][k] for k in fields}, "afterStop": {k: stopped["data"]["order"][k] for k in fields}}, ensure_ascii=False))
    settled, _ = request("charge.settle", {"orderId": order_id}, token)
    print("settle:", json.dumps({"ok": settled["ok"], "status": settled["data"]["order"]["status"], "balanceFen": settled["data"]["balanceFen"]}, ensure_ascii=False))
    request("simulator.fault_set", {"chargerId": charger["chargerId"], "recordedAt": "2026-09-06T01:00:11+08:00", "fault": True}, "sim-token")
    recovery, _ = request("simulator.fault_set", {"chargerId": charger["chargerId"], "recordedAt": "2026-09-06T01:00:11+08:00", "fault": False}, "sim-token")
    print("same-second-recovery:", json.dumps({"ok": recovery["ok"], "code": recovery["code"]}))
    reset, _ = request("demo.reset", {}, token)
    print("demo-reset-route:", json.dumps({"ok": reset["ok"], "code": reset["code"], "message": reset["message"]}, ensure_ascii=False))
    with sqlite3.connect(f"file:{database}?mode=ro", uri=True) as db:
        columns = [row[1] for row in db.execute("PRAGMA table_info(request_log)")]
        print("database-check:", json.dumps({"integrity": db.execute("PRAGMA integrity_check").fetchone()[0], "requestLogColumns": columns, "stationCount": db.execute("SELECT COUNT(*) FROM stations").fetchone()[0], "chargerCount": db.execute("SELECT COUNT(*) FROM chargers").fetchone()[0]}, ensure_ascii=False))
    print("requests:", json.dumps(results, ensure_ascii=False))
finally:
    if connection is not None:
        connection.close()
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    print("server-stopped:", process.returncode)
