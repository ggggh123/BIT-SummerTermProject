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

- one directory per ODS table, each with `part-00000.csv` and `_SUCCESS`;
- `manifest.json` with seed, row counts and SHA-256 values;
- `injection_log.json` with Q1-Q10 issue records.

Generated handoff data is intentionally ignored by git.

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
