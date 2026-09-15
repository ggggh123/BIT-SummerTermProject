#!/usr/bin/env python3
"""三层对账：ODS → DWS → ADS 逐层复现，误差 0（《04-SCML》§3.4 验收硬指标）。

    python warehouse/jobs/reconcile.py \\
        --ods handoff/ods \\
        --dws handoff/dws \\
        --ads handoff/ads/ads.db \\
        [--dwd handoff/dwd] [--require-dwd]

### 它检查什么

| 组 | 内容 | 强度 |
|---|---|---|
| A 内部自洽 | ADS 各表之间的守恒关系（KPI = 逐日汇总、五状态之和 = 桩数、分层用户数之和 = 有订单用户数…） | 只依赖 ADS 自身，**永远能跑** |
| B DWS↔ADS | 同一指标在两层的值逐行/逐合计相等 | 只依赖 DWS+ADS，**永远能跑** |
| C ODS↔ADS | **独立重算**：从 ODS 重新跑一遍清洗聚合，比对 `ads_meta` 的总营收/总订单/总电量 | 只依赖 ODS+ADS，**永远能跑** |
| D DWD↔DWS | `SUM(dwd_order_detail.amount_fen)` = `SUM(dws_station_day.revenue_fen)`，误差 **0** | 需 #3 的 `handoff/dwd`，缺则 SKIP |

A/B/C 三组不依赖任何人：**只要 ODS 在，这份对账就能跑**，这是刻意的 ——
不能让「等 DWD」变成「链路没法验收」。

### 退出码

`0` = 全绿（允许 SKIP，除非加了 `--require-dwd`）；`1` = 有 FAIL。
CI 与演示脚本据此判成败，不要只看输出文本。
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")

sys.path.insert(0, str(Path(__file__).resolve().parent))

from _lib import CN_TZ, iter_rows  # noqa: E402
from build_local import (  # noqa: E402  用同一套清洗逻辑做「独立重算」
    load_chargers,
    load_orders,
    load_stations,
    load_users,
)

# 金额/行数类必须零误差；浮点派生量给一个与两侧舍入位数相称的容差
MONEY_TOL = 0
COUNT_TOL = 0
KWH_TOL = 0.05      # 电量保留 3 位小数，两侧求和路径不同，留 0.05 kWh
RATE_TOL = 0.05     # 百分比保留 1 位


class Report:
    def __init__(self) -> None:
        self.rows: list[tuple[str, str, str]] = []

    def check(self, group: str, name: str, ok: bool, detail: str = "") -> None:
        self.rows.append((group, name, "PASS" if ok else "FAIL", detail))

    def skip(self, group: str, name: str, detail: str) -> None:
        self.rows.append((group, name, "SKIP", detail))

    def close(
        self, group: str, name: str, left: float, right: float, tol: float, unit: str = ""
    ) -> None:
        ok = abs(left - right) <= tol
        self.check(
            group, name, ok,
            f"{left}{unit} vs {right}{unit}（差 {abs(left - right):.6g}，容差 {tol}）"
            if not ok
            else f"{left}{unit} == {right}{unit}",
        )

    @property
    def failed(self) -> int:
        return sum(1 for _, _, status, _ in self.rows if status == "FAIL")

    def render(self) -> str:
        width = max((len(name) for _, name, _, _ in self.rows), default=10)
        group = ""
        lines = []
        for row_group, name, status, detail in self.rows:
            if row_group != group:
                group = row_group
                lines.append(f"\n── {group} " + "─" * max(0, 66 - len(group)))
            mark = {"PASS": "✓", "FAIL": "✗", "SKIP": "-"}[status]
            suffix = f"  {detail}" if status != "PASS" else ""
            lines.append(f"  {mark} {name.ljust(width)}  {suffix}")
        return "\n".join(lines)


# --------------------------------------------------------------------------- #
# 读取器
# --------------------------------------------------------------------------- #
def read_dws(dws_dir: Path) -> dict[str, list[dict]]:
    """读 `handoff/dws/<table>/part-*.csv`。"""
    tables: dict[str, list[dict]] = {}
    for name in ("dws_station_day", "dws_charger_day", "dws_user_day", "dws_region_day"):
        table_dir = dws_dir / name
        if not table_dir.is_dir():
            continue
        rows = [row for part in sorted(table_dir.glob("part-*"))
                if part.suffix == ".csv"
                for row in _csv_rows(part)]
        tables[name] = rows
    return tables


def _csv_rows(path: Path) -> list[dict]:
    import csv

    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def read_ads(db_path: Path) -> sqlite3.Connection:
    # resolve()：`as_uri()` 只接受绝对路径，而 Git Bash 传进来的 `/tmp/x` 在 Windows
    # 上会被判成「无盘符的相对路径」。先规范化再拼 URI。
    connection = sqlite3.connect(f"{db_path.resolve().as_uri()}?mode=ro", uri=True)
    connection.row_factory = sqlite3.Row
    return connection


def read_dwd(path: Path, table: str) -> list[dict] | None:
    """读 DWD 表：优先 CSV，其次 Parquet（需本机有 pyspark）。都没有返回 None。"""
    table_dir = path / table
    if not table_dir.is_dir():
        return None
    csvs = sorted(p for p in table_dir.rglob("*.csv") if p.is_file())
    if csvs:
        return [row for part in csvs for row in _csv_rows(part)]
    parquet_parts = [p for p in table_dir.rglob("*.parquet") if p.is_file()]
    if not parquet_parts:
        return None
    try:
        from pyspark.sql import SparkSession
    except ImportError:
        return None
    spark = SparkSession.builder.master("local[1]").appName("ev-scml-reconcile").getOrCreate()
    spark.sparkContext.setLogLevel("ERROR")
    try:
        return [row.asDict(recursive=True) for row in spark.read.parquet(str(table_dir)).collect()]
    finally:
        spark.stop()


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #
def main() -> int:
    parser = argparse.ArgumentParser(description="ODS/DWS/ADS 三层对账")
    parser.add_argument("--ods", type=Path, default=Path("handoff/ods"))
    parser.add_argument("--dws", type=Path, default=Path("handoff/dws"))
    parser.add_argument("--ads", type=Path, default=Path("handoff/ads/ads.db"))
    parser.add_argument("--dwd", type=Path, default=Path("handoff/dwd"))
    parser.add_argument("--require-dwd", action="store_true",
                        help="#3 交付 DWD 后打开：DWD 缺失由 SKIP 变为 FAIL")
    args = parser.parse_args()

    report = Report()
    info_lines: list[str] = []
    if not args.ads.is_file():
        print(f"[FAIL] ADS 数据库不存在：{args.ads}", file=sys.stderr)
        return 1

    connection = read_ads(args.ads)
    try:
        return reconcile(args, connection, report, info_lines)
    finally:
        connection.close()


def reconcile(
    args: argparse.Namespace,
    connection: sqlite3.Connection,
    report: Report,
    info_lines: list[str],
) -> int:
    meta = {row["key"]: row["value"] for row in connection.execute("SELECT key, value FROM ads_meta")}
    scalar = lambda sql, params=(): connection.execute(sql, params).fetchone()[0]  # noqa: E731

    # ---------------------------------------------------------------- A 内部自洽
    group = "A 口径自洽（ADS 自身）"
    daily_revenue = scalar("SELECT COALESCE(SUM(revenue_fen),0) FROM ads_daily")
    daily_orders = scalar("SELECT COALESCE(SUM(order_cnt),0) FROM ads_daily")
    daily_energy = scalar("SELECT COALESCE(SUM(energy_kwh),0) FROM ads_daily")
    report.close(group, "总营收：ads_daily 合计 == ads_meta.totalRevenueFen",
                 daily_revenue, int(meta.get("totalRevenueFen", 0)), MONEY_TOL, " 分")
    report.close(group, "总订单：ads_daily 合计 == ads_meta.orderCount",
                 daily_orders, int(meta.get("orderCount", 0)), COUNT_TOL)
    report.close(group, "总电量：ads_daily 合计 == ads_meta.totalEnergyKwh",
                 round(daily_energy, 3), float(meta.get("totalEnergyKwh", 0)), KWH_TOL, " kWh")

    station_day_revenue = scalar("SELECT COALESCE(SUM(revenue_fen),0) FROM ads_station_day")
    station_day_orders = scalar("SELECT COALESCE(SUM(order_cnt),0) FROM ads_station_day")
    station_day_energy = scalar("SELECT COALESCE(SUM(energy_kwh),0) FROM ads_station_day")
    report.close(group, "总营收：站点日表合计 == 全城日表合计",
                 station_day_revenue, daily_revenue, MONEY_TOL, " 分")
    report.close(group, "总订单：站点日表合计 == 全城日表合计",
                 station_day_orders, daily_orders, COUNT_TOL)
    report.close(group, "总电量：站点日表合计 == 全城日表合计",
                 round(station_day_energy, 3), round(daily_energy, 3), KWH_TOL, " kWh")

    bad_states = scalar(
        """SELECT COUNT(1) FROM ads_station
            WHERE idle_cnt + reserved_cnt + charging_cnt + fault_cnt + restarting_cnt
                  <> charger_cnt"""
    )
    report.check(group, "五状态之和 == charger_cnt（每站）", bad_states == 0,
                 "" if bad_states == 0 else f"{bad_states} 个站点不守恒")
    bad_mix = scalar("SELECT COUNT(1) FROM ads_station WHERE fast_cnt + slow_cnt <> charger_cnt")
    report.check(group, "快桩 + 慢桩 == charger_cnt（每站）", bad_mix == 0,
                 "" if bad_mix == 0 else f"{bad_mix} 个站点不守恒")
    report.close(group, "ads_station 桩数合计 == ads_charger 行数",
                 scalar("SELECT COALESCE(SUM(charger_cnt),0) FROM ads_station"),
                 scalar("SELECT COUNT(1) FROM ads_charger"), COUNT_TOL)

    rfm_users = scalar("SELECT COALESCE(SUM(user_cnt),0) FROM ads_user_rfm")
    report.close(group, "RFM 分层用户数之和 == ads_meta.firstOrderUserCount",
                 rfm_users, int(meta.get("firstOrderUserCount", 0)), COUNT_TOL)
    report.close(group, "RFM 分层营收之和 == 总营收",
                 scalar("SELECT COALESCE(SUM(revenue_fen),0) FROM ads_user_rfm"),
                 daily_revenue, MONEY_TOL, " 分")
    report.close(group, "行政区营收合计 == 全城营收合计",
                 scalar("SELECT COALESCE(SUM(revenue_fen),0) FROM ads_district"),
                 daily_revenue, MONEY_TOL, " 分")

    factor = float(meta.get("carbonFactorTonPerMwh", 0))
    expected_co2 = round(daily_energy / 1000.0 * factor * 1000, 2)
    report.close(group, "碳减排 = 电量 / 1000 × 0.581 tCO₂/MWh",
                 round(scalar("SELECT COALESCE(SUM(co2_saved_kg),0) FROM ads_district"), 2),
                 expected_co2, 1.0, " kg")

    bad_busy = scalar("SELECT COUNT(1) FROM ads_station_hourly WHERE busy_count > pile_count")
    report.check(group, "小时表 busy_count ≤ pile_count", bad_busy == 0,
                 "" if bad_busy == 0 else f"{bad_busy} 行越界")
    bad_util = scalar(
        "SELECT COUNT(1) FROM ads_station_hourly WHERE utilization < 0 OR utilization > 100"
    )
    report.check(group, "小时表 utilization ∈ [0,100]", bad_util == 0,
                 "" if bad_util == 0 else f"{bad_util} 行越界")

    stations_n = scalar("SELECT COUNT(1) FROM ads_station")
    days_n = scalar("SELECT COUNT(1) FROM ads_daily")
    report.close(group, "小时表行数 == 站点数 × 24 × 天数",
                 scalar("SELECT COUNT(1) FROM ads_station_hourly"), stations_n * 24 * days_n,
                 COUNT_TOL)

    enabled = scalar("SELECT COUNT(1) FROM ads_station WHERE forecast_enabled = 1")
    if enabled:
        report.close(group, "预测点数 == 参与预测站点数 × 24",
                     scalar("SELECT COUNT(1) FROM ads_forecast_24h"), enabled * 24, COUNT_TOL)
    metrics_summary = connection.execute(
        "SELECT COUNT(1), AVG(wape), AVG(baseline_wape) FROM ads_forecast_metric"
    ).fetchone()
    batch_summary = connection.execute(
        "SELECT COUNT(1), MIN(is_baseline), MAX(is_baseline), MAX(model_version) FROM ads_forecast_batch"
    ).fetchone()
    has_valid_batch = (
        int(batch_summary[0] or 0) > 0
        and int(batch_summary[1] or 0) in (0, 1)
        and int(batch_summary[2] or 0) in (0, 1)
    )
    report.check(group, "预测批次如实标注 is_baseline∈{0,1}", has_valid_batch)
    if int(batch_summary[2] or 0) == 1:
        info_lines.append(
            f"基线预测 WAPE 均值 {metrics_summary[1]:.2f}% vs 常量基线 "
            f"{metrics_summary[2]:.2f}%（seasonal-naive 相对朴素基线应有增益）"
        )
    else:
        info_lines.append(
            f"真实预测批次 {batch_summary[3]} 已接入；WAPE 均值 {metrics_summary[1]:.2f}% "
            f"vs 基线 {metrics_summary[2]:.2f}%"
        )

    # ---------------------------------------------------------------- B DWS ↔ ADS
    group = "B 层间一致（DWS ↔ ADS）"
    dws = read_dws(args.dws)
    if not dws:
        report.skip(group, "DWS 交接包", f"{args.dws} 不存在，先跑 build_local.py 或 Spark 作业")
    else:
        dws_station = dws["dws_station_day"]
        report.close(group, "dws_station_day 行数 == ads_station_day 行数",
                     len(dws_station), scalar("SELECT COUNT(1) FROM ads_station_day"), COUNT_TOL)
        report.close(group, "DWS 站点日营收合计 == ADS 站点日营收合计",
                     sum(int(float(row["revenue_fen"])) for row in dws_station),
                     station_day_revenue, MONEY_TOL, " 分")
        report.close(group, "DWS 站点日订单合计 == ADS 站点日订单合计",
                     sum(int(float(row["order_cnt"])) for row in dws_station),
                     station_day_orders, COUNT_TOL)

        ads_rows = {
            (row["dt"], row["station_id"]): row
            for row in connection.execute(
                "SELECT dt, station_id, revenue_fen, order_cnt FROM ads_station_day"
            )
        }
        mismatched = sum(
            1
            for row in dws_station
            if (row["dt"], int(float(row["station_id"]))) not in ads_rows
            or ads_rows[(row["dt"], int(float(row["station_id"])))]["revenue_fen"]
            != int(float(row["revenue_fen"]))
        )
        report.check(group, "逐行比对 (dt,station_id) 的营收", mismatched == 0,
                     "" if mismatched == 0 else f"{mismatched} 行不一致")

        dws_region = dws["dws_region_day"]
        report.close(group, "DWS 行政区营收合计 == ADS 行政区营收合计",
                     sum(int(float(row["revenue_fen"])) for row in dws_region),
                     scalar("SELECT COALESCE(SUM(revenue_fen),0) FROM ads_district"),
                     MONEY_TOL, " 分")
        report.close(group, "DWS 用户日消费合计 == ADS 全城营收合计",
                     sum(int(float(row["amount_fen"])) for row in dws["dws_user_day"]),
                     daily_revenue, MONEY_TOL, " 分")
        report.close(group, "DWS 桩日电量合计 == ADS 站点日电量合计",
                     round(sum(float(row["energy_kwh"]) for row in dws["dws_charger_day"]), 3),
                     round(station_day_energy, 3), KWH_TOL, " kWh")

    # ---------------------------------------------------------------- C ODS ↔ ADS
    group = "C 独立重算（ODS ↔ ADS）"
    if not (args.ods / "ods_orders").is_dir():
        report.skip(group, "ODS 交接包", f"{args.ods} 不存在")
    else:
        # 用与 build_local.py 相同的清洗规则从 ODS 重新算一遍，比对 ADS 的总量。
        # 这不是「自己跟自己对」，而是「同一规则在两个入口的结果是否一致」：
        # 只要有一侧的入口变了（列名、过滤条件、去重策略），这里立刻会炸。
        detected: Counter = Counter()
        stations = load_stations(args.ods, detected)
        chargers, charger_reference = load_chargers(args.ods, detected)
        users = load_users(args.ods, detected)
        orders = load_orders(args.ods, users, stations, charger_reference, chargers, detected)
        report.close(group, "重算总营收 == ads_meta.totalRevenueFen",
                     sum(order["amount_fen"] for order in orders),
                     int(meta.get("totalRevenueFen", 0)), MONEY_TOL, " 分")
        report.close(group, "重算总订单 == ads_meta.orderCount",
                     len(orders), int(meta.get("orderCount", 0)), COUNT_TOL)
        report.close(group, "重算总电量 == ads_meta.totalEnergyKwh",
                     round(sum(order["energy_kwh"] for order in orders), 3),
                     float(meta.get("totalEnergyKwh", 0)), KWH_TOL, " kWh")
        report.close(group, "清洗后桩数 == ads_charger 行数",
                     len(chargers), scalar("SELECT COUNT(1) FROM ads_charger"), COUNT_TOL)

    # ---------------------------------------------------------------- D DWD ↔ DWS
    group = "D 上游一致（DWD ↔ DWS）"
    dwd_orders = read_dwd(args.dwd, "dwd_order_detail")
    if dwd_orders is None:
        detail = f"{args.dwd} 下没有 dwd_order_detail（CSV 或 Parquet）"
        if args.require_dwd:
            report.check(group, "DWD 订单表可用", False, detail)
        else:
            report.skip(group, "DWD 订单表", detail + "，#3 交付前跳过")
    else:
        completed = [row for row in dwd_orders if str(row.get("status", "")).strip() == "completed"]
        dwd_amount = sum(int(float(row.get("amount_fen") or 0)) for row in completed)
        dws_amount = sum(int(float(row["revenue_fen"])) for row in dws.get("dws_station_day", []))
        report.close(group, "DWD 订单金额合计 == DWS 站点日营收合计（误差 0）",
                     dwd_amount, dws_amount, 0, " 分")
        report.close(group, "DWD completed 订单数 == DWS 站点日订单合计",
                     len(completed),
                     sum(int(float(row["order_cnt"])) for row in dws.get("dws_station_day", [])),
                     COUNT_TOL)

    # ---------------------------------------------------------------- 输出
    print(report.render())
    for line in info_lines:
        print(f"\n[info] {line}")
    total = len(report.rows)
    passed = sum(1 for _, _, status, _ in report.rows if status == "PASS")
    skipped = sum(1 for _, _, status, _ in report.rows if status == "SKIP")
    print(f"\n共 {total} 项：通过 {passed}，失败 {report.failed}，跳过 {skipped}")
    if report.failed:
        print("对账未通过 —— 不要把这批 ADS 交付给下游。", file=sys.stderr)
        return 1
    print("[OK] 对账全绿")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
