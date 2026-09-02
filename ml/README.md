# ML Forecasting Subsystem

## Overview

This subsystem trains a Ridge regression model and a seasonal-naive baseline from 90 days of hourly station history, then produces 144 physically-bounded forecast records (6 stations × 24 horizons) and publishes them to the Qt server via the v1 TCP protocol.

## Data

- **Input**: `runtime/ml/station_hourly_history.csv`
- **Schema**: `station_id, observed_at, pile_count, rated_power_kw, temperature_c, is_holiday, busy_count, load_kw`
- **Size**: 6 stations × 90 days × 24 hours = 12,960 rows
- **Seed**: `20260901`
- **Time range**: `2026-06-03T09:00:00+08:00` → `2026-09-01T08:00:00+08:00`

## Architecture

```text
CSV history → load_history() → build_supervised() → chronological_split()
                                                              ↓
                                                  fit_ridge() + seasonal_naive
                                                              ↓
                                              choose_champion() (validation MAE)
                                                              ↓
                                              build_forecast_run() (144 records)
                                                              ↓
                                            publish_forecast() → save_last_good()
```

## Chronological Split

- **Train**: 62 days (first 62)
- **Validation**: 14 days (next 14)
- **Test**: 14 days (last 14)
- No random shuffling; split by `target_at` to prevent leakage.

## Features

Per (station, origin, horizon) row:

| Feature | Source | Leakage-safe |
|---|---|---|
| `target_hour_sin/cos` | target_at calendar | Yes (known future) |
| `target_dow_sin/cos` | target_at calendar | Yes |
| `target_is_weekend` | target_at calendar | Yes |
| `target_is_holiday` | target row | Yes (known schedule) |
| `target_temperature_c` | target row | Yes (simulated, known) |
| `load_lag_1` | origin row, shift(1) | Yes |
| `load_lag_24` | origin row, shift(24) | Yes |
| `load_roll_6` | shift(1).rolling(6).mean() | Yes |
| `load_roll_24` | shift(1).rolling(24).mean() | Yes |
| `busy_lag_1/24` | same pattern | Yes |
| `busy_roll_6/24` | same pattern | Yes |

## Champion Selection

- Compare average validation MAE across horizons 1/6/24.
- If Ridge < baseline → use Ridge; else use seasonal-naive.
- Selection uses validation only, never test.

## Physical Bounds

- `predicted_load_kw` ∈ [0, rated_power_kw]
- `predicted_busy_count` ∈ [0, pile_count]
- `predicted_idle_count` = pile_count - busy_count
- No NaN/Inf allowed.

## Congestion Levels

- `busy / pile_count < 50%` → `low`
- `50% ≤ ratio < 80%` → `medium`
- `ratio ≥ 80%` → `high`

## Peak Marking

Each station marks the consecutive 2-hour window with the highest total predicted load as `is_peak=true`.

## Usage

### Run full pipeline (train + forecast)

```bash
python -m evml.cli run-all \
  --history runtime/ml/station_hourly_history.csv \
  --cutoff 2026-09-01T09:00:00+08:00 \
  --generated-at 2026-09-01T12:00:00+08:00 \
  --output-dir runtime/ml/artifacts
```

### Publish to server

```bash
python -m evml.cli publish \
  --candidate runtime/ml/artifacts/forecast_candidate.json \
  --host 127.0.0.1 --port 9100 \
  --last-good runtime/ml/forecast_last_good.json
```

### Run tests

```bash
PYTHONPATH=ml/src python -m pytest ml/tests -v
```

## Artifacts

| File | Description |
|---|---|
| `model_load.joblib` | Trained Ridge pipeline for load_kw |
| `model_busy.joblib` | Trained Ridge pipeline for busy_count |
| `metrics.json` | Evaluation metrics (MAE/WAPE) for both models on val/test |
| `forecast_candidate.json` | 144-record publishable forecast (no metrics) |
| `run_summary.json` | Run metadata + champion selection + metrics |
| `forecast_last_good.json` | Last successful publish payload (only after server ACK) |

## Metrics (Real Data)

| Model | Target | h=1 MAE | h=6 MAE | h=24 MAE |
|---|---|---|---|---|
| Ridge | load_kw | 37.78 | 37.77 | 37.77 |
| Seasonal-naive | load_kw | 37.80 | 76.12 | 25.04 |
| Ridge | busy_count | 0.92 | 0.92 | 0.92 |
| Seasonal-naive | busy_count | 0.81 | 1.85 | 0.47 |

Ridge was selected as champion for both targets based on validation MAE.
