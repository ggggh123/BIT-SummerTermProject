"""DWS 表结构契约测试（《04-SCML》§3.2 的四张汇总宽表）。

DWS 没有第二个消费方言（不像 ADS 还要给 SQLite），所以这里守的是：
表齐、列齐、字段类型与「金额整数分 / 时间字符串 / 百分比 0-100」的口径一致、
以及每张表都有显式 HDFS LOCATION。
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SQL_DIR = ROOT / "warehouse" / "sql"

# 设计文档 §3.2 逐字给出的核心字段，少一个都算契约破了
EXPECTED = {
    "dws_station_day": [
        "dt", "station_id", "order_cnt", "energy_kwh", "revenue_fen",
        "avg_utilization", "peak_hour", "fast_order_cnt", "slow_order_cnt", "fault_cnt",
    ],
    "dws_charger_day": [
        "dt", "charger_id", "order_cnt", "energy_kwh", "charge_duration_sec", "fault_flag",
    ],
    "dws_user_day": [
        "dt", "user_id", "order_cnt", "energy_kwh", "amount_fen", "active_flag",
    ],
    "dws_region_day": [
        "dt", "district", "station_cnt", "charger_cnt", "order_cnt",
        "energy_kwh", "revenue_fen", "served_user_cnt",
    ],
}

MONEY_COLUMNS = ["revenue_fen", "amount_fen"]


def hive_columns(sql: str, table: str) -> list[str]:
    pattern = re.compile(
        r"CREATE\s+TABLE\s+IF\s+NOT\s+EXISTS\s+" + table + r"\s*\((.*?)\n\)",
        re.S | re.I,
    )
    match = pattern.search(sql)
    if not match:
        raise AssertionError(f"dws_schema.sql 里找不到表定义：{table}")
    columns = []
    for line in match.group(1).splitlines():
        line = line.strip().rstrip(",")
        if not line or line.startswith("--"):
            continue
        name = line.split()[0]
        if name.upper() in {"PRIMARY", "CONSTRAINT", "UNIQUE", "FOREIGN"}:
            continue
        columns.append(name)
    return columns


class DwsSchemaContractTest(unittest.TestCase):
    def setUp(self):
        self.schema = (SQL_DIR / "dws_schema.sql").read_text(encoding="utf-8")
        self.etl = (SQL_DIR / "dws_etl.sql").read_text(encoding="utf-8")

    def test_expected_tables_and_columns(self):
        for table, columns in EXPECTED.items():
            self.assertEqual(
                columns, hive_columns(self.schema, table), f"{table} 的列与设计文档 §3.2 不一致"
            )

    def test_every_table_has_explicit_hdfs_location(self):
        for table in EXPECTED:
            self.assertIn(
                f"LOCATION '/ev-charging/dws/{table}'",
                self.schema,
                f"{table} 缺少 HDFS LOCATION",
            )

    def test_money_columns_are_bigint(self):
        """金额必须整数分且用 BIGINT：8 站 × 90 天营收合计已过 4 亿分，INT 会溢出。"""
        for table, columns in EXPECTED.items():
            for money in MONEY_COLUMNS:
                if money not in columns:
                    continue
                pattern = re.compile(
                    r"CREATE\s+TABLE\s+IF\s+NOT\s+EXISTS\s+" + table + r"\s*\((.*?)\n\)",
                    re.S | re.I,
                )
                body = pattern.search(self.schema).group(1)
                self.assertRegex(
                    body,
                    re.compile(rf"{money}\s+BIGINT", re.I),
                    f"{table}.{money} 应为 BIGINT",
                )

    def test_etl_writes_every_declared_table(self):
        for table in EXPECTED:
            self.assertIn(
                f"INSERT OVERWRITE TABLE ev_charging.{table}", self.etl,
                f"dws_etl.sql 没有灌 {table}",
            )

    def test_etl_only_reads_from_dwd_layer(self):
        """DWS 必须从 DWD 读，不许越过 DWD 直接读 ODS —— 否则清洗规则会被绕过。"""
        # 注释里会提到 `ods_telemetry`（用来解释故障为什么取自事件流），
        # 所以先把注释剥掉再查表名。
        code = "\n".join(
            line for line in self.etl.splitlines() if not line.strip().startswith("--")
        )
        self.assertNotIn("ods_", code, "dws_etl.sql 里出现了 ODS 表引用")

    def test_peak_hour_ties_break_on_earliest_hour(self):
        """峰值时段并列时必须取更早的整点，否则与本地实现逐行对不上。"""
        self.assertIn("ORDER BY busy_count DESC, hour ASC", " ".join(self.etl.split()))


if __name__ == "__main__":
    unittest.main()
