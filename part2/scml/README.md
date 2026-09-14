# #4 SCML Foundation

This folder contains the starter foundation for the second-stage #4 work:
simulation data generation and ODS handoff for downstream quality checking.

## What Is Ready

- `data_generator/generator.py`: deterministic ODS generator.
- `config/part2_scml_sample.yaml`: small sample config for quick checks.
- `config/part2_scml_full.yaml`: target scale config from the design document.
- `scripts/run_scml_sample.sh`: generate and validate a small ODS handoff.
- `scripts/run_scml_full.sh`: generate and validate the full ODS handoff.
- `scripts/validate_handoff.py`: verify row counts, SHA-256 values and injection log.
- `scripts/hdfs_put_ods.sh`: upload `handoff/ods` to `/ev-charging/ods`.
- `scripts/run_dim_date.sh`: generate DWD `dim_date` with Spark on YARN.
- `contracts/handoff_contract.md`: ODS/DWD/DWS/ADS handoff contract for #3/#2/#1.
- `warehouse/sql/ads_schema.sql`: ADS SQLite table contract for #2 Flask and #1 Web.
- `warehouse/README.md`: DWS/ADS SparkSQL job entry after #3 publishes DWD.

## Quick Start On The VM

```bash
cd /mnt/hgfs/term_project_all/BIT-SummerTermProject-latest/part2/scml
source /etc/profile.d/ev-second-project.sh
bash scripts/run_scml_sample.sh
bash scripts/hdfs_put_ods.sh
```

Expected local output:

```text
[OK] handoff/ods manifest, hashes, row counts and injection log are valid
```

Expected HDFS target:

```text
/ev-charging/ods
```

## Full Data Run

```bash
cd /mnt/hgfs/term_project_all/BIT-SummerTermProject-latest/part2/scml
source /etc/profile.d/ev-second-project.sh
bash scripts/run_scml_full.sh
bash scripts/hdfs_put_ods.sh
```

The full run uses the documented scale: 8 stations, about 300 chargers, 5000
users, at least 120000 orders, at least 1000000 telemetry records, 17280
station-hour records and at least 20000 events.

## Handoff To #3

Send or share the local `handoff/ods` folder after validation. It contains:

- one directory per ODS table with `_SUCCESS`;
- `dt=YYYY-MM-DD` partition directories for `ods_orders`, `ods_telemetry`,
  `ods_station_hourly` and `ods_events`;
- flat `part-00000.csv` for the dimension snapshots `ods_stations`, `ods_chargers`
  and `ods_users`;
- `manifest.json` with seed, data window, row counts and SHA-256 values;
- `injection_log.json` with one record per injected dirty row (Q1-Q10).

Partition details and the Spark `basePath` read recipe live in
`contracts/handoff_contract.md`.

Generated handoff data is intentionally ignored by git.

## Data Quality Injection

Ten issue classes are injected at the rates defined by the design document
(`QUALITY_RULES` in `data_generator/generator.py`), and **every mutated row is
recorded** in `injection_log.json` as `{rule, table, row_id, business_key,
description}`. A rule can hit more than one table:

| Rule | Tables | Rate |
|---|---|---|
| Q1 missing value | `ods_orders`, `ods_telemetry` | 0.5% |
| Q2 duplicate record | `ods_orders`, `ods_telemetry` | 1% |
| Q3 outlier | `ods_orders`, `ods_telemetry` | 0.3% |
| Q4 mixed timestamp format | `ods_orders`, `ods_telemetry` | 1% |
| Q5 logical contradiction | `ods_orders`, `ods_station_hourly` | 0.3% |
| Q6 wrong money unit | `ods_orders` | 0.5% |
| Q7 orphan reference | `ods_orders` | 0.3% |
| Q8 illegal field value | `ods_users`, `ods_chargers` | 0.3% |
| Q9 out-of-range coordinate | `ods_stations` | 1-2 rows |
| Q10 dirty text | `ods_stations`, `ods_users` | 0.5% |

`injection_log.json` also carries a `summary` block with the per-rule, per-table
counts, which is what `/api/quality/summary` needs for its `injected` column.

## Telemetry Time Axis

Telemetry frames are placed on a 5-minute grid **inside the history window**:
row `n` of `telemetry_count` lands on slot `(n-1) * window_slots // total`, so the
series never runs past `start_date + history_days`. At the full scale that means
roughly 39 of the 288 chargers report on each 5-minute tick, 1000000 frames over
90 days. The old `recorded_at = base + row_id * 5min` formula pushed the last
frame about 9.5 years into the future and broke `dt` partitioning.

## Handoff To #2 And #1

#2 can start Flask read-model work from `warehouse/sql/ads_schema.sql` before the
full ADS export exists. The final SQLite file will be written to:

```text
handoff/ads/ads.db
```

## DWD Date Dimension

#4 owns `dim_date`; after Hadoop/YARN is running:

```bash
source /etc/profile.d/ev-second-project.sh
bash scripts/run_dim_date.sh 2026-09-01 90 /ev-charging/dwd/dim_date
```
