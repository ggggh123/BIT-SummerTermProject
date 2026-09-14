import json
import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "data_generator"))


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
            self.assertTrue(all(item["sha256"] for item in saved_manifest["tables"].values()))

            injection_log = json.loads(injection_path.read_text(encoding="utf-8"))
            self.assertEqual(injection_log["seed"], 20260914)
            self.assertEqual(len(injection_log["issues"]), 10)
            self.assertEqual(
                {issue["id"] for issue in injection_log["issues"]},
                {"Q1", "Q2", "Q3", "Q4", "Q5", "Q6", "Q7", "Q8", "Q9", "Q10"},
            )


if __name__ == "__main__":
    unittest.main()
