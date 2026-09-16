"""PRL 质量报告写入 ADS SQLite 的事务、批次与备份回归。"""
import json
import sqlite3
import tempfile
import unittest
from pathlib import Path
from part2.scripts.publish_quality_ads import publish


class QualityAdsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="prl-quality-ads-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.db, self.backup = self.root / "ads.db", self.root / "ads.before.db"
        self.manifest = self.root / "ads_manifest.json"
        con = sqlite3.connect(self.db)
        con.executescript("""
CREATE TABLE ads_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE ads_quality_table(name TEXT PRIMARY KEY,rows_before INTEGER NOT NULL,rows_after INTEGER NOT NULL);
CREATE TABLE ads_quality_issue(rule TEXT PRIMARY KEY,type TEXT NOT NULL,injected INTEGER NOT NULL,detected INTEGER NOT NULL,handled INTEGER NOT NULL,recall REAL NOT NULL);
CREATE TABLE ads_quality_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE untouched(value TEXT);
INSERT INTO ads_meta VALUES('runId','ads-1'); INSERT INTO ads_meta VALUES('sourceRunId','source-1'); INSERT INTO ads_quality_meta VALUES('old','old'); INSERT INTO untouched VALUES('preserve');
""")
        con.commit(); con.close()
        common = {"run_id": "quality-1", "source_run_id": "source-1", "contract_version": "0.1.0-draft", "policy_version": "0.1.0-draft", "dwd_profile": "scml-dwd-v0.1", "ready_for_team_delivery": False}
        self.q = self.root / "quality.json"; self.c = self.root / "cleaning.json"
        quality = {**common, "report_type": "quality_report", "source_contract_version": "ods-dwd-v0.1", "pending_policy_notes": ["待确认"], "rules": {}, "injection_comparison": {}}
        for n in range(1, 11):
            quality["rules"][f"Q{n}"] = {"name": f"问题{n}"}
            quality["injection_comparison"][f"Q{n}"] = {"truePositive": n, "falsePositive": 1, "falseNegative": 2, "recall": n/(n+2), "precision": n/(n+1)}
        dwd_tables = ("dwd_order_detail", "dwd_telemetry_detail", "dwd_station_hourly", "dwd_event", "dim_stations", "dim_chargers", "dim_users")
        cleaning = {**common, "report_type": "cleaning_report", "tables": {name: {"before": 10, "after": 9} for name in ("users", "stations", "chargers", "orders", "telemetry", "station_hourly", "events")}, "dwd_assertions": [{"table": name, "ok": True} for name in dwd_tables]}
        self.q.write_text(json.dumps(quality)); self.c.write_text(json.dumps(cleaning))
        self.manifest.write_text(json.dumps({
            "contractVersion": "ads-flask-v1",
            "runId": "ads-1",
            "sourceRunId": "source-1",
            "database": "ads.db",
            "tables": {"ads_quality_table": 1, "ads_quality_issue": 1},
            "quality": {"source": "baseline"},
        }))

    def test_pending_requires_explicit_acknowledgement(self):
        with self.assertRaisesRegex(ValueError, "accept-pending"):
            publish(self.q, self.c, self.db, self.backup)
        self.assertFalse(self.backup.exists())

    def test_transaction_replaces_only_quality_tables_and_keeps_backup(self):
        result = publish(self.q, self.c, self.db, self.backup, True, self.manifest)
        self.assertTrue(result["ok"]); self.assertTrue(result["accepted_pending_policy"])
        con = sqlite3.connect(self.db)
        self.assertEqual(con.execute("SELECT COUNT(*) FROM ads_quality_table").fetchone()[0], 7)
        self.assertEqual(con.execute("SELECT COUNT(*) FROM ads_quality_issue").fetchone()[0], 10)
        self.assertEqual(con.execute("SELECT value FROM untouched").fetchone()[0], "preserve")
        row = con.execute("SELECT injected,detected,handled,recall FROM ads_quality_issue WHERE rule='R01'").fetchone()
        self.assertEqual(row[:3], (3, 2, 2)); self.assertAlmostEqual(row[3], 1/3)
        exact = json.loads(con.execute("SELECT value FROM ads_quality_meta WHERE key='exactMetrics'").fetchone()[0])
        self.assertEqual(exact["R01"]["falsePositive"], 1)
        con.close()
        old = sqlite3.connect(self.backup)
        self.assertEqual(old.execute("SELECT value FROM ads_quality_meta WHERE key='old'").fetchone()[0], "old")
        old.close()
        manifest = json.loads(self.manifest.read_text(encoding="utf-8"))
        self.assertEqual(manifest["tables"]["ads_quality_table"], 7)
        self.assertEqual(manifest["tables"]["ads_quality_issue"], 10)
        self.assertEqual(manifest["quality"]["source"], "prl-quality-report")
        self.assertTrue(manifest["quality"]["acceptedPendingPolicy"])
        self.assertEqual(manifest["databaseSha256"], result["database_sha256"])

    def test_manifest_batch_mismatch_rejected_before_backup(self):
        manifest = json.loads(self.manifest.read_text(encoding="utf-8"))
        manifest["sourceRunId"] = "other"
        self.manifest.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "清单.*来源批次"):
            publish(self.q, self.c, self.db, self.backup, True, self.manifest)
        self.assertFalse(self.backup.exists())

    def test_source_batch_mismatch_rejected_before_backup(self):
        con = sqlite3.connect(self.db); con.execute("UPDATE ads_meta SET value='other' WHERE key='sourceRunId'"); con.commit(); con.close()
        with self.assertRaisesRegex(ValueError, "来源批次"):
            publish(self.q, self.c, self.db, self.backup, True)
        self.assertFalse(self.backup.exists())

    def test_schema_mismatch_rejected(self):
        con = sqlite3.connect(self.db); con.execute("ALTER TABLE ads_quality_issue ADD COLUMN drift TEXT"); con.commit(); con.close()
        with self.assertRaisesRegex(ValueError, "schema"):
            publish(self.q, self.c, self.db, self.backup, True)

    def test_report_pair_mismatch_rejected(self):
        data = json.loads(self.c.read_text(encoding="utf-8")); data["run_id"] = "other"; self.c.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "同一成功批次"):
            publish(self.q, self.c, self.db, self.backup, True)

    def test_duplicate_assertion_cannot_masquerade_as_seven_tables(self):
        data = json.loads(self.c.read_text(encoding="utf-8"))
        data["dwd_assertions"] = [{"table": "dim_users", "ok": True} for _ in range(7)]
        self.c.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "七表"):
            publish(self.q, self.c, self.db, self.backup, True)

    def test_existing_backup_is_never_overwritten(self):
        self.backup.write_text("keep")
        with self.assertRaisesRegex(ValueError, "备份已存在"):
            publish(self.q, self.c, self.db, self.backup, True)
        self.assertEqual(self.backup.read_text(encoding="utf-8"), "keep")

    def test_cascade_metadata_preserves_missing_vs_zero(self):
        data = json.loads(self.c.read_text(encoding="utf-8"))
        data["tables"]["orders"]["cascade_affected_rows"] = 3
        data["tables"]["users"]["cascade_affected_rows"] = 0
        self.c.write_text(json.dumps(data))
        publish(self.q, self.c, self.db, self.backup, True)
        with sqlite3.connect(self.db) as con:
            details = json.loads(con.execute("SELECT value FROM ads_quality_meta WHERE key='tableDetails'").fetchone()[0])
        self.assertEqual(details["ods_orders"]["cascadeAffectedRows"], 3)
        self.assertEqual(details["ods_users"]["cascadeAffectedRows"], 0)
        self.assertIsNone(details["ods_stations"]["cascadeAffectedRows"])
