import csv
import json
import sys
import tempfile
import unittest
from datetime import datetime, timedelta
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "data_generator"))


def read_csv_partitions(handoff_dir: Path, table: str) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in sorted((handoff_dir / table).glob("dt=*/part-*.csv")):
        with path.open(newline="", encoding="utf-8") as handle:
            rows.extend(csv.DictReader(handle))
    return rows


def read_jsonl_partitions(handoff_dir: Path, table: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for path in sorted((handoff_dir / table).glob("dt=*/part-*.jsonl")):
        rows.extend(json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip())
    return rows


class GeneratorContractTest(unittest.TestCase):
    def test_generator_writes_ods_handoff_manifest_and_injection_log(self):
        from generator import GenerationConfig, generate_handoff

        config = GenerationConfig(
            seed=20260914,
            station_count=2,
            chargers_per_station=4,
            user_count=20,
            order_count=50,
            telemetry_count=120,
            history_days=2,
            event_count=30,
            start_date="2026-09-01",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            handoff_dir = Path(temp_dir) / "handoff" / "ods"
            manifest = generate_handoff(config, handoff_dir)

            self.assertEqual(manifest["seed"], 20260914)
            self.assertEqual(manifest["tables"]["ods_stations"]["rows"], 2)
            self.assertEqual(manifest["tables"]["ods_chargers"]["rows"], 8)
            self.assertEqual(manifest["tables"]["ods_users"]["rows"], 20)
            self.assertEqual(manifest["tables"]["ods_orders"]["rows"], 50)
            self.assertEqual(manifest["tables"]["ods_telemetry"]["rows"], 120)
            self.assertEqual(manifest["tables"]["ods_station_hourly"]["rows"], 96)
            self.assertEqual(manifest["tables"]["ods_events"]["rows"], 30)

            manifest_path = handoff_dir / "manifest.json"
            injection_path = handoff_dir / "injection_log.json"
            self.assertTrue(manifest_path.exists())
            self.assertTrue(injection_path.exists())

            saved_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(saved_manifest, manifest)
            self.assertEqual(saved_manifest["contractVersion"], "ods-dwd-v0.1")
            self.assertEqual(saved_manifest["kind"], "prl-test-fixture")
            self.assertTrue(all(item["sha256"] for item in saved_manifest["files"]))
            self.assertTrue(any(item["path"] == "injection_log.json" for item in saved_manifest["files"]))

            orders = read_csv_partitions(handoff_dir, "ods_orders")
            self.assertEqual(len(orders), 50)
            self.assertIn("_row_id", orders[0])
            self.assertNotIn("station_id", orders[0])

            events = read_jsonl_partitions(handoff_dir, "ods_events")
            self.assertEqual(len(events), 30)
            self.assertIn("_row_id", events[0])
            self.assertIn("event_type", events[0])

            injection_log = json.loads(injection_path.read_text(encoding="utf-8"))
            self.assertEqual(injection_log["seed"], 20260914)
            self.assertTrue(all("row_id" in issue for issue in injection_log["issues"]))
            self.assertEqual(
                {issue["rule"] for issue in injection_log["issues"]},
                {"Q1", "Q2", "Q3", "Q4", "Q5", "Q6", "Q7", "Q8", "Q9", "Q10"},
            )
            self.assertEqual(injection_log["totalInjected"], len(injection_log["issues"]))

            # every injected row must actually carry the dirty value it claims
            by_row_id = {row["_row_id"]: row for row in orders}
            missing_ended = [
                item for item in injection_log["issues"] if item["rule"] == "Q1" and item["table"] == "ods_orders"
            ]
            self.assertTrue(missing_ended)
            for item in missing_ended:
                self.assertEqual(by_row_id[item["row_id"]]["ended_at"], r"\N")

    def test_telemetry_stays_inside_history_window(self):
        from generator import GenerationConfig, generate_handoff

        config = GenerationConfig(
            seed=7,
            station_count=2,
            chargers_per_station=4,
            user_count=10,
            order_count=40,
            telemetry_count=500,
            history_days=5,
            event_count=20,
            start_date="2026-09-01",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            handoff_dir = Path(temp_dir) / "handoff" / "ods"
            generate_handoff(config, handoff_dir)

            rows = read_csv_partitions(handoff_dir, "ods_telemetry")
            self.assertEqual(len(rows), 500)
            base = datetime.fromisoformat("2026-09-01T00:00:00+08:00")
            limit = base + timedelta(days=config.history_days)

            # Q4 deliberately writes mixed formats, so only ISO rows are parsed here.
            iso_stamps = [row["recorded_at"] for row in rows if "T" in row["recorded_at"]]
            self.assertTrue(iso_stamps)
            stamps = [datetime.fromisoformat(value) for value in iso_stamps]
            self.assertGreaterEqual(min(stamps), base)
            self.assertLess(max(stamps), limit)

            unix_stamps = [row["recorded_at"] for row in rows if row["recorded_at"].isdigit()]
            self.assertTrue(unix_stamps, "Q4 should inject unix timestamp frames")
            for value in unix_stamps:
                moment = datetime.fromtimestamp(int(value), base.tzinfo)
                self.assertGreaterEqual(moment, base - timedelta(days=1))
                self.assertLess(moment, limit + timedelta(days=1))

            partitions = sorted(path.name for path in (handoff_dir / "ods_telemetry").glob("dt=*"))
            self.assertTrue(partitions)
            self.assertEqual(partitions[0], "dt=2026-09-01")
            self.assertLess(partitions[-1], f"dt=2026-09-0{1 + config.history_days}")

    def test_injection_covers_every_documented_table(self):
        from generator import PARTITION_COLUMNS, QUALITY_RULES, GenerationConfig, generate_handoff

        config = GenerationConfig(
            seed=20260914,
            station_count=8,
            chargers_per_station=6,
            user_count=400,
            order_count=3000,
            telemetry_count=4000,
            history_days=10,
            event_count=200,
            start_date="2026-09-01",
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            handoff_dir = Path(temp_dir) / "handoff" / "ods"
            generate_handoff(config, handoff_dir)
            injection_log = json.loads((handoff_dir / "injection_log.json").read_text(encoding="utf-8"))

            covered: dict[str, set[str]] = {}
            for issue in injection_log["issues"]:
                covered.setdefault(issue["rule"], set()).add(issue["table"])

            for rule, spec in QUALITY_RULES.items():
                self.assertEqual(covered.get(rule, set()), set(spec["tables"]), f"{rule} table coverage")
                self.assertGreater(injection_log["summary"][rule]["total"], 0)

            # rates: at full-ish scale every rule must inject more than a single token row
            for rule in ("Q1", "Q2", "Q4"):
                self.assertGreater(injection_log["summary"][rule]["total"], 3)

            manifest = json.loads((handoff_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertIn("ods_orders", PARTITION_COLUMNS)
            self.assertTrue(manifest["tables"]["ods_orders"]["partitions"])


if __name__ == "__main__":
    unittest.main()
