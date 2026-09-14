# SCML Warehouse Jobs

This directory is reserved for #4 SparkSQL jobs after #3 publishes `handoff/dwd`.

Expected flow:

1. Read clean DWD and dimension Parquet from `/ev-charging/dwd`.
2. Build DWS Parquet under `/ev-charging/dws`.
3. Build ADS Parquet under `/ev-charging/ads`.
4. Export ADS tables to `handoff/ads/ads.db` for #2 Flask and #1 Web.

The current foundation only prepares ODS generation and handoff validation.
