"""已清洗内部行 → #4 精确 DWD schema；不依赖注入日志、不猜补业务值。"""
import json
from functools import reduce
from importlib.resources import files
from pyspark.sql import functions as F, types as T
from part2.quality.rules import accepted, data_type

PROFILE = "scml-dwd-v0.1"
TIME_FORMAT = "yyyy-MM-dd'T'HH:mm:ssXXX"
DISTRICTS = {"Chaoyang": "朝阳区", "Haidian": "海淀区", "Fengtai": "丰台区", "Tongzhou": "通州区", "Daxing": "大兴区"}


def load_scml_contract():
    return json.loads(files("part2").joinpath("contracts/scml-dwd-v0.1.json").read_text(encoding="utf-8"))


def timestamp(column):
    return F.date_format(F.col(column), TIME_FORMAT).alias(column)


def time_parts(column):
    return [F.hour(column).alias("hour"), F.date_format(column, "yyyy-MM-dd").alias("dt")]


def scml_frames(processed):
    """投影时保留原始名称；INT 不允许从非整数功率静默截断。"""
    spec = load_scml_contract()
    clean = {table: accepted(frame) for table, frame in processed.items()}
    if clean["chargers"].where(F.col("power_kw") != F.floor("power_kw")).limit(1).count():
        raise ValueError("下游 dim_chargers.power_kw 要求 INT，但输入包含非整数额定功率；拒绝截断")
    result = {}
    result["dim_users"] = clean["users"].select(F.col("id").alias("user_id"), "mobile", "nickname", "balance_fen", "status", timestamp("registered_at"))
    district_map = F.create_map(*[F.lit(value) for pair in DISTRICTS.items() for value in pair])
    stations = clean["stations"].withColumn("district_raw", F.get_json_object("_raw_json", "$.district")).withColumn("name_raw", F.get_json_object("_raw_json", "$.name"))
    stations = stations.withColumn("district", F.coalesce(district_map[F.col("district")], F.col("district")))
    stations = stations.withColumn("name", F.when(F.col("name").rlike(r"^(Chaoyang|Haidian|Fengtai|Tongzhou|Daxing) Station [0-9]+$"), F.concat("district", F.col("id"), F.lit("号充电站"))).otherwise(F.col("name")))
    result["dim_stations"] = stations.select(F.col("id").alias("station_id"), "name", "name_raw", "address", "district", "district_raw", "latitude", "longitude", "price_fen_per_kwh", "forecast_enabled", F.lit(0).alias("coord_imputed"))
    result["dim_chargers"] = clean["chargers"].select(F.col("id").alias("charger_id"), "station_id", "code", "type", "power_kw", "status", "charge_count", "total_duration_sec")
    orders = clean["orders"]
    wait = ((F.col("started_at").cast("long") - F.col("reserved_at").cast("long")) / 60.0).alias("wait_min")
    result["dwd_order_detail"] = orders.select(F.col("id").alias("order_id"), "user_id", "charger_id", "station_id", "status", timestamp("reserved_at"), timestamp("started_at"), timestamp("ended_at"), "energy_kwh", "amount_fen", wait, *time_parts("started_at"))
    chargers = clean["chargers"].select(F.col("id").alias("charger_id"), "station_id")
    result["dwd_telemetry_detail"] = clean["telemetry"].join(chargers, "charger_id").select(F.col("id").alias("telemetry_id"), "charger_id", "station_id", timestamp("recorded_at"), *time_parts("recorded_at"), "power_kw", "energy_increment_kwh", "event_type")
    result["dwd_station_hourly"] = clean["station_hourly"].select("station_id", timestamp("observed_at"), *time_parts("observed_at"), "pile_count", "rated_power_kw", "busy_count", "load_kw", F.round(F.col("busy_count") / F.col("pile_count") * 100, 1).alias("utilization"), "temperature_c", "is_holiday")
    events = clean["events"].withColumn("event_type", F.when(F.col("event_type") == "charger_fault", F.lit("fault")).otherwise(F.col("event_type")))
    result["dwd_event"] = events.select(F.col("id").alias("event_id"), "event_type", "entity_type", "entity_id", "message", timestamp("created_at"), *time_parts("created_at"))
    return {name: frame.select(*[F.col(column).cast(data_type(dtype)).alias(column) for column, dtype in spec["tables"][name]["columns"].items()]) for name, frame in result.items()}


def lineage_frame(processed, run_id):
    parts = []
    for table, spec in load_scml_contract()["tables"].items():
        raw = accepted(processed[spec["source"]])
        keys = ["station_id", "observed_at"] if spec["source"] == "station_hourly" else ["id"]
        parts.append(raw.select(F.lit(table).alias("table"), F.col("_row_id").alias("row_id"), F.to_json(F.struct(*keys)).alias("business_key_json"), F.lit(run_id).alias("run_id")))
    return reduce(lambda left, right: left.unionByName(right), parts)


def write_scml(frames, root):
    for name, spec in load_scml_contract()["tables"].items():
        writer = frames[name].write.mode("errorifexists")
        if spec["partition"]:
            writer = writer.partitionBy(*spec["partition"])
        writer.parquet(root.rstrip("/") + "/" + name)
        if spec["partition"] and frames[name].limit(1).count() == 0:
            # 空事实表也保留可读 schema；仅写零行文件，不造一条业务记录。
            frames[name].drop("dt").write.mode("errorifexists").parquet(root.rstrip("/") + "/" + name + "/dt=__HIVE_DEFAULT_PARTITION__")


def read_scml(spark, root):
    # dt 的契约是 STRING；禁止目录发现时自动推成 DateType。
    spark.conf.set("spark.sql.sources.partitionColumnTypeInference.enabled", "false")
    result = {}
    for name, spec in load_scml_contract()["tables"].items():
        frame = spark.read.parquet(root.rstrip("/") + "/" + name)
        for column in spec["partition"]:
            if isinstance(frame.schema[column].dataType, T.NullType):
                # 全部为 Hive NULL 分区时 Spark 给出 void；显式补回契约类型。
                frame = frame.withColumn(column, F.col(column).cast("string"))
        result[name] = frame
    return result


def assert_scml(frames, expected_counts=None):
    """写后读回验收：精确字段类型、保留量、键、时间分区、派生值和引用。"""
    checks = []
    specs = load_scml_contract()["tables"]
    if set(frames) != set(specs):
        raise ValueError("SCML DWD 必须恰好包含七张表")
    for table, spec in specs.items():
        frame = frames[table]
        expected_schema = {name: data_type(dtype) for name, dtype in spec["columns"].items()}
        actual_fields = [field.name for field in frame.schema.fields]
        if actual_fields != list(expected_schema) or {field.name: field.dataType for field in frame.schema.fields} != expected_schema:
            raise ValueError(f"SCML DWD 类型／字段不符：{table}")
        invalid = [F.col(name).isNull() for name in set(spec["columns"]) - set(spec["optional"])]
        for name, dtype in spec["columns"].items():
            if name.endswith("_id"):
                invalid.append(F.col(name) <= 0)
            if dtype == "double":
                invalid.append(F.isnan(name) | (F.abs(F.col(name)) == float("inf")))
        if spec.get("time"):
            column = F.col(spec["time"])
            parsed = F.to_timestamp(column, TIME_FORMAT)
            invalid.extend([column.isNotNull() & (~column.rlike(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+08:00$") | parsed.isNull()), ~F.col("dt").eqNullSafe(F.date_format(parsed, "yyyy-MM-dd")), ~F.col("hour").eqNullSafe(F.hour(parsed))])
        if table == "dwd_order_detail":
            start, reserved, end = (F.to_timestamp(name, TIME_FORMAT) for name in ("started_at", "reserved_at", "ended_at"))
            invalid.extend([(F.col("status") == "completed") & (start.isNull() | end.isNull()), start < reserved, end < start, F.col("energy_kwh") < 0, F.col("amount_fen") < 0, ~F.col("wait_min").eqNullSafe((start.cast("long") - reserved.cast("long")) / 60.0)])
        if table == "dwd_station_hourly":
            invalid.extend([~F.col("busy_count").between(0, F.col("pile_count")), F.col("load_kw") < 0, F.col("load_kw") > F.col("rated_power_kw"), F.col("utilization") != F.round(F.col("busy_count") / F.col("pile_count") * 100, 1)])
        if table == "dwd_event":
            invalid.append(~((F.col("event_type") == "fault") & (F.col("entity_type") == "charger") | (F.col("event_type") == "order_completed") & (F.col("entity_type") == "order")))
        if table == "dim_stations":
            invalid.append(F.col("coord_imputed") != 0)
        condition = reduce(lambda a, b: a | F.coalesce(b, F.lit(False)), invalid, F.lit(False))
        stats = frame.agg(F.count("*").alias("rows"), F.countDistinct(F.struct(*spec["key"])).alias("keys"), F.sum(F.when(condition, 1).otherwise(0)).alias("violations")).first().asDict()
        stats["violations"] = stats["violations"] or 0
        ok = stats["rows"] == stats["keys"] and stats["violations"] == 0
        if expected_counts is not None:
            ok = ok and stats["rows"] == expected_counts[table]
        checks.append({"table": table, "ok": ok, **stats})
    references = {
        "dim_chargers": [("station_id", "dim_stations")],
        "dwd_order_detail": [("user_id", "dim_users"), ("charger_id", "dim_chargers"), ("station_id", "dim_stations")],
        "dwd_telemetry_detail": [("charger_id", "dim_chargers"), ("station_id", "dim_stations")],
        "dwd_station_hourly": [("station_id", "dim_stations")],
    }
    for table, refs in references.items():
        for key, parent in refs:
            if frames[table].join(frames[parent].select(key), key, "left_anti").limit(1).count():
                raise ValueError(f"SCML 外键断言失败：{table}.{key}")
    for entity, parent, key in (("charger", "dim_chargers", "charger_id"), ("order", "dwd_order_detail", "order_id")):
        rows = frames["dwd_event"].where(F.col("entity_type") == entity)
        if rows.join(frames[parent].select(F.col(key).alias("entity_id")), "entity_id", "left_anti").limit(1).count():
            raise ValueError(f"SCML 事件引用断言失败：{entity}")
    if not all(item["ok"] for item in checks):
        raise ValueError(f"SCML DWD 断言失败：{checks}")
    return checks
