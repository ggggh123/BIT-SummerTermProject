"""诊断：站点 × 整点 load_kw 的低分位与最小值（#5 自测，非交付物）。"""
from pyspark.sql import SparkSession, functions as F

SRC = "hdfs://pangxiangzhen01:8020/ev-charging/tmp/dwd_station_hourly_synth"
spark = SparkSession.builder.appName("diag-low").getOrCreate()
df = spark.read.parquet(SRC)
df.createOrReplaceTempView("t")

print("=== 全部时段的总体分布 ===")
spark.sql("""
SELECT COUNT(*) n,
       ROUND(MIN(load_kw),2) mn, ROUND(AVG(load_kw),2) avg,
       ROUND(percentile_approx(load_kw,0.01),2) p01,
       ROUND(percentile_approx(load_kw,0.05),2) p05,
       ROUND(SUM(CASE WHEN load_kw = 0 THEN 1 ELSE 0 END)/COUNT(*)*100,2) zero_pct
FROM t
""").show()

print("=== hour=0 各站分布 ===")
spark.sql("""
SELECT station_id,
       COUNT(*) n,
       ROUND(MIN(load_kw),2) mn,
       ROUND(percentile_approx(load_kw,0.05),2) p05,
       ROUND(AVG(load_kw),2) avg,
       SUM(CASE WHEN load_kw <= 0.001 THEN 1 ELSE 0 END) zero_cnt
FROM t WHERE hour(observed_at) = 0
GROUP BY station_id ORDER BY station_id
""").show(10)

print("=== station=1 各整点 P05 / avg ===")
spark.sql("""
SELECT hour(observed_at) h,
       ROUND(MIN(load_kw),2) mn,
       ROUND(percentile_approx(load_kw,0.05),2) p05,
       ROUND(AVG(load_kw),2) avg,
       ROUND(AVG(busy_count),2) avg_busy
FROM t WHERE station_id = 1
GROUP BY hour(observed_at) ORDER BY h
""").show(26)

print("=== load_kw 与 busy_count 的一致性（应满足 load>0 当 busy>0）===")
spark.sql("""
SELECT SUM(CASE WHEN busy_count > 0 AND load_kw <= 0.001 THEN 1 ELSE 0 END) bad_rows,
       COUNT(*) total
FROM t
""").show()

spark.stop()
print("DIAG_DONE")
