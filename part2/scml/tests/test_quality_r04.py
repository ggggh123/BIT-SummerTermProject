"""R04「时间格式混杂」的契约口径测试（ADS 侧独立复算）。

契约 `part2/contracts/ods-dwd-v0.1.json` 的 Q4 policy 要求
「统一时区；**区分可解析的非标准格式与不可解析时间**」——两类都要统计。

本测试锁定 `build_local.py` / `_ads_extras.py` 的计数口径：

* 可解析但非标准（`yyyy/MM/dd HH:mm:ss`、Unix 秒）**要计数**；
* 不可解析**要计数**；
* 标准 ISO 8601 不计数；
* **清洗宽容度不变**：可解析的非标准行只计数、不剔除（避免丢数据）。
"""

import csv
import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JOBS = ROOT / "warehouse" / "jobs"

sys.path.insert(0, str(JOBS))

import _ads_extras as extras  # noqa: E402
from _lib import is_nonstandard_time, parse_timestamp  # noqa: E402
from build_local import load_orders, load_station_hourly, scan_telemetry  # noqa: E402

ISO = "2026-07-21T08:45:00+08:00"


def write_csv(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


class NonstandardTimeUnitTest(unittest.TestCase):
    """`is_nonstandard_time` 的分类边界。"""

    def test_classification(self):
        cases = {
            ISO: False,                        # 标准 ISO 8601 +08:00
            "2026/07/21 12:00:00": True,       # 斜杠格式（可解析）
            "1784611800": True,                # Unix 秒（可解析）
            "2026-07-21 08:45:00": True,       # 空格分隔（可解析，非 +08:00 形式）
            "not-a-time": True,                # 不可解析
            "": False,
            "   ": False,
            None: False,
            "\\N": False,                      # ODS 的 NULL 标记
        }
        for value, expected in cases.items():
            with self.subTest(value=value):
                self.assertIs(is_nonstandard_time(value), expected)

    def test_reuses_preparsed_value(self):
        moment = parse_timestamp("2026/07/21 12:00:00")
        self.assertTrue(is_nonstandard_time("2026/07/21 12:00:00", moment))
        self.assertFalse(is_nonstandard_time(ISO, parse_timestamp(ISO)))


class R04CountingTest(unittest.TestCase):
    """构造最小 ODS，验证三个计数点的口径与剔除行为。"""

    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.ods = Path(self._temp.name) / "ods"

    def tearDown(self):
        self._temp.cleanup()

    def test_scan_telemetry(self):
        rows = [
            {"id": "1", "charger_id": "10", "recorded_at": ISO,
             "power_kw": "7.2", "energy_increment_kwh": "1.0", "event_type": "charging"},
            {"id": "2", "charger_id": "10", "recorded_at": "2026/07/21 09:00:00",
             "power_kw": "7.2", "energy_increment_kwh": "1.0", "event_type": "charging"},
            {"id": "3", "charger_id": "10", "recorded_at": "1784611800",
             "power_kw": "7.2", "energy_increment_kwh": "1.0", "event_type": "charging"},
            {"id": "4", "charger_id": "10", "recorded_at": "not-a-time",
             "power_kw": "7.2", "energy_increment_kwh": "1.0", "event_type": "charging"},
        ]
        write_csv(self.ods / "ods_telemetry" / "dt=2026-07-21" / "part-00000.csv", rows)
        detected = Counter()
        total, removed = scan_telemetry(self.ods, {10: 120}, detected)
        self.assertEqual(total, 4)
        self.assertEqual(detected["R04"], 3, "斜杠 + Unix 秒 + 不可解析都应计数")
        self.assertEqual(removed, 1, "仅不可解析行被剔除；可解析的非标准行保留")

    def test_load_station_hourly(self):
        rows = [
            {"station_id": "1", "observed_at": "2026/07/21 12:00:00", "pile_count": "3",
             "rated_power_kw": "120", "temperature_c": "25", "is_holiday": "0",
             "busy_count": "1", "load_kw": "7.2"},
            {"station_id": "1", "observed_at": "2026-07-21T13:00:00+08:00", "pile_count": "3",
             "rated_power_kw": "120", "temperature_c": "25", "is_holiday": "0",
             "busy_count": "1", "load_kw": "7.2"},
        ]
        write_csv(self.ods / "ods_station_hourly" / "dt=2026-07-21" / "part-00000.csv", rows)
        detected = Counter()
        kept = load_station_hourly(self.ods, {1: {"id": 1}}, detected)
        self.assertEqual(detected["R04"], 1, "斜杠格式应计数且不被剔除")
        self.assertEqual(len(kept), 2, "两条都应保留（可解析）")

    def test_load_orders(self):
        rows = [{
            "_row_id": "orders-000000001", "id": "1", "user_id": "5", "charger_id": "10",
            "status": "completed", "reserved_at": "2026/07/21 12:00:00",
            "started_at": "2026-07-21T12:05:00+08:00", "ended_at": "2026-07-21T13:00:00+08:00",
            "energy_kwh": "10.0", "amount_fen": "1000",
        }]
        write_csv(self.ods / "ods_orders" / "dt=2026-07-21" / "part-00000.csv", rows)
        detected = Counter()
        load_orders(
            self.ods,
            {5: {"id": 5}},
            {1: {"id": 1, "price_fen_per_kwh": 100}},
            {10: 1},
            {10: {"id": 10, "station_id": 1, "power_kw": 120.0, "type": "fast", "status": "idle"}},
            detected,
        )
        self.assertEqual(detected["R04"], 1, "reserved_at 的斜杠格式应计数（按行计一次）")

    def test_build_events(self):
        rows = [
            {"id": "1", "event_type": "charger_fault", "entity_type": "charger",
             "entity_id": "10", "message": "故障", "created_at": "2026/07/21 12:00:00"},
            {"id": "2", "event_type": "order_completed", "entity_type": "order",
             "entity_id": "1", "message": "完成", "created_at": ISO},
        ]
        write_csv(self.ods / "ods_events" / "dt=2026-07-21" / "part-00000.csv", rows)
        detected = Counter()
        extras.build_events(self.ods, {}, {}, {}, detected)
        self.assertEqual(detected["R04"], 1, "事件时间戳非标准应计数")


if __name__ == "__main__":
    unittest.main()
