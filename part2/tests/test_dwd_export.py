"""交接包完整性测试；假 Parquet 字节仅用于哈希校验，不用作 Spark 验收。"""
import json
import tempfile
import unittest
from pathlib import Path
from part2.common.handoff import sha256_file
from part2.scripts.export_dwd import verify_export


class DwdExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ev-part2-export-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tables = ("dim_users", "dim_stations", "dim_chargers", "dwd_order_detail", "dwd_telemetry_detail", "dwd_station_hourly")
        result = {"ok": True, "run_id": "test", "assertions": [{"table": table, "ok": True, "rows": 1} for table in self.tables]}
        for table in self.tables:
            path = self.root / "dwd" / table / "part-0.parquet"
            path.parent.mkdir(parents=True)
            path.write_bytes(b"test checksum only; not actual Parquet")
        (self.root / "reports").mkdir()
        for path in (self.root / "reports/run_result.json", self.root / "RUN_SUCCEEDED.json"):
            path.write_text(json.dumps(result))
        self.manifest = {"package_version": "prl-dwd-package-0.1-draft", "kind": "prl-dwd-fixture", "run_id": "test", "table_rows": {table: 1 for table in self.tables}, "files": [{"path": str(path.relative_to(self.root)), "bytes": path.stat().st_size, "sha256": sha256_file(path)} for path in self.root.rglob("*") if path.is_file()]}
        self.save()

    def save(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def test_hash_roundtrip(self):
        self.assertEqual(verify_export(self.root)["kind"], "prl-dwd-fixture")

    def test_payload_tamper_fails(self):
        (self.root / "dwd/dim_users/part-0.parquet").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "校验失败"):
            verify_export(self.root)

    def test_unlisted_file_fails(self):
        (self.root / "extra.csv").write_text("unexpected")
        with self.assertRaisesRegex(ValueError, "未列入"):
            verify_export(self.root)

    def test_misreported_rows_fail(self):
        self.manifest["table_rows"]["dim_users"] = 99
        self.save()
        with self.assertRaisesRegex(ValueError, "行数"):
            verify_export(self.root)

    def test_path_escape_fails(self):
        self.manifest["files"][0]["path"] = "../outside"
        self.save()
        with self.assertRaisesRegex(ValueError, "越界"):
            verify_export(self.root)

    def test_missing_data_fails(self):
        path = self.root / "dwd/dim_users/part-0.parquet"
        self.manifest["files"] = [item for item in self.manifest["files"] if item["path"] != str(path.relative_to(self.root))]
        path.unlink()  # 仅删除本测试创建的假数据。
        self.save()
        with self.assertRaisesRegex(ValueError, "缺少 DWD"):
            verify_export(self.root)
