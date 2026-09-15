"""对重新读回的 DWD 独立验收；不能用“文件写成功”代替业务约束。"""
from functools import reduce
from pyspark.sql import functions as F
from part2.quality.rules import data_type


def original_names(dwd, contract):
    frames = {}
    for table, spec in contract["tables"].items():
        if not spec["dwd_table"]:
            continue
        frame = dwd[spec["dwd_table"]]
        expected = {"_row_id": data_type("string"), "_run_id": data_type("string")}
        expected.update({spec["rename"].get(name, name): data_type(dtype) for name, dtype in spec["columns"].items()})
        if table == "orders":
            expected["station_id"] = data_type("long")
        actual = {field.name: field.dataType for field in frame.schema.fields}
        if actual != expected:
            raise ValueError(f"DWD 类型／字段不符：{spec['dwd_table']}；{actual}")
        columns = [F.col(spec["rename"].get(name, name)).alias(name) for name in spec["columns"]]
        frames[table] = frame.select("_row_id", "_run_id", *columns, *(["station_id"] if table == "orders" else []))
    return frames


def assert_dwd(dwd, contract, policy):
    frames = original_names(dwd, contract)
    checks = []
    for table, frame in frames.items():
        spec = contract["tables"][table]
        optional = set(policy["optional"].get(table, [])) - set(policy["defaults"].get(table, {}))
        required = [name for name in spec["columns"] if name not in optional] + ["_row_id", "_run_id"]
        if table == "orders":
            required.append("station_id")
        invalid = [F.col(name).isNull() for name in required]
        for name, dtype in spec["columns"].items():
            if dtype == "timestamp":
                invalid.append(~F.year(F.col(name)).between(*policy["timestamp_year_range"]))
            if dtype == "string" and name in required and name != "avatar_path":
                invalid.append(F.length(F.trim(F.col(name))) == 0)
            if name == "id" or name.endswith("_id"):
                invalid.append(F.col(name) <= 0)
        for name, allowed in policy["enums"].get(table, {}).items():
            invalid.append(~F.col(name).isin(allowed))
        for name, (lower, upper) in policy["ranges"].get(table, {}).items():
            invalid.append(~F.col(name).between(lower, upper))
        if table == "users":
            invalid.append(~F.col("mobile").rlike(r"^1[0-9]{10}$"))
        if table == "stations":
            invalid += [~F.col(name).between(*bounds) for name, bounds in policy["beijing_bbox"].items()]
        if table == "orders":
            invalid += [F.col("amount_fen") < 0, F.col("status").isin("charging", "completed") & F.col("started_at").isNull(), (F.col("status") == "completed") & F.col("ended_at").isNull(), F.col("started_at") < F.col("reserved_at"), F.col("ended_at") < F.coalesce("started_at", "reserved_at"), (F.col("status") == "reserved") & (F.col("started_at").isNotNull() | F.col("ended_at").isNotNull()), (F.col("status") == "charging") & F.col("ended_at").isNotNull()]
        if table == "station_hourly":
            invalid += [F.col("busy_count") > F.col("pile_count"), F.col("load_kw") > F.col("rated_power_kw"), F.col("observed_at") != F.date_trunc("hour", F.col("observed_at"))]
        for field, parent in {"chargers": [("station_id", "stations")], "orders": [("user_id", "users"), ("charger_id", "chargers"), ("station_id", "stations")], "telemetry": [("charger_id", "chargers")], "station_hourly": [("station_id", "stations")]}.get(table, []):
            marker = "_exists_" + field
            keys = frames[parent].select(F.col("id").alias(field)).distinct().withColumn(marker, F.lit(1))
            frame = frame.join(keys, field, "left")
            invalid.append(F.col(marker).isNull())
        if table in {"orders", "telemetry"}:
            refs = frames["chargers"].select(F.col("id").alias("charger_id"), F.col("station_id").alias("_charger_station_id"), F.col("power_kw").alias("_rated"))
            frame = frame.join(refs, "charger_id", "left")
            if table == "telemetry":
                invalid.append(F.col("power_kw") > F.col("_rated"))
            else:
                invalid.append(F.col("station_id") != F.col("_charger_station_id"))
                prices = frames["stations"].select(F.col("id").alias("station_id"), F.col("price_fen_per_kwh").alias("_price"))
                frame = frame.join(prices, "station_id", "left")
                if policy["money"]["reference"] == "constant_station_price":
                    expected = F.round(F.col("_price").cast("decimal(18,0)") * F.col("energy_kwh"), 0)
                    tolerance = F.greatest(F.lit(policy["money"]["absolute_tolerance_fen"]).cast("decimal(18,6)"), expected * F.lit(policy["money"]["relative_tolerance"]).cast("decimal(18,6)"))
                    invalid.append((F.col("status") == "completed") & (F.abs(F.col("amount_fen") - expected) > tolerance))
        condition = reduce(lambda a, b: a | F.coalesce(b, F.lit(False)), invalid, F.lit(False))
        stats = frame.agg(F.count("*").alias("rows"), F.countDistinct(F.struct(*spec["primary_key"])).alias("keys"), F.countDistinct("_row_id").alias("row_ids"), F.sum(F.when(condition, 1).otherwise(0)).alias("violations")).first().asDict()
        stats["violations"] = stats["violations"] or 0
        ok = stats["rows"] == stats["keys"] == stats["row_ids"] and stats["violations"] == 0
        checks.append({"table": spec["dwd_table"], "ok": ok, **stats})
    if not all(item["ok"] for item in checks):
        raise ValueError(f"DWD 断言失败：{checks}")
    return checks
