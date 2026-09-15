"""分布式计数、有限样本和报告生成；注入日志只在检测完成后对账。"""
from datetime import datetime
from decimal import Decimal
from pyspark.sql import functions as F, Window
from part2.clean.normalization import SHANGHAI
from part2.quality.rules import rejected


def json_default(value):
    if isinstance(value, Decimal):
        return str(value)
    if isinstance(value, datetime):
        return value.replace(tzinfo=SHANGHAI).isoformat() if value.tzinfo is None else value.astimezone(SHANGHAI).isoformat()
    raise TypeError(type(value).__name__)


def profiles(raw_frames, processed, contract):
    report = {}
    for table, raw in raw_frames.items():
        columns = contract["tables"][table]["columns"]
        expressions = [F.count("*").alias("rows")]
        for name in columns:
            expressions += [F.sum(F.when(F.col(name).isNull(), 1).otherwise(0)).alias(name + "__null"), F.sum(F.when(F.col(name).isNotNull() & (F.trim(F.col(name)) == ""), 1).otherwise(0)).alias(name + "__empty"), F.approx_count_distinct(name, .05).alias(name + "__unique_approx")]
        stats = raw.agg(*expressions).first().asDict()
        numeric = []
        for name, dtype in columns.items():
            if dtype != "string":
                numeric += [F.min(name).alias(name + "__min"), F.max(name).alias(name + "__max")]
        ranges = processed[table].agg(*numeric).first().asDict() if numeric else {}
        report[table] = {"rows": stats.pop("rows"), "raw_columns": {name: {"null": stats[name + "__null"] or 0, "empty_string": stats[name + "__empty"] or 0, "distinct_approx": stats[name + "__unique_approx"]} for name in columns}, "parsed_ranges_including_rejected": ranges, "distinct_rsd": .05}
    return report


def score_distributed(spark, expected, findings, rules):
    """以 (规则, 表, 原始行) 做集合对账；级联影响另报，不伪装成注入误报。"""
    keys = ["rule", "table", "row_id"]
    truth = expected.select(*keys).distinct().withColumn("_truth", F.lit(1))
    actual = findings.where(F.col("origin") == "direct").select(*keys).distinct().withColumn("_found", F.lit(1))
    joined = truth.join(actual, keys, "full")
    counts = joined.groupBy("rule").agg(F.sum(F.when(F.col("_truth").isNotNull() & F.col("_found").isNotNull(), 1).otherwise(0)).alias("truePositive"), F.sum(F.when(F.col("_truth").isNull(), 1).otherwise(0)).alias("falsePositive"), F.sum(F.when(F.col("_found").isNull(), 1).otherwise(0)).alias("falseNegative"))
    result = {rule: {"truePositive": 0, "falsePositive": 0, "falseNegative": 0, "recall": None, "precision": None} for rule in rules}
    for row in counts.collect():  # 至多十个聚合桶，不收集业务全表。
        row = row.asDict()
        rule = row.pop("rule")
        tp, fp, fn = (row[name] for name in ("truePositive", "falsePositive", "falseNegative"))
        result[rule] = {**row, "recall": tp / (tp+fn) if tp+fn else None, "precision": tp / (tp+fp) if tp+fp else None}
    return result


def build_reports(raw_frames, processed, findings, contract, policy, metadata, expected, assertions, dwd_mapping=None):
    quality = {**metadata, "report_type": "quality_report", "profile": profiles(raw_frames, processed, contract), "rules": {}, "injection_comparison": score_distributed(findings.sparkSession, expected, findings, [item["id"] for item in contract["rules"]])}
    grouped = [row.asDict() for row in findings.groupBy("table", "rule", "origin").count().collect()]
    samples = findings.withColumn("_rank", F.row_number().over(Window.partitionBy("table", "rule", "origin").orderBy("row_id"))).where(F.col("_rank") <= policy["sample_limit"]).drop("_rank")
    sample_rows = [row.asDict(recursive=True) for row in samples.collect()]
    for rule in contract["rules"]:
        rule_id = rule["id"]
        counts = [item for item in grouped if item["rule"] == rule_id]
        quality["rules"][rule_id] = {"name": rule["name"], "by_table_and_origin": [{**item, "rate": item["count"] / quality["profile"][item["table"]]["rows"] if quality["profile"][item["table"]]["rows"] else None} for item in counts], "samples": [item for item in sample_rows if item["rule"] == rule_id]}
    quality["notes"] = ["注入标签不参与检测；对账使用唯一的 (rule, table, row_id)。", "falsePositive 表示未匹配注入日志的直接命中；正式数据需人工确认日志是否穷尽问题。", "cascade 是维度／实体隔离导致的关联影响，单独列示，不混入直接注入 TP/FP/FN。", "每表、规则、来源最多保留 5 个样本；完整问题与原始隔离行在 Parquet 审计表。", "原始 distinct 为 RSD=0.05 的近似值；parsed_ranges 是可解析值的清洗前范围，包含隔离行。"]
    cleaning = {**metadata, "report_type": "cleaning_report", "tables": {}, "dwd_assertions": assertions}
    for table, frame in processed.items():
        reject = rejected()
        repair = F.exists("_issues", lambda item: item["action"] == "repair")
        fill = F.exists("_issues", lambda item: item["action"] == "fill")
        stats = frame.agg(F.count("*").alias("before"), F.sum(F.when(reject, 1).otherwise(0)).alias("quarantined"), F.sum(F.when(~reject & repair, 1).otherwise(0)).alias("repaired_retained_rows"), F.sum(F.when(~reject & fill, 1).otherwise(0)).alias("filled_retained_rows"), F.sum(F.when(F.exists("_issues", lambda item: item["origin"] == "cascade"), 1).otherwise(0)).alias("cascade_affected_rows"), F.sum(F.when(F.size("_issues") > 0, 1).otherwise(0)).alias("affected_rows")).first().asDict()
        stats = {key: value or 0 for key, value in stats.items()}
        stats["after"] = stats["before"] - stats["quarantined"]
        stats["retention_rate"] = stats["after"] / stats["before"] if stats["before"] else None
        stats["dwd_table"] = dwd_mapping[table] if dwd_mapping else contract["tables"][table]["dwd_table"]
        if stats["dwd_table"] and assertions:
            readback = next(item["rows"] for item in assertions if item["table"] == stats["dwd_table"])
            if readback != stats["after"]:
                raise ValueError(f"DWD 读回行数与清洗保留数不一致：{table}")
        cleaning["tables"][table] = stats
    dispositions = findings.withColumn("action", F.explode("actions")).groupBy("table", "rule", "origin", "action").agg(F.countDistinct("row_id").alias("rows"))
    cleaning["rule_action_hits"] = [row.asDict() for row in dispositions.collect()]
    orders = processed["orders"]
    reject = rejected()
    ledger = orders.agg(F.sum("_amount_raw").alias("raw_declared_fen_numeric_sum"), F.sum(F.when(reject, F.col("_amount_raw"))).alias("quarantined_raw_numeric_sum"), F.sum(F.when(~reject, F.col("amount_fen").cast("decimal(24,6)") - F.col("_amount_raw"))).alias("retained_repair_delta_fen"), F.sum(F.when(~reject, F.col("amount_fen").cast("decimal(24,6)"))).alias("output_amount_fen"), F.sum(F.when(F.col("_amount_raw").isNull(), 1).otherwise(0)).alias("unparseable_or_missing_amount_rows")).first().asDict()
    for name in list(ledger):
        ledger[name] = ledger[name] or 0
    ledger["balanced"] = ledger["raw_declared_fen_numeric_sum"] - ledger["quarantined_raw_numeric_sum"] + ledger["retained_repair_delta_fen"] == ledger["output_amount_fen"]
    ledger["note"] = "原始合计按原列声明的分口径统计，不代表真实营收；无法解析的金额不参与数值合计并单独计数。修正差额、隔离量均显式保留。"
    if not ledger["balanced"]:
        raise ValueError("订单清洗金额对账不平")
    cleaning["order_amount_reconciliation"] = ledger
    event_note = "events 输出 dwd_event，charger_fault 映射为 fault；原始行追踪位于 audit/dwd_lineage。" if dwd_mapping else "events 参与质量检测并保存 audit/clean_events，不新增未约定的 DWD 事件表。"
    cleaning["notes"] = ["before = after + quarantined，按唯一原始行计算，不能将规则命中量直接相加。", "修正／填充保留行可相互重叠，不能相加得保留数；隔离行的修正建议不算实际保留修正。", event_note, "当前缺少 #4 的 dim_date，因此没有宣称日期维度已验收。"]
    return quality, cleaning


def markdown_report(quality, cleaning):
    lines = ["# PRL 数据质量与清洗试跑报告", "", f"批次：`{quality['run_id']}`；输入类型：`{quality['source_kind']}`。", "", f"契约：`{quality['contract_version']}`；策略：`{quality['policy_version']}`；均待团队确认。", "", "## 清洗前后（唯一原始行）", "", "| 表 | 输入 | 保留 | 隔离 | 修正后保留 | 级联影响 |", "|---|---:|---:|---:|---:|---:|"]
    for table, item in cleaning["tables"].items():
        lines.append(f"| {table} | {item['before']} | {item['after']} | {item['quarantined']} | {item['repaired_retained_rows']} | {item['cascade_affected_rows']} |")
    lines += ["", "## 注入对账（直接问题，不含级联）", "", "| 规则 | TP | FP | FN | 召回率 | 精确率 |", "|---|---:|---:|---:|---:|---:|"]
    for rule, item in quality["injection_comparison"].items():
        rate = lambda value: "无分母" if value is None else f"{value:.1%}"
        lines.append(f"| {rule} | {item['truePositive']} | {item['falsePositive']} | {item['falseNegative']} | {rate(item['recall'])} | {rate(item['precision'])} |")
    count = len(cleaning.get("dwd_assertions", []))
    lines += ["", "## 边界与证据", "", "- 检测来自真实 Spark 规则，不来自注入标签；完整 JSON 含规则细节与有限样本。", f"- {count} 张 DWD 表写入后重新读取并执行类型、键、必填、外键、范围、时间及金额断言。", "- 这不是正式全量验收；测试夹具的指标不能用于宣称 #4 全量数据质量。", "- 本机 Ubuntu 25.04 的结果不等于 Ubuntu 22.04 集成验证。", "- 字段、价格冻结口径、阈值和报告接口仍须队友确认。", ""]
    if quality.get("pending_policy_notes"):
        lines += ["## SCML 兼容层待确认项", ""] + ["- " + note for note in quality["pending_policy_notes"]] + [""]
    if quality.get("hourly_coverage"):
        coverage = quality["hourly_coverage"]
        lines += [f"小时连续性：期望 {coverage['expected_station_hours']} 行，保留 {coverage['retained_station_hours']} 行，缺口 {coverage['missing_station_hours']} 行。", "", coverage["note"], ""]
    return "\n".join(lines)
