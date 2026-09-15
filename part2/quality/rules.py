"""PySpark 十类检测／处置编排；DataFrame 关联、窗口去重，不读取注入日志。"""
from functools import reduce
from pyspark.sql import functions as F, types as T, Window
from part2.quality.record_rules import inspect_record

ISSUE_TYPE = T.StructType([T.StructField(name, T.StringType(), False) for name in ("rule", "action", "detail", "origin")])
ISSUES_TYPE = T.ArrayType(ISSUE_TYPE, False)


def data_type(name):
    simple = {"string": T.StringType(), "long": T.LongType(), "integer": T.IntegerType(), "double": T.DoubleType(), "timestamp": T.TimestampType()}
    if name in simple:
        return simple[name]
    if name.startswith("decimal("):
        precision, scale = map(int, name[8:-1].split(","))
        return T.DecimalType(precision, scale)
    raise ValueError(f"未知契约类型：{name}")


def raw_schema(spec):
    return T.StructType([T.StructField(name, T.StringType(), True) for name in ["_row_id", *spec["columns"]]])


def normalized_schema(table, spec):
    fields = [T.StructField("_row_id", T.StringType(), False), T.StructField("_raw_json", T.StringType(), False)]
    fields += [T.StructField(name, data_type(dtype), True) for name, dtype in spec["columns"].items()]
    if table == "orders":
        fields.append(T.StructField("_amount_raw", T.DecimalType(24, 6), True))
    fields.append(T.StructField("_issues", ISSUES_TYPE, False))
    return T.StructType(fields)


def add_issue(frame, condition, rule, action, detail, origin="direct"):
    item = F.struct(*[F.lit(value).alias(name) for name, value in zip(("rule", "action", "detail", "origin"), (rule, action, detail, origin))])
    return frame.withColumn("_issues", F.when(F.coalesce(condition, F.lit(False)), F.concat("_issues", F.array(item))).otherwise(F.col("_issues")))


def rejected():
    return F.exists("_issues", lambda item: item["action"].isin("reject", "deduplicate"))


def accepted(frame):
    return frame.where(~rejected())


def deduplicate(frame, spec):
    """无 source_updated_at 契约：不编造“最新一条”，冲突内容全隔离。"""
    keys = spec["primary_key"]
    key_valid = reduce(lambda a, b: a & b, [F.col(key).isNotNull() for key in keys])
    group = Window.partitionBy(*keys)
    frame = frame.withColumn("_signature", F.to_json(F.struct(*spec["columns"]), {"ignoreNullFields": "false"}))
    frame = frame.withColumn("_conflict", F.min("_signature").over(group) != F.max("_signature").over(group))
    frame = frame.withColumn("_duplicate_rank", F.row_number().over(group.orderBy("_row_id")))
    frame = add_issue(frame, key_valid & F.col("_conflict"), "Q2", "reject", "相同业务键但内容冲突：本组全部隔离")
    frame = add_issue(frame, key_valid & ~F.col("_conflict") & (F.col("_duplicate_rank") > 1), "Q2", "deduplicate", "相同标准化业务内容：保留字典序最小的原始行标识")
    return frame.drop("_signature", "_conflict", "_duplicate_rank")


def foreign_key(frame, field, raw_parent, clean_parent, parent_key="id", predicate=None):
    predicate = F.lit(True) if predicate is None else predicate
    raw_keys = raw_parent.select(F.col(parent_key).alias(field)).where(F.col(field).isNotNull()).distinct().withColumn("_raw_match", F.lit(1))
    clean_keys = clean_parent.select(F.col(parent_key).alias(field)).where(F.col(field).isNotNull()).distinct().withColumn("_clean_match", F.lit(1))
    frame = frame.join(raw_keys, field, "left").join(clean_keys, field, "left")
    condition = predicate & F.col(field).isNotNull()
    frame = add_issue(frame, condition & F.col("_raw_match").isNull(), "Q7", "reject", f"{field}: 原始维度／实体中不存在引用")
    frame = add_issue(frame, condition & F.col("_raw_match").isNotNull() & F.col("_clean_match").isNull(), "Q7", "reject", f"{field}: 被引用记录已被清洗隔离", "cascade")
    return frame.drop("_raw_match", "_clean_match")


def money_reference(frame, policy):
    """参考值来自已通过清洗的维度；精确 100 倍吻合才允许修正元混入。"""
    money = policy["money"]
    if money["reference"] not in {"constant_station_price", "units_only"}:
        raise ValueError("未知订单单价口径")
    valid_integer = F.col("amount_fen").isNotNull() & (F.col("amount_fen") >= 0)
    expected = F.round(F.col("_price_fen_per_kwh").cast("decimal(18,0)") * F.col("energy_kwh"), 0)
    frame = frame.withColumn("_expected_fen", expected)
    reference = (F.col("status") == "completed") & F.col("_expected_fen").isNotNull() & F.lit(money["reference"] == "constant_station_price")
    tolerance = F.greatest(F.lit(money["absolute_tolerance_fen"]).cast("decimal(18,6)"), F.col("_expected_fen") * F.lit(money["relative_tolerance"]).cast("decimal(18,6)"))
    mismatch = F.abs(F.col("_amount_raw") - F.col("_expected_fen")) > tolerance
    repaired = reference & (mismatch | ~valid_integer) & (F.col("_amount_raw") >= 0) & (F.col("_amount_raw") * 100 == F.col("_expected_fen")) & F.lit(money["repair_exact_yuan_factor"])
    frame = frame.withColumn("_money_repaired", F.coalesce(repaired, F.lit(False)))
    frame = add_issue(frame, F.col("_money_repaired"), "Q6", "repair", "原值 ×100 与冻结参考分值精确吻合，修正元混入")
    frame = add_issue(frame, ~F.col("_money_repaired") & (~valid_integer | (reference & mismatch)), "Q6", "reject", "金额不是有效整数分，或超出参考金额容差且不能证明单位错误")
    frame = frame.withColumn("amount_fen", F.when(F.col("_money_repaired"), F.col("_expected_fen").cast("long")).otherwise(F.col("amount_fen")))
    return frame.drop("_expected_fen", "_money_repaired", "_price_fen_per_kwh")


def run_rules(spark, raw_frames, contract, policy):
    """返回所有带审计信息的标准化行。调用方必须设置可靠 checkpoint 目录。

    小型配置发送到 Python worker；业务记录始终在 Spark 中，不 collect 全表。
    """
    normalized, processed = {}, {}
    for table, spec in contract["tables"].items():
        # 用默认参数绑定当前表，避免延迟执行闭包全部指向最后一张表。
        rows = raw_frames[table].rdd.map(lambda row, table=table, spec=spec: inspect_record(table, row.asDict(), spec, policy))
        normalized[table] = spark.createDataFrame(rows, normalized_schema(table, spec)).checkpoint(eager=True)
    for table in ("users", "stations", "chargers", "orders", "telemetry", "station_hourly", "events"):
        frame = normalized[table]
        references = {"chargers": [("station_id", "stations")], "orders": [("user_id", "users"), ("charger_id", "chargers")], "telemetry": [("charger_id", "chargers")], "station_hourly": [("station_id", "stations")]}.get(table, [])
        for field, parent in references:
            frame = foreign_key(frame, field, normalized[parent], accepted(processed[parent]))
        if table == "events":
            for entity, parent in (("order", "orders"), ("charger", "chargers"), ("station", "stations"), ("user", "users")):
                frame = foreign_key(frame, "entity_id", normalized[parent], accepted(processed[parent]), predicate=F.col("entity_type") == entity)
        if table == "orders":
            chargers = accepted(processed["chargers"]).select(F.col("id").alias("charger_id"), "station_id")
            stations = accepted(processed["stations"]).select(F.col("id").alias("station_id"), F.col("price_fen_per_kwh").alias("_price_fen_per_kwh"))
            frame = frame.join(chargers.join(stations, "station_id"), "charger_id", "left")
            frame = money_reference(frame, policy)
        if table == "telemetry":
            rated = accepted(processed["chargers"]).select(F.col("id").alias("charger_id"), F.col("power_kw").alias("_rated_power_kw"))
            frame = frame.join(rated, "charger_id", "left")
            frame = add_issue(frame, F.col("power_kw") > F.col("_rated_power_kw"), "Q3", "reject", "遥测功率超过关联充电桩额定功率").drop("_rated_power_kw")
        processed[table] = deduplicate(frame, contract["tables"][table]).checkpoint(eager=True)
        print(f"PRL_PROGRESS=rules:{table}", flush=True)
    return processed


def finding_frame(processed):
    pieces = []
    for table, frame in processed.items():
        pieces.append(frame.select(F.lit(table).alias("table"), F.col("_row_id").alias("row_id"), F.explode("_issues").alias("issue")).select("table", "row_id", "issue.*"))
    return reduce(lambda left, right: left.unionByName(right), pieces).groupBy("table", "row_id", "rule", "origin").agg(F.sort_array(F.collect_set("action")).alias("actions"), F.sort_array(F.collect_set("detail")).alias("details"))


def dwd_frames(processed, contract, run_id):
    result = {}
    for table, spec in contract["tables"].items():
        if not spec["dwd_table"]:
            continue
        columns = [F.col(name).alias(spec["rename"].get(name, name)) for name in spec["columns"]]
        if table == "orders":
            columns.append(F.col("station_id"))
        result[spec["dwd_table"]] = accepted(processed[table]).select("_row_id", *columns).withColumn("_run_id", F.lit(run_id))
    return result
