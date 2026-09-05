"""真实回环 TCP P0 回归；所有 SQL 注入仅作用于本测试创建的临时库。"""
import datetime as dt
from contextlib import closing
import json
import os
from pathlib import Path
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import unittest

SERVER = sys.argv.pop(1)


class TcpP0(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ev-tcp-p0-")
        self.addCleanup(self.tmp.cleanup)
        self.db = Path(self.tmp.name) / "test.db"
        self.seq = 0
        self.start_server()
        self.token = self.call("auth.user_login", {"mobile": "13800138000"})["data"]["token"]

    def start_server(self):
        with socket.socket() as selector:
            selector.bind(("127.0.0.1", 0))
            port = selector.getsockname()[1]
        self.proc = subprocess.Popen(
            [SERVER, "--server", "--db", str(self.db), "--host", "127.0.0.1", "--port", str(port),
             "--snapshot", str(Path(self.tmp.name) / "snapshot.json")],
            env={**os.environ, "QT_QPA_PLATFORM": "offscreen"}, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(self.stop_server)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                self.fail("服务端启动失败")
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
                return
            except OSError:
                time.sleep(0.02)
        self.fail("服务端未监听")

    def stop_server(self):
        self.sock.close()
        if self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(timeout=5)

    def call(self, action, payload=None, token=None, rid=None, raw=False):
        self.seq += 1
        rid = rid or f"req-{self.seq}"
        body = json.dumps({"version": 1, "requestId": rid, "action": action,
                           "token": token if token is not None else getattr(self, "token", ""),
                           "payload": payload or {}}, separators=(",", ":")).encode()
        self.sock.sendall(struct.pack("!I", len(body)) + body)
        def exact(n):
            data = b""
            while len(data) < n:
                part = self.sock.recv(n - len(data))
                self.assertTrue(part)
                data += part
            return data
        data = exact(struct.unpack("!I", exact(4))[0])
        response = json.loads(data)
        self.assertEqual(set(response), {"requestId", "ok", "code", "message", "data"})
        self.assertEqual(response["requestId"], rid)
        return data if raw else response

    def sql(self, statement, args=()):
        with closing(sqlite3.connect(self.db)) as db, db:
            return db.execute(statement, args).fetchall()

    def order(self, start=True):
        result = self.call("charge.reserve", {"chargerId": 1})
        self.assertTrue(result["ok"], result)
        oid = result["data"]["order"]["orderId"]
        if start:
            self.assertTrue(self.call("charge.start", {"orderId": oid})["ok"])
        return oid

    def sample(self, rid="sample", at="2026-09-06T10:00:00+08:00", energy=0.75):
        return self.call("telemetry.push", {"chargerId": 1, "recordedAt": at, "powerKw": 60,
                        "energyIncrementKwh": energy, "status": "charging"}, "sim-token", rid)

    def assert_time(self, value):
        self.assertRegex(value, r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?\+08:00$")
        self.assertEqual(dt.datetime.fromisoformat(value).utcoffset(), dt.timedelta(hours=8))

    def test_new_business_times(self):
        login = self.call("auth.user_login", {"mobile": "13911112222"}, "")
        self.assert_time(login["data"]["user"]["registeredAt"])
        admin = self.call("admin.login", {"username": "admin", "password": "123456"}, "")
        self.assert_time(admin["data"]["admin"]["createdAt"])
        oid = self.order()
        sample = self.sample()
        self.assert_time(sample["data"]["acceptedAt"])
        order = self.call("charge.stop", {"orderId": oid})["data"]["order"]
        for key in ("reservedAt", "startedAt", "endedAt"):
            self.assert_time(order[key])
        status = self.call("simulator.status", {"state": "paused", "eventCount": 0,
                           "simulatedAt": "2026-09-06T11:00:00+08:00"}, "sim-token")
        self.assert_time(status["data"]["acceptedAt"])
        for charger in status["data"]["chargers"]:
            self.assert_time(charger["updatedAt"])
        for (created,) in self.sql("SELECT created_at FROM request_log"):
            self.assert_time(created)

    def test_stop_preserves_telemetry_and_actual_duration(self):
        oid = self.order()
        before = self.sample()["data"]["order"]
        self.assertEqual((before["energyKwh"], before["amountFen"]), (0.75, 113))
        stopped = self.call("charge.stop", {"orderId": oid})["data"]["order"]
        self.assertEqual((stopped["energyKwh"], stopped["amountFen"], stopped["status"]), (0.75, 113, "charging"))
        self.assertEqual(self.sql("SELECT status FROM chargers WHERE id=1"), [("charging",)])
        self.assertEqual(self.sample("after-stop", "2026-09-06T10:00:01+08:00", 1)["data"]["order"]["energyKwh"], 0.75)
        self.sql("UPDATE orders SET started_at='2026-09-06T09:00:00+08:00', ended_at='2026-09-06T09:02:17+08:00' WHERE id=?", (oid,))
        settled = self.call("charge.settle", {"orderId": oid})
        self.assertTrue(settled["ok"], settled)
        self.assertEqual(settled["data"]["balanceFen"], 49887)
        self.assertEqual(settled["data"]["order"]["elapsedSec"], 137)
        self.assertEqual(self.sql("SELECT charge_count,total_duration_sec FROM chargers WHERE id=1"), [(1, 137)])

    def test_recharge_replay_persists_across_restart_and_identity(self):
        first = self.call("wallet.recharge", {"amountFen": 100}, rid="recharge", raw=True)
        self.assertEqual(first, self.call("wallet.recharge", {"amountFen": 100}, rid="recharge", raw=True))
        self.stop_server()
        self.start_server()
        self.token = self.call("auth.user_login", {"mobile": "13800138000"}, "")["data"]["token"]
        self.assertEqual(first, self.call("wallet.recharge", {"amountFen": 100}, rid="recharge", raw=True))
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"), [(50100,)])
        other = self.call("auth.user_login", {"mobile": "13911112222"}, "")["data"]["token"]
        denied = self.call("wallet.recharge", {"amountFen": 100}, other, "recharge")
        self.assertFalse(denied["ok"])
        self.assertEqual(denied["data"], {})
        self.assertFalse(self.call("wallet.recharge", {"amountFen": 200}, rid="recharge")["ok"])
        self.assertFalse(self.call("user.update", {"nickname": "冲突"}, rid="recharge")["ok"])
        self.assertEqual(self.call("wallet.recharge", {"amountFen": 100}, "bad-token", "recharge")["code"], "AUTH_REQUIRED")
        logs = str(self.sql("SELECT * FROM request_log"))
        self.assertNotIn(self.token, logs)
        self.assertNotIn(other, logs)

    def test_telemetry_replay_before_cursor(self):
        self.order()
        first = self.sample()
        self.assertEqual(first, self.sample())
        self.assertEqual(self.sql("SELECT COUNT(*) FROM telemetry"), [(1,)])
        self.assertEqual(self.sql("SELECT energy_kwh,amount_fen FROM orders"), [(0.75, 113)])
        self.assertFalse(self.sample("new-id")["ok"])

    def test_order_mutations_replay_the_original_success_after_state_changes(self):
        def replay(action, payload, rid):
            first = self.call(action, payload, rid=rid, raw=True)
            self.assertTrue(json.loads(first)["ok"])
            self.assertEqual(first, self.call(action, payload, rid=rid, raw=True))
            return json.loads(first)["data"]["order"]
        oid = replay("charge.reserve", {"chargerId": 1}, "reserve")["orderId"]
        replay("charge.start", {"orderId": oid}, "start")
        self.sample()
        replay("charge.stop", {"orderId": oid}, "stop")
        replay("charge.settle", {"orderId": oid}, "settle")
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"), [(49887,)])
        self.assertEqual(self.sql("SELECT charge_count FROM chargers WHERE id=1"), [(1,)])
        oid = replay("charge.reserve", {"chargerId": 1}, "reserve-2")["orderId"]
        replay("order.cancel", {"orderId": oid}, "cancel")

    def test_busy_returns_contract_error_without_side_effects(self):
        with closing(sqlite3.connect(self.db)) as locked:
            locked.execute("BEGIN IMMEDIATE")
            result = self.call("wallet.recharge", {"amountFen": 100}, rid="busy")
            self.assertEqual(result["code"], "DB_BUSY")
            self.assertEqual(result["data"], {})
            locked.rollback()
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"), [(50000,)])
        self.assertEqual(self.sql("SELECT COUNT(*) FROM request_log WHERE request_id='busy'"), [(0,)])
        self.assertTrue(self.call("wallet.recharge", {"amountFen": 100}, rid="busy")["ok"])

    def test_small_energy_increment_is_not_rounded_before_billing(self):
        self.order()
        self.assertEqual(self.sample(energy=0.1234567)["data"]["order"]["energyKwh"], 0.1234567)

    def test_cursor_compares_instants_and_fault_freezes_once(self):
        oid = self.order()
        self.assertTrue(self.sample()["ok"])
        self.assertFalse(self.sample("same-instant", "2026-09-06T10:00:00.000+08:00")["ok"])
        self.assertTrue(self.sample("next-ms", "2026-09-06T10:00:00.001+08:00", 0)["ok"])
        self.sql("UPDATE orders SET amount_fen=0 WHERE id=?", (oid,))
        fault_payload = {"chargerId": 1, "recordedAt": "2026-09-06T10:00:00.002+08:00", "fault": True}
        first = self.call("simulator.fault_set", fault_payload, "sim-token", "fault", raw=True)
        self.assertTrue(json.loads(first)["ok"])
        self.assertEqual(first, self.call("simulator.fault_set", fault_payload, "sim-token", "fault", raw=True))
        self.assertEqual(self.sql("SELECT energy_kwh,amount_fen,ended_at FROM orders"), [(0.75, 113, fault_payload["recordedAt"])])

    def test_cursor_preserves_arbitrary_fraction_across_telemetry_fault_and_restart(self):
        self.order()
        self.assertTrue(self.sample("fraction-1", "2026-09-06T10:00:00.0001+08:00", 0.1)["ok"])
        first = self.sample("fraction-2", "2026-09-06T10:00:00.0002+08:00", 0.1)
        self.assertTrue(first["ok"], first)
        self.assertEqual(self.sample("fraction-between", "2026-09-06T10:00:00.00015+08:00")["code"], "ORDER_STATE_CONFLICT")
        self.assertEqual(self.sample("fraction-equal", "2026-09-06T10:00:00.0002000+08:00")["code"], "ORDER_STATE_CONFLICT")
        fault = {"chargerId": 1, "recordedAt": "2026-09-06T10:00:00.0002000000000000000000000000001+08:00", "fault": True}
        fault_ack = self.call("simulator.fault_set", fault, "sim-token", "fraction-fault", raw=True)
        self.assertTrue(json.loads(fault_ack)["ok"], json.loads(fault_ack))
        self.assertEqual(first, self.sample("fraction-2", "2026-09-06T10:00:00.0002+08:00", 0.1))
        self.assertEqual(fault_ack, self.call("simulator.fault_set", fault, "sim-token", "fraction-fault", raw=True))
        self.stop_server()
        self.start_server()
        equal_fault = {**fault, "fault": False, "recordedAt": "2026-09-06T10:00:00.00020000000000000000000000000010+08:00"}
        self.assertEqual(self.call("simulator.fault_set", equal_fault, "sim-token", "equal-fault")["code"], "ORDER_STATE_CONFLICT")
        later = "2026-09-06T10:00:00.0002000000000000000000000000002+08:00"
        self.assertTrue(self.call("simulator.fault_set", {**fault, "fault": False, "recordedAt": later}, "sim-token", "fraction-recover")["ok"])
        telemetry = {"chargerId": 1, "recordedAt": later, "powerKw": 0, "energyIncrementKwh": 0, "status": "fault"}
        self.assertEqual(self.call("telemetry.push", telemetry, "sim-token", "equal-recover")["code"], "ORDER_STATE_CONFLICT")
        telemetry["recordedAt"] = "2026-09-06T10:00:00.0002000000000000000000000000003+08:00"
        self.assertTrue(self.call("telemetry.push", telemetry, "sim-token", "later-recover")["ok"])
        self.assertEqual(self.sql("SELECT COUNT(*) FROM telemetry"), [(5,)])
        self.assertEqual(self.sql("SELECT energy_kwh, amount_fen FROM orders"), [(0.2, 30)])

    def test_cursor_selects_exact_maximum_with_mixed_whole_and_fraction_strings(self):
        self.order()
        for timestamp in ("2026-09-06T10:00:00.0002+08:00", "2026-09-06T10:00:00+08:00", "2026-09-06T10:00:00.0001+08:00"):
            self.sql("INSERT INTO telemetry(charger_id,recorded_at,power_kw,energy_increment_kwh,event_type) VALUES(1,?,0,0,'telemetry')", (timestamp,))
        self.assertEqual(self.sample("max-equal", "2026-09-06T10:00:00.000200+08:00")["code"], "ORDER_STATE_CONFLICT")
        self.assertTrue(self.sample("max-next", "2026-09-06T10:00:00.0003+08:00", 0)["ok"])
        self.assertTrue(self.sample("next-second", "2026-09-06T10:00:01+08:00", 0)["ok"])
        self.assertEqual(self.sample("whole-equal", "2026-09-06T10:00:01.0000000000000000000+08:00")["code"], "ORDER_STATE_CONFLICT")

    def test_forecast_publish_keeps_two_phase_and_stable_final_ack(self):
        cutoff = dt.datetime.fromisoformat("2026-09-06T00:00:00+08:00")
        payload = {"runId": "p0-run", "modelVersion": "test", "dataCutoff": cutoff.isoformat(),
                   "generatedAt": cutoff.isoformat(), "records": [
                       {"stationId": station, "forecastAt": (cutoff + dt.timedelta(hours=hour)).isoformat(),
                        "horizonH": hour, "predictedLoadKw": 0, "predictedBusyCount": 0,
                        "predictedIdleCount": 8, "congestionLevel": "low", "isPeak": False}
                       for station in range(1, 7) for hour in range(1, 25)]}
        self.sql("CREATE TRIGGER injected_failure BEFORE INSERT ON request_log BEGIN SELECT RAISE(ABORT,'private ACK failure'); END")
        failed = self.call("forecast.publish", payload, "ml-token", "forecast")
        self.assertFalse(failed["ok"], failed)
        self.assertEqual(self.sql("SELECT COUNT(*) FROM forecasts"), [(144,)])
        version = self.sql("SELECT version FROM snapshot_meta")
        self.sql("DROP TRIGGER injected_failure")
        first = self.call("forecast.publish", payload, "ml-token", "forecast", raw=True)
        self.assertTrue(json.loads(first)["ok"])
        self.assertEqual(first, self.call("forecast.publish", payload, "ml-token", "forecast", raw=True))
        self.assertEqual(self.sql("SELECT version FROM snapshot_meta"), version)

    def test_atomic_sql_and_ack_failures(self):
        for action, stage, table in (("charge.start", "reserved", "chargers"),
                                     ("order.cancel", "reserved", "chargers"),
                                     ("charge.settle", "stopped", "orders"),
                                     ("charge.settle", "stopped", "chargers"),
                                     ("wallet.recharge", "none", "request_log"),
                                     ("charge.reserve", "none", "request_log"),
                                     ("charge.start", "reserved", "request_log"),
                                     ("charge.stop", "charging", "request_log"),
                                     ("order.cancel", "reserved", "request_log"),
                                     ("charge.settle", "stopped", "request_log"),
                                     ("telemetry.push", "charging", "request_log")):
            with self.subTest(action=action, failure_table=table):
                self.sql("DROP TRIGGER IF EXISTS injected_failure")
                self.sql("DELETE FROM orders")
                self.sql("UPDATE chargers SET status='idle' WHERE id=1")
                oid = self.order(stage != "reserved") if stage != "none" else None
                if stage == "stopped":
                    self.sql("UPDATE orders SET ended_at='2026-09-06T10:00:00+08:00',energy_kwh=.75,amount_fen=113 WHERE id=?", (oid,))
                snapshot = [self.sql(f"SELECT * FROM {t}") for t in ("users", "orders", "chargers", "telemetry", "request_log")]
                event = "INSERT" if table == "request_log" else "UPDATE"
                self.sql(f"CREATE TRIGGER injected_failure BEFORE {event} ON {table} BEGIN SELECT RAISE(ABORT,'injected private SQL failure'); END")
                rid = f"failure-{self.seq}"
                if action == "telemetry.push":
                    result = self.sample(rid)
                else:
                    payload = {"amountFen": 100} if action == "wallet.recharge" else ({"chargerId": 1} if action == "charge.reserve" else {"orderId": oid})
                    result = self.call(action, payload, rid=rid)
                self.assertFalse(result["ok"], result)
                self.assertIn(result["code"], ("DB_BUSY", "INTERNAL_ERROR"))
                self.assertNotIn("injected", result["message"])
                self.assertEqual([self.sql(f"SELECT * FROM {t}") for t in ("users", "orders", "chargers", "telemetry", "request_log")], snapshot)
                self.sql("DROP TRIGGER injected_failure")
                retry = self.sample(rid) if action == "telemetry.push" else self.call(action, payload, rid=rid)
                self.assertTrue(retry["ok"], retry)

    def test_commit_failure_rolls_back_business_and_ack(self):
        self.sql("CREATE TABLE deferred_failure (user_id INTEGER REFERENCES users(id) DEFERRABLE INITIALLY DEFERRED)")
        self.sql("CREATE TRIGGER injected_failure AFTER INSERT ON request_log BEGIN INSERT INTO deferred_failure VALUES(-1); END")
        result = self.call("wallet.recharge", {"amountFen": 100}, rid="commit-failure")
        self.assertEqual(result["code"], "INTERNAL_ERROR")
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"), [(50000,)])
        self.assertEqual(self.sql("SELECT COUNT(*) FROM request_log WHERE request_id='commit-failure'"), [(0,)])
        self.assertEqual(self.sql("SELECT COUNT(*) FROM deferred_failure"), [(0,)])
        self.sql("DROP TRIGGER injected_failure")
        self.assertTrue(self.call("wallet.recharge", {"amountFen": 100}, rid="commit-failure")["ok"])

    def test_active_order_guard_and_settle_preserves_recovery_state(self):
        for state in ("fault", "restarting", "idle"):
            with self.subTest(state=state):
                self.sql("DELETE FROM orders")
                self.sql("UPDATE chargers SET status='idle', total_duration_sec=0 WHERE id=1")
                oid = self.order()
                self.sql("UPDATE orders SET started_at='2026-09-06T09:00:00+08:00',ended_at='2026-09-06T09:02:17+08:00',energy_kwh=.75,amount_fen=113 WHERE id=?", (oid,))
                self.sql("UPDATE chargers SET status=? WHERE id=1", (state,))
                other = self.call("auth.user_login", {"mobile": "13911112222"}, "")["data"]["token"]
                denied = self.call("charge.reserve", {"chargerId": 1}, other)
                self.assertEqual(denied["code"], "CHARGER_NOT_AVAILABLE")
                self.assertTrue(self.call("charge.settle", {"orderId": oid})["ok"])
                self.assertEqual(self.sql("SELECT status FROM chargers WHERE id=1"), [(state,)])
                self.assertEqual(self.sql("SELECT total_duration_sec FROM chargers WHERE id=1"), [(137,)])
                self.sql("UPDATE chargers SET status='idle' WHERE id=1")


if __name__ == "__main__":
    unittest.main(verbosity=2)
