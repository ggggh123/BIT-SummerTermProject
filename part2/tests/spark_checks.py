"""显式运行的本地 Spark 回归（不冒充 YARN 集成证据）。

执行：ev-part2 python -m unittest part2.tests.spark_checks -v
"""
import copy
import csv
import json
import os
import tempfile
import time
import unittest
from pathlib import Path
from pyspark.sql import SparkSession, functions as F
from part2.common.contracts import load_contract
from part2.common.spark_io import read_csv_strings
from part2.scripts.make_fixture import fixture_records, write_fixture
from part2.quality.rules import raw_schema, normalized_schema, run_rules, finding_frame, dwd_frames, accepted, rejected, deduplicate, foreign_key, money_reference
from part2.quality.record_rules import inspect_record
from part2.quality.metrics import score_findings
from part2.quality.reporting import score_distributed, build_reports
from part2.clean.assertions import assert_dwd
from part2.clean.scml_dwd import scml_frames, write_scml, read_scml, assert_scml, lineage_frame
from part2.tests.test_scml_handoff import write_scml_fixture
from part2.common.handoff import verify_handoff
from part2.scripts.spark_quality_clean import load_raw, load_expected


class SparkPipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ["TZ"] = "Asia/Shanghai"
        time.tzset()
        cls.temp = tempfile.TemporaryDirectory(prefix="ev-part2-spark-tests-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        cls.contract = load_contract()
        cls.policy = json.loads((Path(__file__).parents[1] / "contracts/quality-policy-v0.1.json").read_text())
        cls.tables, cls.truth = fixture_records()
        cls.spark = SparkSession.builder.master("local[2]").appName("PRL-local-regression-not-yarn").config("spark.sql.shuffle.partitions", "2").config("spark.ui.enabled", "false").config("spark.sql.ansi.enabled", "true").config("spark.sql.session.timeZone", "Asia/Shanghai").config("spark.sql.warehouse.dir", (cls.root / "warehouse").as_uri()).getOrCreate()
        cls.addClassCleanup(cls.spark.stop)
        cls.spark.sparkContext.setLogLevel("ERROR")
        cls.spark.sparkContext.setCheckpointDir((cls.root / "checkpoints").as_uri())
        write_fixture(cls.root / "input")
        cls.raw = {}
        for table, spec in cls.contract["tables"].items():
            path = (cls.root / "input" / f"{table}.{spec['format']}").as_uri()
            cls.raw[table] = read_csv_strings(cls.spark, path, raw_schema(spec), cls.contract["csv_null"]) if spec["format"] == "csv" else cls.spark.read.schema(raw_schema(spec)).json(path)
        cls.processed = run_rules(cls.spark, cls.raw, cls.contract, cls.policy)
        cls.findings = finding_frame(cls.processed).cache()
        cls.actual = [row.asDict(recursive=True) for row in cls.findings.collect()]
        cls.dwd = dwd_frames(cls.processed, cls.contract, "local-test")
        cls.orders = {row.order_id: row.asDict() for row in cls.dwd["dwd_order_detail"].collect()}

    def normalized(self, table, records):
        spec = self.contract["tables"][table]
        return self.spark.createDataFrame([inspect_record(table, row, spec, self.policy) for row in records], normalized_schema(table, spec))

    def test_fixture_exact_ten_rules(self):
        result = score_findings(self.truth, self.actual)
        self.assertEqual(len(self.actual), 10)
        for row in result.values():
            self.assertEqual((row["truePositive"], row["falsePositive"], row["falseNegative"]), (1, 0, 0))

    def test_dwd_counts_and_constraints(self):
        checks = assert_dwd(self.dwd, self.contract, self.policy)
        self.assertEqual({item["table"]: item["rows"] for item in checks}, {"dim_users": 2, "dim_stations": 2, "dim_chargers": 3, "dwd_order_detail": 6, "dwd_telemetry_detail": 1, "dwd_station_hourly": 2})

    def test_legal_open_orders_remain(self):
        self.assertEqual({self.orders[index]["status"] for index in (2, 3, 4)}, {"reserved", "charging", "cancelled"})
        self.assertIsNone(self.orders[2]["ended_at"])
        self.assertIsNone(self.orders[3]["ended_at"])

    def test_amount_and_time_repaired(self):
        self.assertEqual(self.orders[8]["amount_fen"], 1000)
        self.assertEqual(self.orders[6]["reserved_at"].isoformat(), "2026-08-06T08:00:00")
        self.assertEqual(self.orders[1]["station_id"], 1)

    def test_csv_empty_null_quotes_and_multiline_distinct(self):
        spec = self.contract["tables"]["users"]
        path = self.root / "csv-edge.csv"
        records = [dict(self.tables["users"][0], _row_id="empty", avatar_path="", nickname='含"引号",逗号\n换行'), dict(self.tables["users"][0], _row_id="null", avatar_path="\\N")]
        with path.open("w", newline="", encoding="utf-8") as target:
            writer = csv.DictWriter(target, fieldnames=raw_schema(spec).fieldNames())
            writer.writeheader()
            writer.writerows(records)
        actual = {row._row_id: row for row in read_csv_strings(self.spark, path.as_uri(), raw_schema(spec), "\\N").collect()}
        self.assertEqual(actual["empty"].avatar_path, "")
        self.assertIsNone(actual["null"].avatar_path)
        self.assertEqual(actual["empty"].nickname, records[0]["nickname"])

    def test_duplicates_deterministic_and_conflicts_quarantined(self):
        base = self.tables["orders"][0]
        records = [dict(base, _row_id="b", id="101"), dict(base, _row_id="a", id="101"), dict(base, _row_id="c", id="102"), dict(base, _row_id="d", id="102", amount_fen="1100"), dict(base, _row_id="n1", id=None), dict(base, _row_id="n2", id=None)]
        rows = deduplicate(self.normalized("orders", records).repartition(2), self.contract["tables"]["orders"])
        self.assertEqual([row._row_id for row in accepted(rows).collect()], ["a"])
        q2 = {row._row_id for row in rows.where(F.exists("_issues", lambda item: item["rule"] == "Q2")).collect()}
        self.assertEqual(q2, {"b", "c", "d"})

    def test_orphan_and_dimension_removal_have_separate_origin(self):
        base = self.tables["orders"][0]
        frame = self.normalized("orders", [dict(base, _row_id="ok", user_id="1"), dict(base, _row_id="cascade", user_id="3"), dict(base, _row_id="orphan", user_id="999")])
        frame = foreign_key(frame, "user_id", self.normalized("users", self.tables["users"]), accepted(self.processed["users"]))
        actual = {row._row_id: [item.origin for item in row._issues if item.rule == "Q7"] for row in frame.collect()}
        self.assertEqual(actual, {"ok": [], "cascade": ["cascade"], "orphan": ["direct"]})

    def test_money_reference_no_size_guessing(self):
        base = self.tables["orders"][0]
        rows = [dict(base, _row_id="unit", amount_fen="10.00"), dict(base, _row_id="fraction_unit", energy_kwh="0.01", amount_fen="0.01"), dict(base, _row_id="wrong", amount_fen="99"), dict(base, _row_id="within", amount_fen="1005"), dict(base, _row_id="zero", energy_kwh="0", amount_fen="0")]
        frame = self.normalized("orders", rows).withColumn("_price_fen_per_kwh", F.lit(100).cast("long"))
        result = money_reference(frame, self.policy)
        good = {row._row_id: row.amount_fen for row in accepted(result).collect()}
        self.assertEqual(good, {"unit": 1000, "fraction_unit": 1, "within": 1005, "zero": 0})
        self.assertEqual([row._row_id for row in result.where(rejected()).collect()], ["wrong"])

    def test_units_only_policy_does_not_invent_historical_price(self):
        policy = copy.deepcopy(self.policy)
        policy["money"]["reference"] = "units_only"
        row = dict(self.tables["orders"][0], amount_fen="10")
        frame = self.normalized("orders", [row]).withColumn("_price_fen_per_kwh", F.lit(100).cast("long"))
        result = accepted(money_reference(frame, policy)).first()
        self.assertEqual(result.amount_fen, 10)
        self.assertEqual(result._issues, [])

    def test_overlapping_issues_count_one_quarantined_row(self):
        row = dict(self.tables["orders"][0], ended_at=None, energy_kwh="-1", status="alien")
        frame = self.normalized("orders", [row])
        self.assertGreater(len(frame.first()._issues), 1)
        self.assertEqual(frame.where(rejected()).count(), 1)

    def test_empty_truth_cannot_supply_detection(self):
        expected = self.spark.createDataFrame([], "rule string, table string, row_id string")
        score = score_distributed(self.spark, expected, self.findings, [f"Q{i}" for i in range(1, 11)])
        self.assertEqual(sum(item["falsePositive"] for item in score.values()), 10)
        self.assertTrue(all(item["recall"] is None for item in score.values()))

    def test_assertion_rejects_schema_drift(self):
        bad = dict(self.dwd)
        bad["dim_users"] = bad["dim_users"].withColumn("balance_fen", F.col("balance_fen").cast("double"))
        with self.assertRaisesRegex(ValueError, "类型／字段"):
            assert_dwd(bad, self.contract, self.policy)

    def test_assertion_rejects_post_write_corruption(self):
        bad = dict(self.dwd)
        bad["dwd_order_detail"] = bad["dwd_order_detail"].withColumn("station_id", F.lit(999).cast("long"))
        with self.assertRaisesRegex(ValueError, "断言失败"):
            assert_dwd(bad, self.contract, self.policy)

    def test_reports_reconcile_removal_and_repairs(self):
        expected = self.spark.createDataFrame(self.truth, "rule string, table string, row_id string")
        _, cleaning = build_reports(self.raw, self.processed, self.findings, self.contract, self.policy, {"run_id": "local-test"}, expected, [])
        ledger = cleaning["order_amount_reconciliation"]
        self.assertTrue(ledger["balanced"])
        self.assertEqual(str(ledger["raw_declared_fen_numeric_sum"]), "6010.000000")
        self.assertEqual(str(ledger["quarantined_raw_numeric_sum"]), "4000.000000")
        self.assertEqual(str(ledger["retained_repair_delta_fen"]), "990.000000")
        self.assertEqual(str(ledger["output_amount_fen"]), "3000.000000")
        self.assertEqual(sum(item["quarantined"] for item in cleaning["tables"].values()), 7)
        self.assertEqual(sum(item["repaired_retained_rows"] for item in cleaning["tables"].values()), 3)

    def test_scml_native_reader_maps_headers_json_numbers_and_labels(self):
        root = self.root / "native-scml"
        root.mkdir()
        write_scml_fixture(root)
        manifest = verify_handoff(root)
        raw = load_raw(self.spark, root.as_uri(), manifest, self.contract)
        station = raw["stations"].where(F.col("id") == "1").first()
        self.assertEqual(station.district, self.tables["stations"][0]["district"])
        self.assertEqual(station.latitude, self.tables["stations"][0]["latitude"])
        self.assertEqual(raw["events"].first().id, "1")
        labels = [row.asDict() for row in load_expected(self.spark, root.as_uri(), manifest).collect()]
        self.assertEqual(sorted(labels, key=str), sorted(self.truth, key=str))
        for frame in raw.values():
            frame.unpersist()

    def test_scml_seven_tables_partitioned_readback(self):
        frames = scml_frames(self.processed)
        root = (self.root / "scml-dwd").as_uri()
        write_scml(frames, root)
        readback = read_scml(self.spark, root)
        expected = {name: frame.count() for name, frame in self.dwd.items()}
        expected["dwd_event"] = 1
        checks = assert_scml(readback, expected)
        self.assertEqual(len(checks), 7)
        self.assertEqual(readback["dim_users"].schema["user_id"].dataType.simpleString(), "int")
        order = readback["dwd_order_detail"].where(F.col("order_id") == 1).first()
        self.assertTrue(order.started_at.endswith("+08:00"))
        self.assertEqual(order.dt, order.started_at[:10])
        opened = readback["dwd_order_detail"].where(F.col("order_id") == 2).first()
        self.assertIsNone(opened.dt)
        self.assertIsNone(opened.hour)
        self.assertIsNone(opened.wait_min)
        self.assertEqual(lineage_frame(self.processed, "lineage-test").count(), sum(expected.values()))

    def test_scml_does_not_truncate_fractional_rated_power(self):
        bad = dict(self.processed)
        bad["chargers"] = bad["chargers"].withColumn("power_kw", F.lit(7.5))
        with self.assertRaisesRegex(ValueError, "拒绝截断"):
            scml_frames(bad)

    def test_scml_assertion_rejects_field_reordering(self):
        frames = scml_frames(self.processed)
        bad = dict(frames)
        columns = bad["dim_users"].columns
        bad["dim_users"] = bad["dim_users"].select(*reversed(columns))
        with self.assertRaisesRegex(ValueError, "类型／字段"):
            assert_scml(bad)

    def test_scml_empty_partitioned_table_is_readable(self):
        frames = scml_frames(self.processed)
        frames["dwd_telemetry_detail"] = frames["dwd_telemetry_detail"].where(F.lit(False))
        root = (self.root / "scml-empty").as_uri()
        write_scml(frames, root)
        readback = read_scml(self.spark, root)
        self.assertEqual(readback["dwd_telemetry_detail"].count(), 0)
        self.assertEqual(readback["dwd_telemetry_detail"].schema["dt"].dataType.simpleString(), "string")


if __name__ == "__main__":
    unittest.main()
