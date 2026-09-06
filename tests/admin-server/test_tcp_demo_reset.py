"""在线复位真实 TCP 回归；破坏性注入仅作用于临时副本。"""
import hashlib
from contextlib import closing
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import subprocess
import tempfile
import time
import unittest
import test_tcp_p0 as harness

GOLDEN = Path(__file__).resolve().parents[2] / "runtime/golden/core.db"
HASH = "5dd13bef7990c8166949d836a6fd8eadcc0b1ef8b11dc1b91272c33bead3a0f7"


class DemoResetTcp(unittest.TestCase):
    call = harness.TcpP0.call
    sql = harness.TcpP0.sql
    assert_time = harness.TcpP0.assert_time

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="ev-reset-")
        self.addCleanup(self.tmp.cleanup)
        self.db = Path(self.tmp.name) / "runtime.db"
        self.golden = Path(self.tmp.name) / "golden.db"
        shutil.copyfile(GOLDEN, self.golden)
        self.hash = HASH
        self.snapshot = Path(self.tmp.name) / "snapshot.json"
        self.seq = 0
        self.addCleanup(self.stop_server)
        self.start_server()

    def start_server(self):
        with socket.socket() as selector:
            selector.bind(("127.0.0.1", 0))
            port = selector.getsockname()[1]
        self.proc = subprocess.Popen([harness.SERVER, "--server", "--db", str(self.db),
            "--port", str(port), "--snapshot", str(self.snapshot), "--golden", str(self.golden),
            "--golden-hash", self.hash], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                self.fail(self.proc.stderr.read().decode())
            try:
                self.sock = socket.create_connection(("127.0.0.1",port),timeout=5)
                self.admin = self.call("admin.login", {"username":"admin","password":"123456"}, "")["data"]["token"]
                self.token = self.call("auth.user_login", {"mobile":"13800138000"}, "")["data"]["token"]
                return
            except OSError:
                time.sleep(.02)
        self.fail("服务端未监听")

    def stop_server(self):
        if hasattr(self,"sock"): self.sock.close()
        if not hasattr(self,"proc"): return
        if self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(timeout=8)
        if self.proc.stderr: self.proc.stderr.close()

    def reset(self, rid="reset", payload=None, raw=False):
        return self.call("demo.reset", {"confirmation":"RESET_DEMO"} if payload is None else payload, self.admin, rid, raw)

    def test_restore_replay_and_persistent_old_receipts(self):
        self.sql("UPDATE snapshot_meta SET version=90")
        self.sql("UPDATE users SET balance_fen=99 WHERE id=1")
        first = self.reset(raw=True)
        result=json.loads(first)
        self.assertTrue(result["ok"],result)
        self.assertEqual(result["data"]["goldenHash"],HASH)
        self.assert_time(result["data"]["resetAt"])
        for table,count in [("stations",6),("chargers",48),("users",30),("forecast_runs",0)]:
            self.assertEqual(self.sql(f"SELECT COUNT(*) FROM {table}"),[(count,)])
        self.assertEqual(self.sql("SELECT version FROM snapshot_meta"),[(91,)])
        self.assertEqual(self.sql("SELECT request_id FROM request_log"),[("reset",)])
        golden_balance=self.sql("SELECT balance_fen FROM users WHERE id=1")[0][0]
        self.assertTrue(self.call("wallet.recharge",{"amountFen":100})["ok"])
        self.assertEqual(self.reset(payload={"confirmation":"RESET_DEMO","ignored":9},raw=True),first)
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"),[(golden_balance+100,)])
        self.assertTrue(self.reset("reset-two")["ok"])
        self.assertTrue(self.call("wallet.recharge",{"amountFen":200})["ok"])
        self.stop_server(); self.start_server()
        self.assertEqual(self.reset(raw=True),first)
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"),[(golden_balance+200,)])
        self.assertEqual(self.call("wallet.recharge",{"amountFen":10},rid="reset")["code"],"FORBIDDEN")
        self.assertEqual(self.call("admin.user_set_status",{"userId":1,"status":"frozen"},self.admin,"reset")["code"],"INVALID_REQUEST")
        self.assertEqual(json.loads(self.snapshot.read_text())["snapshotVersion"],92)
        self.assert_time(json.loads(self.snapshot.read_text())["generatedAt"])

    def test_permissions_payload_and_identity_conflicts(self):
        for token,code in [("","AUTH_REQUIRED"),(self.token,"FORBIDDEN"),("sim-token","FORBIDDEN")]:
            self.assertEqual(self.call("demo.reset",{},token)["code"],code)
        for payload in [{},{"confirmation":True},{"confirmation":"reset_demo"},{"confirmation":1}]:
            self.assertEqual(self.reset(payload=payload)["code"],"INVALID_REQUEST")
        self.call("wallet.recharge",{"amountFen":10},rid="collision")
        self.assertEqual(self.reset("collision")["code"],"FORBIDDEN")
        self.assertTrue(self.reset()["ok"])
        self.sql("INSERT INTO admins(username,password_hash,created_at) SELECT 'other',password_hash,created_at FROM admins LIMIT 1")
        other=self.call("admin.login",{"username":"other","password":"123456"},"")["data"]["token"]
        self.assertEqual(self.call("demo.reset",{"confirmation":"RESET_DEMO"},other,"reset")["code"],"FORBIDDEN")

    def test_transaction_failure_rolls_back_everything(self):
        self.snapshot.write_text("last-good")
        self.sql("UPDATE users SET balance_fen=7654 WHERE id=1")
        self.sql("CREATE TRIGGER reject_reset BEFORE DELETE ON stations BEGIN SELECT RAISE(ABORT,'test reset failure'); END")
        result=self.reset()
        self.assertEqual(result["code"],"INTERNAL_ERROR")
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"),[(7654,)])
        self.assertEqual(self.sql("SELECT version FROM snapshot_meta"),[(0,)])
        self.assertEqual(self.sql("SELECT COUNT(*) FROM demo_reset_receipts"),[(0,)])
        self.assertEqual(self.snapshot.read_text(),"last-good")

    def test_snapshot_failure_is_stable_success_and_retry(self):
        target=Path(self.tmp.name)/"last-good.json"
        target.write_text("last-good")
        self.snapshot.symlink_to(target)
        # QSaveFile can replace a symlink: force a directory as output instead.
        self.snapshot.unlink(); self.snapshot.mkdir()
        (self.snapshot/"last-good").write_text("preserved")
        first=self.reset(raw=True)
        response=json.loads(first)
        self.assertTrue(response["ok"],response)
        self.assertIn("快照",response["message"])
        self.assertEqual((self.snapshot/"last-good").read_text(),"preserved")
        (self.snapshot/"last-good").unlink(); self.snapshot.rmdir()
        deadline=time.monotonic()+4
        while not self.snapshot.is_file() and time.monotonic()<deadline: time.sleep(.05)
        self.assertTrue(self.snapshot.is_file())
        self.assertEqual(json.loads(self.snapshot.read_text())["snapshotVersion"],1)
        self.assertEqual(self.reset(raw=True),first)

    def test_pending_survives_process_restart_without_second_reset(self):
        self.sql("CREATE TRIGGER reject_ack BEFORE INSERT ON request_log WHEN NEW.action='demo.reset' BEGIN SELECT RAISE(ABORT,'test ack failure'); END")
        self.assertEqual(self.reset()["code"],"INTERNAL_ERROR")
        self.assertEqual(self.sql("SELECT state,snapshot_version FROM demo_reset_receipts"),[("pending",1)])
        balance=self.sql("SELECT balance_fen FROM users WHERE id=1")[0][0]
        self.assertTrue(self.call("wallet.recharge",{"amountFen":100})["ok"])
        self.stop_server()
        self.sql("DROP TRIGGER reject_ack")
        self.start_server()
        first=self.reset(raw=True)
        self.assertTrue(json.loads(first)["ok"])
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"),[(balance+100,)])
        self.assertEqual(self.sql("SELECT version FROM snapshot_meta"),[(1,)])
        self.assertEqual(self.reset(raw=True),first)

    def test_old_restart_callback_cannot_complete_new_restart(self):
        self.sql("UPDATE chargers SET status='fault' WHERE id=1")
        self.assertTrue(self.call("admin.charger_restart",{"chargerId":1},self.admin)["ok"])
        time.sleep(.8)
        self.assertTrue(self.reset()["ok"])
        self.sql("UPDATE chargers SET status='fault' WHERE id=1")
        self.assertTrue(self.call("admin.charger_restart",{"chargerId":1},self.admin)["ok"])
        time.sleep(.9)
        self.assertEqual(self.sql("SELECT status FROM chargers WHERE id=1"),[("restarting",)])
        time.sleep(.8)
        self.assertEqual(self.sql("SELECT status FROM chargers WHERE id=1"),[("idle",)])

    def test_old_pending_across_new_reset_business_and_process_restart(self):
        self.sql("CREATE TRIGGER reject_old_ack BEFORE INSERT ON request_log WHEN NEW.request_id='old-pending' BEGIN SELECT RAISE(ABORT,'test ACK failure'); END")
        self.assertEqual(self.reset("old-pending")["code"],"INTERNAL_ERROR")
        metadata=self.sql("SELECT reset_at,golden_hash,snapshot_version FROM demo_reset_receipts WHERE request_id='old-pending'")
        self.assertEqual(metadata[0][1:],(HASH,1))
        self.assertTrue(self.reset("new-reset")["ok"])
        self.assertEqual(self.sql("SELECT state FROM demo_reset_receipts WHERE request_id='old-pending'"),[("pending",)])
        balance=self.sql("SELECT balance_fen FROM users WHERE id=1")[0][0]
        self.assertTrue(self.call("wallet.recharge",{"amountFen":321},rid="new-business")["ok"])
        snapshot=self.snapshot.read_bytes()
        self.assertEqual(json.loads(snapshot)["snapshotVersion"],2)
        self.stop_server()
        self.sql("DROP TRIGGER reject_old_ack")
        self.start_server()
        recovered=self.reset("old-pending",raw=True)
        response=json.loads(recovered)
        self.assertTrue(response["ok"],response)
        self.assertEqual(response["data"],{"resetAt":metadata[0][0],"goldenHash":HASH})
        self.assertEqual(self.sql("SELECT reset_at,golden_hash,snapshot_version FROM demo_reset_receipts WHERE request_id='old-pending'"),metadata)
        self.assertEqual(self.sql("SELECT state FROM demo_reset_receipts WHERE request_id='old-pending'"),[("final",)])
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"),[(balance+321,)])
        self.assertEqual(self.sql("SELECT version FROM snapshot_meta"),[(2,)])
        self.assertEqual(self.sql("SELECT COUNT(*) FROM request_log WHERE request_id='new-business'"),[(1,)])
        self.assertEqual(self.snapshot.read_bytes(),snapshot)
        self.assertEqual(self.reset("old-pending",raw=True),recovered)

    def test_golden_preflight_failures_do_not_mutate(self):
        self.sql("UPDATE users SET balance_fen=8765 WHERE id=1")
        for suffix in ["-wal","-shm","-journal"]:
            sidecar=Path(str(self.golden)+suffix); sidecar.touch()
            self.assertEqual(self.reset()["code"],"INTERNAL_ERROR")
            sidecar.unlink()
        with self.golden.open("ab") as stream: stream.write(b"bad-hash")
        self.assertEqual(self.reset()["code"],"INTERNAL_ERROR")
        self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"),[(8765,)])
        self.assertEqual(self.sql("SELECT COUNT(*) FROM demo_reset_receipts"),[(0,)])

    def test_same_file_rejected_before_open_even_via_hardlink(self):
        self.stop_server()
        alias=Path(self.tmp.name)/"alias.db"
        os.link(self.golden,alias)
        for runtime,snapshot in [(self.golden,self.snapshot),(alias,self.snapshot),(self.db,self.golden),(self.db,alias)]:
            proc=subprocess.Popen([harness.SERVER,"--server","--db",str(runtime),"--golden",str(self.golden),
                "--golden-hash",HASH,"--snapshot",str(snapshot),"--port","0"],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                _,error=proc.communicate(timeout=1)
            except subprocess.TimeoutExpired:
                proc.terminate(); proc.communicate(timeout=5)
                self.fail("同文件配置必须在打开运行库前拒绝")
            self.assertNotEqual(proc.returncode,0,error)
            self.assertEqual(hashlib.sha256(self.golden.read_bytes()).hexdigest(),HASH)
            for suffix in ["-wal","-shm","-journal"]: self.assertFalse(Path(str(runtime)+suffix).exists())

    def test_approved_hash_does_not_bypass_schema_integrity_or_fk(self):
        for damage in ["DROP INDEX idx_orders_one_active_per_user", "UPDATE schema_version SET version=2",
                       "ALTER TABLE users ADD COLUMN unexpected TEXT", "UPDATE chargers SET station_id=999 WHERE id=1", "corrupt"]:
            self.stop_server()
            shutil.copyfile(GOLDEN,self.golden)
            if damage=="corrupt": self.golden.write_bytes(b"not a sqlite database")
            else:
                with closing(sqlite3.connect(self.golden)) as db, db: db.execute(damage)
            self.hash=hashlib.sha256(self.golden.read_bytes()).hexdigest()
            self.start_server()
            self.sql("UPDATE users SET balance_fen=123 WHERE id=1")
            self.assertEqual(self.reset()["code"],"INTERNAL_ERROR",damage)
            self.assertEqual(self.sql("SELECT balance_fen FROM users WHERE id=1"),[(123,)])
            self.assertEqual(self.sql("SELECT COUNT(*) FROM demo_reset_receipts"),[(0,)])

    def test_default_database_path_is_checked_before_open(self):
        self.stop_server()
        for db_args in [[], ["--db", " \t "]]:
            for collision in ["golden", "snapshot"]:
                for alias_kind in ["direct", "hardlink"]:
                    with self.subTest(db_args=db_args, collision=collision, alias_kind=alias_kind):
                        with tempfile.TemporaryDirectory(prefix="ev-reset-default-") as isolated:
                            root=Path(isolated)
                            default_db=root/"NeusoftTraining/ChargingPlatformServer/charging_platform_server_data_v1.db"
                            default_db.parent.mkdir(parents=True)
                            shutil.copyfile(GOLDEN,default_db)
                            other_golden=root/"golden.db"
                            shutil.copyfile(GOLDEN,other_golden)
                            alias=default_db
                            if alias_kind=="hardlink":
                                alias=root/"alias.db"
                                os.link(default_db,alias)
                            golden=alias if collision=="golden" else other_golden
                            snapshot=alias if collision=="snapshot" else root/"snapshot.json"
                            proc=subprocess.Popen([harness.SERVER,"--server",*db_args,"--golden",str(golden),
                                "--golden-hash",HASH,"--snapshot",str(snapshot),"--port","0"],
                                env={**os.environ,"XDG_DATA_HOME":isolated},stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                            try:
                                output,error=proc.communicate(timeout=1)
                            except subprocess.TimeoutExpired:
                                proc.terminate(); proc.communicate(timeout=5)
                                self.fail("默认运行库与黄金/快照同文件必须在打开前拒绝")
                            self.assertEqual(proc.returncode,1,error)
                            self.assertNotIn(b"listening on",output)
                            self.assertEqual(hashlib.sha256(default_db.read_bytes()).hexdigest(),HASH)
                            self.assertEqual(hashlib.sha256(other_golden.read_bytes()).hexdigest(),HASH)
                            for file in [default_db,alias,other_golden]:
                                for suffix in ["-wal","-shm","-journal"]:
                                    self.assertFalse(Path(str(file)+suffix).exists())


if __name__ == "__main__": unittest.main()
