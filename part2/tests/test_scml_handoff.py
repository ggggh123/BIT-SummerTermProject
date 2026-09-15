"""#4 交接描述适配的独立回归；这里生成的是接口测试数据，不是正式生成器。"""
import csv
import json
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile
from part2.common.contracts import load_contract, raw_columns
from part2.common.handoff import sha256_file, verify_handoff
from part2.common.scml_handoff import SCML_PROFILE, PARTITIONED, label_records
from part2.scripts.build_python_bundle import build_bundle
from part2.scripts.make_fixture import fixture_records


def write_scml_fixture(root):
    contract = load_contract()
    tables, truth = fixture_records()
    manifest = {"contractVersion": "ods-dwd-v0.1", "kind": "prl-test-fixture", "runId": "scml-test", "run_id": "scml-test", "seed": 1, "dataWindow": {"start": "2026-08-01T00:00:00+08:00", "end": "2026-10-01T00:00:00+08:00", "days": 61}, "files": [], "tables": {}}
    for table, rows in tables.items():
        fmt = contract["tables"][table]["format"]
        source_table = "ods_" + table
        rel = source_table + ("/dt=2026-09-01" if table in PARTITIONED else "") + "/part-00000." + fmt
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        columns = raw_columns(table)
        if table == "stations":
            columns.remove("district")
            columns.insert(4, "district")
        if fmt == "csv":
            with path.open("w", newline="", encoding="utf-8") as target:
                writer = csv.DictWriter(target, fieldnames=columns)
                writer.writeheader()
                writer.writerows({key: "\\N" if value is None else value for key, value in row.items()} for row in rows)
        else:
            with path.open("w", encoding="utf-8") as target:
                for row in rows:
                    row = dict(row, id=int(row["id"]), entity_id=int(row["entity_id"]))
                    target.write(json.dumps(row, ensure_ascii=False) + "\n")
        item = {"table": source_table, "path": rel, "format": fmt, "rows": len(rows), "sha256": sha256_file(path)}
        if table in PARTITIONED:
            item["dt"] = "2026-09-01"
        manifest["files"].append(item)
        manifest["tables"][source_table] = {"rows": len(rows), "files": [rel]}
    log = {"contractVersion": "ods-dwd-v0.1", "run_id": "scml-test", "runId": "scml-test", "seed": 1, "totalInjected": len(truth), "issues": [dict(row, table="ods_" + row["table"]) for row in truth]}
    (root / "injection_log.json").write_text(json.dumps(log), encoding="utf-8")
    manifest["files"].append({"table": "injection_log", "path": "injection_log.json", "format": "json", "rows": len(truth), "sha256": sha256_file(root / "injection_log.json")})
    (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return manifest, log


class ScmlHandoffTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="prl-scml-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest, self.log = write_scml_fixture(self.root)

    def save_manifest(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def save_log(self):
        path = self.root / "injection_log.json"
        path.write_text(json.dumps(self.log))
        self.manifest["files"][-1]["sha256"] = sha256_file(path)
        self.save_manifest()

    def test_native_roundtrip_preserves_every_source_byte(self):
        before = {str(p.relative_to(self.root)): sha256_file(p) for p in self.root.rglob("*") if p.is_file()}
        result = verify_handoff(self.root)
        self.assertEqual(result["input_profile"], SCML_PROFILE)
        self.assertEqual(result["source_contract_version"], "ods-dwd-v0.1")
        self.assertEqual(set(item["table"] for item in result["files"] if item.get("table")), set(load_contract()["tables"]))
        self.assertEqual(before, {str(p.relative_to(self.root)): sha256_file(p) for p in self.root.rglob("*") if p.is_file()})

    def test_formal_gate_rejects_fixture_and_accepts_declared_full_handoff(self):
        with self.assertRaisesRegex(ValueError, "kind=ods-handoff"):
            verify_handoff(self.root, require_full=True)
        self.manifest["kind"] = "ods-handoff"
        self.save_manifest()
        self.assertEqual(verify_handoff(self.root, require_full=True)["kind"], "ods-handoff")

    def test_column_order_is_recorded_not_assumed(self):
        item = next(row for row in verify_handoff(self.root)["files"] if row.get("table") == "stations")
        self.assertEqual(item["columns"][4], "district")

    def test_labels_are_mapped_but_not_used_as_data(self):
        manifest = verify_handoff(self.root)
        self.assertEqual(label_records(self.log, manifest), fixture_records()[1])
        self.assertNotIn("expected_findings", manifest)

    def test_partition_outside_window_rejected(self):
        item = next(row for row in self.manifest["files"] if row["table"] == "ods_orders")
        item["dt"] = "2027-01-01"
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "分区"):
            verify_handoff(self.root)

    def test_invalid_calendar_partition_rejected(self):
        item = next(row for row in self.manifest["files"] if row["table"] == "ods_orders")
        item["dt"] = "2026-09-99"
        item["path"] = item["path"].replace("dt=2026-09-01", "dt=2026-09-99")
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "分区"):
            verify_handoff(self.root)

    def test_unknown_source_version_rejected(self):
        self.manifest["contractVersion"] = "future-version"
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "版本"):
            verify_handoff(self.root)

    def test_summary_count_mismatch_rejected(self):
        self.manifest["tables"]["ods_orders"]["rows"] += 1
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "行数"):
            verify_handoff(self.root)

    def test_wrong_label_batch_rejected(self):
        self.log["run_id"] = "another-batch"
        self.save_log()
        with self.assertRaisesRegex(ValueError, "批次"):
            verify_handoff(self.root)

    def test_unknown_label_row_rejected(self):
        self.log["issues"][0]["row_id"] = "missing-row"
        self.save_log()
        with self.assertRaisesRegex(ValueError, "不存在"):
            verify_handoff(self.root)

    def test_bad_payload_still_fails_hash_check(self):
        path = self.root / self.manifest["files"][0]["path"]
        with path.open("a") as target:
            target.write("bad\n")
        with self.assertRaisesRegex(ValueError, "校验和"):
            verify_handoff(self.root)

    def test_unlisted_data_file_rejected_but_success_marker_allowed(self):
        table_dir = self.root / "ods_orders"
        (table_dir / "_SUCCESS").write_text("")
        self.assertEqual(verify_handoff(self.root)["input_profile"], SCML_PROFILE)
        (table_dir / "unlisted.csv").write_text("unexpected\n")
        with self.assertRaisesRegex(ValueError, "未列入 manifest"):
            verify_handoff(self.root)

    def test_bundle_includes_schema_resource(self):
        path = self.root / "code.zip"
        build_bundle(path)
        with ZipFile(path) as archive:
            self.assertIn("part2/contracts/scml-dwd-v0.1.json", archive.namelist())
            self.assertIn("part2/scripts/__init__.py", archive.namelist())
            self.assertFalse(any(name.endswith(".parquet") for name in archive.namelist()))
