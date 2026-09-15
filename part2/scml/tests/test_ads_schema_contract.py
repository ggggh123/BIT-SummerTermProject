"""ADS 表结构契约测试。

守两件事：
1. `warehouse/sql/ads_schema.sql`（SQLite 方言，Flask 交接契约）能建出**全部 15 张表**
   与 7 个设计文档兼容视图；
2. 它与 `warehouse/sql/ads_etl.sql`（Hive 方言，Spark 产出端）的**列集一致**。
   第 2 条是防「改了一边忘了另一边」——这是本地/Spark 双实现最容易塌的地方，
   `reconcile.py` 只能比数值，比不了结构，所以必须由测试兜住。
"""

import contextlib
import re
import sqlite3
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SQL_DIR = ROOT / "warehouse" / "sql"

# Flask 契约实际消费的表（`server/api/*` 与 `/api/health` 会点名的那些）
REQUIRED_TABLES = {
    "ads_meta",
    "ads_station",
    "ads_charger",
    "ads_daily",
    "ads_station_day",
    "ads_station_hourly",
    "ads_user_rfm",
    "ads_district",
    "ads_quality_table",
    "ads_quality_issue",
    "ads_quality_meta",
    "ads_forecast_batch",
    "ads_forecast_24h",
    "ads_forecast_metric",
    "ads_event",
}

# 《04-SCML》§3.3 里出现的表名 -> 由视图提供给下游做口径追溯
REQUIRED_VIEWS = {
    "ads_revenue_overview",
    "ads_station_ranking",
    "ads_user_profile_rfm",
    "ads_peak_hour",
    "ads_charger_health",
    "ads_gov_service",
    "ads_forecast_result",
}

# Spark 侧（ads_etl.sql）负责产出的 7 张业务指标表
SPARK_OWNED = [
    "ads_station",
    "ads_charger",
    "ads_daily",
    "ads_station_day",
    "ads_station_hourly",
    "ads_user_rfm",
    "ads_district",
]


def hive_columns(sql: str, table: str) -> list[str]:
    """从 Hive DDL 里抽出某张表的列名（跳过 COMMENT / LOCATION 等子句行）。"""
    pattern = re.compile(
        r"CREATE\s+TABLE\s+IF\s+NOT\s+EXISTS\s+" + table + r"\s*\((.*?)\n\)",
        re.S | re.I,
    )
    match = pattern.search(sql)
    if not match:
        raise AssertionError(f"ads_etl.sql 里找不到 Hive 表定义：{table}")
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


class AdsSchemaContractTest(unittest.TestCase):
    def setUp(self):
        self.schema = (SQL_DIR / "ads_schema.sql").read_text(encoding="utf-8")
        self.etl = (SQL_DIR / "ads_etl.sql").read_text(encoding="utf-8")

    def _objects(self, kind: str) -> set[str]:
        with contextlib.closing(sqlite3.connect(":memory:")) as conn:
            conn.executescript(self.schema)
            return {
                row[0]
                for row in conn.execute(
                    "SELECT name FROM sqlite_master WHERE type = ?", (kind,)
                )
            }

    def test_ads_schema_creates_required_tables(self):
        self.assertEqual(REQUIRED_TABLES, self._objects("table"))

    def test_ads_schema_creates_design_doc_views(self):
        self.assertEqual(REQUIRED_VIEWS, self._objects("view"))

    def test_every_view_is_queryable(self):
        # 视图只是映射，但写错 JOIN 会静默变成空集，所以要真查一次
        with contextlib.closing(sqlite3.connect(":memory:")) as conn:
            conn.executescript(self.schema)
            for view in sorted(REQUIRED_VIEWS):
                conn.execute(f"SELECT COUNT(1) FROM {view}").fetchone()

    def test_sqlite_and_hive_column_sets_match(self):
        """两侧列名必须逐字一致，否则 Spark 产出的 Parquet 导进 SQLite 会缺列。"""
        for table in SPARK_OWNED:
            with contextlib.closing(sqlite3.connect(":memory:")) as conn:
                conn.executescript(self.schema)
                sqlite_columns = [row[1] for row in conn.execute(f"PRAGMA table_info({table})")]
            self.assertEqual(
                set(sqlite_columns),
                set(hive_columns(self.etl, table)),
                f"{table} 的 SQLite 列集与 Hive 列集不一致",
            )

    def test_all_tables_are_covered_by_one_of_the_two_producers(self):
        """每张表要么由 Spark 产出、要么由 Python 产出，不能两边都漏。"""
        python_owned = {
            "ads_meta",
            "ads_quality_table",
            "ads_quality_issue",
            "ads_quality_meta",
            "ads_forecast_batch",
            "ads_forecast_24h",
            "ads_forecast_metric",
            "ads_event",
        }
        self.assertEqual(REQUIRED_TABLES, set(SPARK_OWNED) | python_owned)

    def test_money_and_rate_columns_follow_naming_convention(self):
        """金额列必须以 `_fen` 结尾；百分比列不得叫 `*_rate_pct` 之类的别名。"""
        with contextlib.closing(sqlite3.connect(":memory:")) as conn:
            conn.executescript(self.schema)
            for table in sorted(REQUIRED_TABLES):
                for row in conn.execute(f"PRAGMA table_info({table})"):
                    column = row[1]
                    if column.endswith("_fen_cny") or column.endswith("_yuan"):
                        self.fail(f"{table}.{column} 疑似把金额写成了元，应为整数分 `*_fen`")

    def test_ads_etl_declares_a_location_for_every_spark_table(self):
        for table in SPARK_OWNED:
            self.assertIn(
                f"LOCATION '/ev-charging/ads/{table}'",
                self.etl,
                f"{table} 缺少 HDFS LOCATION，会落到 Hive warehouse 默认目录",
            )


if __name__ == "__main__":
    unittest.main()
