"""十类执行器的边界单测，不要求启动 Hadoop；不以注入标签驱动检测。"""
import json
import unittest
from pathlib import Path
from decimal import Decimal
from part2.common.contracts import load_contract
from part2.quality.record_rules import inspect_record, clean_text, parse_number
from part2.scripts.make_fixture import fixture_records


class RecordRulesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = load_contract()
        cls.policy = json.loads((Path(__file__).parents[1] / "contracts/quality-policy-v0.1.json").read_text(encoding="utf-8"))
        cls.tables, _ = fixture_records()

    def inspect(self, table, row=None, **changes):
        raw = dict(self.tables[table][0] if row is None else row, **changes)
        return inspect_record(table, raw, self.contract["tables"][table], self.policy)

    def rules(self, result, action=None):
        return {item["rule"] for item in result["_issues"] if action is None or item["action"] == action}

    def test_valid_records_have_no_direct_issues(self):
        for table in self.tables:
            with self.subTest(table=table):
                self.assertEqual(self.inspect(table)["_issues"], [])

    def test_open_orders_keep_missing_end(self):
        for state in ("reserved", "charging", "cancelled"):
            raw = next(row for row in self.tables["orders"] if row["status"] == state)
            self.assertNotIn("Q1", self.rules(self.inspect("orders", raw)))

    def test_completed_missing_end_is_not_filled(self):
        row = self.inspect("orders", ended_at=None)
        self.assertIn("Q1", self.rules(row, "reject"))
        self.assertIsNone(row["ended_at"])

    def test_optional_defaults_are_not_reported_as_missing_quality(self):
        row = self.inspect("users", nickname=None, avatar_path=None, balance_fen=None)
        self.assertEqual(row["nickname"], "未命名用户")
        self.assertEqual(row["avatar_path"], "")
        self.assertIsNone(row["balance_fen"])
        self.assertNotIn("Q1", self.rules(row, "fill"))
        self.assertIn("Q1", self.rules(row, "reject"))

    def test_optional_avatar_null_is_valid_not_q1(self):
        row = self.inspect("users", avatar_path=None)
        self.assertEqual(row["avatar_path"], "")
        self.assertNotIn("Q1", self.rules(row))

    def test_invalid_numeric_values_are_not_zero(self):
        for value in ("NaN", "Infinity", "-Infinity", "not-a-number", "1e9999"):
            with self.subTest(value=value):
                row = self.inspect("telemetry", power_kw=value)
                self.assertIn("Q3", self.rules(row, "reject"))
                self.assertIsNone(row["power_kw"])

    def test_precision_and_integer_overflow_rejected(self):
        for value, dtype in (("1.1", "long"), (str(2**63), "long"), ("0.0000001", "decimal(18,6)"), ("1000000000000", "decimal(18,6)")):
            with self.subTest(value=value), self.assertRaises((ValueError, ArithmeticError)):
                parse_number(value, dtype)

    def test_decimal_is_exact(self):
        self.assertEqual(parse_number("0.123456", "decimal(18,6)"), Decimal("0.123456"))

    def test_mixed_time_and_invalid_date_differ(self):
        good = self.inspect("orders", reserved_at="2026/08/01 08:00:00")
        bad = self.inspect("orders", reserved_at="2026-02-30T00:00:00+08:00")
        self.assertIn("Q4", self.rules(good, "repair"))
        self.assertIn("Q4", self.rules(bad, "reject"))

    def test_timezone_instant_is_compared_not_text(self):
        row = self.inspect("orders", reserved_at="2026-08-01T00:00:00Z")
        self.assertIn("Q4", self.rules(row, "repair"))
        self.assertNotIn("Q5", self.rules(row))

    def test_ancient_or_far_future_dates_are_quarantined(self):
        for value in ("0001-01-01T00:00:00+08:00", "9999-01-01T00:00:00+08:00"):
            self.assertIn("Q4", self.rules(self.inspect("orders", reserved_at=value), "reject"))

    def test_state_time_contradictions(self):
        for changes in ({"ended_at": "2026-08-01T08:01:00+08:00"}, {"status": "reserved"}, {"status": "charging"}):
            self.assertIn("Q5", self.rules(self.inspect("orders", **changes), "reject"))

    def test_raw_fractional_amount_waits_for_reference(self):
        row = self.inspect("orders", amount_fen="10.01")
        self.assertEqual(row["_amount_raw"], Decimal("10.01"))
        self.assertIsNone(row["amount_fen"])
        self.assertNotIn("Q6", self.rules(row))

    def test_negative_amount_is_rejected(self):
        self.assertIn("Q6", self.rules(self.inspect("orders", amount_fen="-1"), "reject"))

    def test_phone_and_enum(self):
        self.assertIn("Q8", self.rules(self.inspect("users", mobile="138000000X", status="deleted"), "reject"))
        self.assertNotIn("Q8", self.rules(self.inspect("users", status="frozen")))

    def test_coordinate_bounds(self):
        # 口径（2026-09-15 由 #2 决定）：越界坐标**仅置空该字段并保留站点记录**（repair），
        # 不整行隔离——stations 是维度表，整行剔除会让该站的订单/桩/遥测变成孤儿引用。
        row = self.inspect("stations", latitude="31.2")
        self.assertIn("Q9", self.rules(row, "repair"))
        self.assertNotIn("Q9", self.rules(row, "reject"))
        self.assertIsNone(row["latitude"], "越界坐标应被置空")
        # NaN 属非法数值，仍走 Q9 的 reject 路径（无法置空为有意义的值）
        self.assertIn("Q9", self.rules(self.inspect("stations", latitude="NaN"), "reject"))

    def test_hourly_logic_and_physical_limits(self):
        row = self.inspect("station_hourly", busy_count="3", load_kw="200", temperature_c="100")
        self.assertEqual(self.rules(row, "reject"), {"Q3", "Q5"})

    def test_hourly_must_be_on_hour(self):
        self.assertIn("Q5", self.rules(self.inspect("station_hourly", observed_at="2026-09-01T07:30:00+08:00"), "reject"))

    def test_text_keeps_normal_chinese_punctuation(self):
        self.assertEqual(clean_text("站点，订单（正常）"), "站点，订单（正常）")
        self.assertEqual(clean_text("　Ａ站１\u200b  "), "A站1")

    def test_text_repair_and_unrecoverable_replacement(self):
        self.assertIn("Q10", self.rules(self.inspect("stations", name="　Ａ测试站\u200b "), "repair"))
        self.assertIn("Q10", self.rules(self.inspect("stations", name="坏\ufffd站"), "reject"))

    def test_normalized_empty_key_text_is_rejected(self):
        self.assertIn("Q1", self.rules(self.inspect("stations", name="\u200b"), "reject"))

    def test_raw_evidence_is_preserved(self):
        raw = dict(self.tables["stations"][0], name="  Ａ测试站  ")
        row = self.inspect("stations", raw)
        self.assertEqual(json.loads(row["_raw_json"]), raw)
        self.assertEqual(row["name"], "A测试站")
