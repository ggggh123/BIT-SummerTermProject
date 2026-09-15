"""ODS CSV 空串与显式 null 的无歧义读取；行列数由交接校验先行核验。"""
from pyspark.sql import functions as F


def read_csv_strings(spark, paths, schema, null_token):
    # Spark/Univocity 将未加引号的空字段解析为 null。
    # 先让解析器仅以空串作为 null，再恢复为空串；最后单独解码约定的 \\N。
    # 因此 csv_null 绝不能是空串；禁止先 nullValue=\\N 再填空（会丢失区别）。
    if not null_token:
        raise ValueError("ODS null 标记必须非空")
    frame = spark.read.schema(schema).options(mode="FAILFAST", header=True, enforceSchema=False, inferSchema=False, nullValue="", emptyValue="", multiLine=True, escape='"').csv(paths)
    return frame.select(*[F.when(F.col(name) == null_token, F.lit(None).cast("string")).otherwise(F.coalesce(F.col(name), F.lit(""))).alias(name) for name in schema.fieldNames()])
