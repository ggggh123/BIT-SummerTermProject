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


class DemandShapeTest(unittest.TestCase):
    """守《04-SCML》§2.1 的「真实分布」要求。

    早期版本的负荷是 `randint(0, pile_count)`、订单开工时间在整窗口上均匀撒点，
    结果是 24 小时曲线是平的、`peak_hour` 在 0–23 均匀散落。这对下游是致命的：
    `load_kw` 是 #5 的预测目标，没有日内规律就没有可学的模式（只能拟合噪声）；
    大屏的热力图与 24h 曲线也会变成一片雪花。这几条断言就是为了防止退回那个状态。
    """

    @classmethod
    def setUpClass(cls):
        from generator import GenerationConfig, generate_handoff

        cls._temp = tempfile.TemporaryDirectory()
        cls.handoff = Path(cls._temp.name) / "ods"
        generate_handoff(
            GenerationConfig(
                seed=20260914,
                station_count=8,
                chargers_per_station=12,
                user_count=300,
                order_count=2000,
                telemetry_count=300,
                history_days=28,
                event_count=60,
                start_date="2026-06-17",
            ),
            cls.handoff,
        )
        cls.hourly = read_csv_partitions(cls.handoff, "ods_station_hourly")
        cls.orders = read_csv_partitions(cls.handoff, "ods_orders")

    @classmethod
    def tearDownClass(cls):
        cls._temp.cleanup()

    def _clean_hourly(self) -> dict[int, list[tuple[datetime, int, float]]]:
        rows = []
        for row in self.hourly:
            # Q5 会把个别 busy_count 抬到 pile_count 以上，形状统计只看正常行
            if int(row["busy_count"]) > int(row["pile_count"]):
                continue
            rows.append(
                (
                    datetime.fromisoformat(row["observed_at"]),
                    int(row["station_id"]),
                    float(row["load_kw"]),
                )
            )
        return rows

    def test_hourly_load_has_diurnal_double_peak(self):
        per_hour: dict[int, list[float]] = {}
        for moment, _station, load in self._clean_hourly():
            per_hour.setdefault(moment.hour, []).append(load)
        self.assertEqual(len(per_hour), 24, "每个整点都要有数据")
        means = {hour: sum(values) / len(values) for hour, values in per_hour.items()}

        overall = sum(means.values()) / 24
        top = max(means.values())
        peak_hours = {hour for hour, value in means.items() if value >= top * 0.92}
        self.assertTrue(
            peak_hours & {7, 8, 9, 17, 18, 19, 20},
            f"高峰时段 {sorted(peak_hours)} 不在早晚双峰区间内",
        )
        for hour in (2, 3, 4):
            self.assertLess(means[hour], overall * 0.55, f"{hour} 点不应该高")
        self.assertGreater(top / min(means.values()), 2.0, "峰谷比太小，等于没有日内形状")

    def test_orders_follow_the_same_diurnal_shape(self):
        per_hour: dict[int, int] = {}
        for row in self.orders:
            value = row.get("started_at")
            if not value or value == r"\N" or "T" not in value:
                continue
            hour = datetime.fromisoformat(value).hour
            per_hour[hour] = per_hour.get(hour, 0) + 1
        self.assertEqual(len(per_hour), 24)
        peak = max(per_hour, key=lambda hour: per_hour[hour])
        self.assertIn(peak, {7, 8, 9, 17, 18, 19, 20}, f"订单高峰落在 {peak} 点")
        night = sum(per_hour.get(hour, 0) for hour in (1, 2, 3, 4))
        evening = sum(per_hour.get(hour, 0) for hour in (17, 18, 19, 20))
        self.assertLess(night, evening * 0.5, "凌晨订单量应显著低于晚高峰")

    def test_station_scale_and_new_station_ramp(self):
        by_station: dict[int, list[tuple[str, float]]] = {}
        for moment, station, load in self._clean_hourly():
            by_station.setdefault(station, []).append((moment.date().isoformat(), load))
        days = sorted({day for values in by_station.values() for day, _ in values})

        # 1 号站最早投运、规模最大；末号站最晚投运、规模最小
        first_avg = sum(v for _, v in by_station[1]) / len(by_station[1])
        last_id = max(by_station)
        last_values = by_station[last_id]
        last_avg = sum(v for _, v in last_values) / len(last_values)
        self.assertGreater(first_avg, last_avg, "1 号站的平均负荷应高于最晚投运的站点")

        # 新站爬坡：末号站在窗口后段应高于前段
        early_cut = days[len(days) // 3]
        late_cut = days[-max(1, len(days) // 3)]
        early = [v for d, v in last_values if d <= early_cut]
        late = [v for d, v in last_values if d >= late_cut]
        self.assertTrue(early and late)
        self.assertGreater(
            sum(late) / len(late), sum(early) / len(early), "末号站应呈现利用率爬坡"
        )

    def test_weekend_morning_peak_is_flattened(self):
        weekday: dict[int, list[float]] = {}
        weekend: dict[int, list[float]] = {}
        for moment, _station, load in self._clean_hourly():
            bucket = weekend if moment.weekday() >= 5 else weekday
            bucket.setdefault(moment.hour, []).append(load)
        if len(weekend) < 24 or len(weekday) < 24:
            self.skipTest("样例窗口内没有完整周末，跳过")
        weekday_morning = sum(weekday[8]) / len(weekday[8])
        weekend_morning = sum(weekend[8]) / len(weekend[8])
        self.assertLess(weekend_morning, weekday_morning, "周末 08 点不应维持通勤早高峰")


if __name__ == "__main__":
    unittest.main()
