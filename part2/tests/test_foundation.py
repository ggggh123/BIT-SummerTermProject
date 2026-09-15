"""启动阶段测试：契约、可复现夹具、交接校验、标准化与指标数学。"""
import json
import tempfile
import unittest
from pathlib import Path
from part2.common.contracts import load_contract, validate_record
from part2.common.handoff import verify_handoff, sha256_file
from part2.clean.normalization import money_to_fen, expected_amount_fen, normalize_timestamp, normalize_text, validate_order_times
from part2.quality.metrics import score_findings
from part2.scripts.make_fixture import fixture_records, write_fixture
from part2.scripts.configure_local import xml_config, write_new_or_same
import xml.etree.ElementTree as ET


class ContractTests(unittest.TestCase):
    def test_contract_is_cached_for_full_scale_row_validation(self):
        load_contract.cache_clear()
        first = load_contract()
        second = load_contract()
        info = load_contract.cache_info()
        self.assertIs(first, second)
        self.assertEqual((info.misses, info.hits), (1, 1))

    def test_draft_is_not_team_approval(self):
        self.assertEqual(load_contract()["status"], "DRAFT_PENDING_TL_SCML_PE_REVIEW")

    def test_coverage_and_row_identity(self):
        tables, expected = fixture_records()
        self.assertEqual({item["rule"] for item in expected}, {f"Q{i}" for i in range(1, 11)})
        identities = {(table, row["_row_id"]) for table, rows in tables.items() for row in rows}
        self.assertEqual(len(identities), sum(map(len, tables.values())))
        for item in expected:
            self.assertIn((item["table"], item["row_id"]), identities)

    def test_raw_values_remain_strings(self):
        tables, _ = fixture_records()
        for table, rows in tables.items():
            for row in rows:
                validate_record(table, row)

    def test_numeric_inference_is_rejected(self):
        tables, _ = fixture_records()
        row = dict(tables["users"][0], balance_fen=100)
        with self.assertRaises(ValueError):
            validate_record("users", row)

    def test_unexpected_fields_rejected(self):
        tables, _ = fixture_records()
        with self.assertRaises(ValueError):
            validate_record("users", dict(tables["users"][0], phone="13800000001"))

    def test_first_stage_field_mapping(self):
        tables = load_contract()["tables"]
        self.assertEqual(tables["users"]["rename"]["mobile"], "phone")
        self.assertEqual(tables["chargers"]["rename"]["power_kw"], "rated_power_kw")
        self.assertEqual(tables["telemetry"]["rename"]["energy_increment_kwh"], "energy_delta_kwh")


class HandoffTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ev-part2-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "sample"
        write_fixture(self.root)

    def test_roundtrip(self):
        self.assertEqual(verify_handoff(self.root)["kind"], "prl-test-fixture")

    def test_repeat_hashes_identical(self):
        second = Path(self.temp.name) / "second"
        write_fixture(second)
        for path in self.root.iterdir():
            self.assertEqual(sha256_file(path), sha256_file(second / path.name))

    def test_nonempty_output_is_not_overwritten(self):
        with self.assertRaises(ValueError):
            write_fixture(self.root)

    def test_changed_payload_fails(self):
        with (self.root / "orders.csv").open("a") as target:
            target.write("tampered\n")
        with self.assertRaisesRegex(ValueError, "校验和"):
            verify_handoff(self.root)

    def test_changed_count_fails(self):
        path = self.root / "manifest.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["files"][0]["rows"] += 1
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "行数"):
            verify_handoff(self.root)

    def test_wrong_version_fails(self):
        path = self.root / "manifest.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["contract_version"] = "other"
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "版本"):
            verify_handoff(self.root)

    def test_missing_table_fails(self):
        path = self.root / "manifest.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["files"] = [item for item in data["files"] if item.get("table") != "events"]
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "缺少"):
            verify_handoff(self.root)

    def test_traversal_rejected(self):
        path = self.root / "manifest.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["files"][0]["path"] = "../outside.csv"
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "越界"):
            verify_handoff(self.root)

    def test_csv_short_rows_are_not_implicit_nulls(self):
        path = self.root / "users.csv"
        lines = path.read_text(encoding="utf-8").splitlines()
        lines[1] = lines[1].rsplit(",", 1)[0]
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        manifest_path = self.root / "manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        next(item for item in manifest["files"] if item["path"] == path.name)["sha256"] = sha256_file(path)
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "行列数"):
            verify_handoff(self.root)


class NormalizationTests(unittest.TestCase):
    def test_money_uses_explicit_units(self):
        self.assertEqual(money_to_fen("10.01", "yuan"), 1001)
        self.assertEqual(money_to_fen("1001", "fen"), 1001)
        with self.assertRaises(ValueError):
            money_to_fen("10.01", "fen")

    def test_money_rejects_invalid_values(self):
        for value in (None, "", "NaN", "Infinity", "-1", "abc"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                money_to_fen(value)

    def test_half_up_rounding(self):
        self.assertEqual(expected_amount_fen("125", "0.1"), 13)
        self.assertEqual(expected_amount_fen("100", "1.005"), 101)

    def test_time_formats(self):
        expected = "2026-09-01T08:00:00+08:00"
        for value in (expected, "2026/09/01 08:00:00", "2026-09-01T00:00:00Z"):
            self.assertEqual(normalize_timestamp(value), expected)

    def test_epoch_seconds_and_milliseconds(self):
        self.assertEqual(normalize_timestamp("1000000000"), normalize_timestamp("1000000000000"))

    def test_missing_and_invalid_time(self):
        self.assertIsNone(normalize_timestamp(None))
        with self.assertRaises(ValueError):
            normalize_timestamp("2026-02-30T08:00:00+08:00")

    def test_active_orders_are_not_deleted_for_missing_end(self):
        tables, _ = fixture_records()
        for row in tables["orders"]:
            if row["status"] in {"reserved", "charging", "cancelled"}:
                validate_order_times(row)

    def test_completed_requires_end(self):
        tables, _ = fixture_records()
        with self.assertRaisesRegex(ValueError, "结束时间"):
            validate_order_times(next(row for row in tables["orders"] if row["_row_id"] == "o5"))

    def test_reversed_time_fails(self):
        tables, _ = fixture_records()
        with self.assertRaisesRegex(ValueError, "倒置"):
            validate_order_times(next(row for row in tables["orders"] if row["_row_id"] == "o7"))

    def test_text_nfkc(self):
        self.assertEqual(normalize_text("  Ａ测试站  "), "A测试站")


class MetricTests(unittest.TestCase):
    def test_false_positive_does_not_inflate_recall(self):
        def item(row):
            return {"rule": "Q1", "table": "orders", "row_id": row}
        result = score_findings([item("a"), item("b")], [item("a"), item("c"), item("a")])["Q1"]
        self.assertEqual(result, {"truePositive": 1, "falsePositive": 1, "falseNegative": 1, "recall": .5, "precision": .5})

    def test_no_denominator_is_not_perfect_score(self):
        finding = {"rule": "Q1", "table": "orders", "row_id": "a"}
        result = score_findings([], [finding])["Q1"]
        self.assertIsNone(result["recall"])
        self.assertEqual(result["precision"], 0)

    def test_rule_overlap_is_separate_findings(self):
        findings = [{"rule": rule, "table": "orders", "row_id": "a"} for rule in ("Q1", "Q5")]
        result = score_findings(findings, findings)
        self.assertEqual(sum(item["truePositive"] for item in result.values()), 2)


class ConfigurationTests(unittest.TestCase):
    def test_xml_values_escaped(self):
        root = ET.fromstring(xml_config({"test.path": "/a&b", "test.count": 1}))
        self.assertEqual(root.find("property/value").text, "/a&b")

    def test_same_config_can_be_reused(self):
        with tempfile.TemporaryDirectory(prefix="ev-part2-config-") as directory:
            path = Path(directory) / "test.xml"
            write_new_or_same(path, "content")
            write_new_or_same(path, "content")
            self.assertEqual(path.read_text(encoding="utf-8"), "content")

    def test_different_config_not_overwritten(self):
        with tempfile.TemporaryDirectory(prefix="ev-part2-config-") as directory:
            path = Path(directory) / "test.xml"
            write_new_or_same(path, "original")
            with self.assertRaises(RuntimeError):
                write_new_or_same(path, "new")
            self.assertEqual(path.read_text(encoding="utf-8"), "original")


if __name__ == "__main__":
    unittest.main()
