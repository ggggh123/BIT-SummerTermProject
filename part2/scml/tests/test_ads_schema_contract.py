import sqlite3
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AdsSchemaContractTest(unittest.TestCase):
    def test_ads_schema_creates_required_tables(self):
        schema_path = ROOT / "warehouse" / "sql" / "ads_schema.sql"
        with sqlite3.connect(":memory:") as conn:
            conn.executescript(schema_path.read_text(encoding="utf-8"))
            names = {
                row[0]
                for row in conn.execute(
                    "SELECT name FROM sqlite_master WHERE type='table'"
                )
            }

        self.assertEqual(
            {
                "ads_revenue_overview",
                "ads_station_ranking",
                "ads_user_profile_rfm",
                "ads_peak_hour",
                "ads_charger_health",
                "ads_gov_service",
                "ads_metadata",
            },
            names,
        )


if __name__ == "__main__":
    unittest.main()
