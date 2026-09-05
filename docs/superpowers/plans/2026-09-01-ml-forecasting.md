# ML Forecasting Subsystem Implementation Plan

> **状态：可选参考计划。** 2026-09-04 起，ML 训练、在线发布和预测展示不纳入 core acceptance 或 core release；本计划的 checkbox、测试记录和既有成果均保留，只有在显式启用 ML optional profile 时才作为独立参考执行。当前范围见 [`2026-09-04-core-scope-rebaseline.md`](2026-09-04-core-scope-rebaseline.md)。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Train a reproducible seasonal-naive baseline and Ridge models from the fixed 90-day station history, produce 6-station × 24-hour forecasts with 1h/6h/24h metrics, and publish one validated 144-record run to the Qt service.

**Architecture:** Python reads the immutable CSV history exported by the data subsystem, creates leakage-safe horizon samples, compares Ridge against yesterday-same-hour baseline on chronological splits, bounds all outputs physically, and publishes through the shared v1 framed JSON protocol. `forecast_last_good.json` changes only after the server acknowledges all 144 records.

**Tech Stack:** Python 3, pandas, NumPy, scikit-learn Ridge/Pipeline, joblib, pytest, standard-library socket/struct/json.

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- Run every Python command from the repository root with the globally installed system `python3`; do not create/activate a virtual environment, invoke pip, or install the package editable. Every ML test command uses `PYTHONPATH=ml/src python3 -m pytest ...`, and every CLI command uses `PYTHONPATH=ml/src python3 -m evml.cli ...`.
- Input contains exactly 6 stations × 90 days × 24 hours = 12,960 rows, generated with seed `20260901` and explicit `+08:00` timestamps.
- The six input station IDs are exactly the database rows with `forecast_enabled=1`; newly created stations are outside v1 model scope and show no forecast.
- Chronological split is 62 days train, 14 days validation, 14 days test; never randomize rows.
- History features may use only data at or before forecast origin; target time features may describe the known target calendar but not target measurements.
- Produce horizons 1–24 for each station and report horizons 1, 6 and 24.
- Forecast run contains exactly 144 unique records and no NaN/Inf.
- Busy count is within `[0,pileCount]`; idle count equals pileCount minus busy; load is within `[0,ratedPowerKw]`.
- ML never writes SQLite. It publishes action `forecast.publish` through the v1 4-byte big-endian JSON protocol.
- Test fixtures may use the fixed Sep 1 cutoff, but release generation must take an explicit recorded `--cutoff/--generated-at` whose 24-hour forecast window covers the Sep 10 presentation slot. No code silently substitutes the current clock.
- If Ridge validation performance is not better than baseline for a target, deploy the baseline for that target and report the choice honestly.
- Use Ubuntu's system `python3-sklearn` package (scikit-learn 1.4.x on the demonstration host) as authoritative; Ridge, TimeSeriesSplit, MAE and R² cover the frozen scope. Do not install scikit-learn or other ML dependencies with pip.

---

## Planned File Map

- `ml/pyproject.toml` — package, dependencies and `evml` CLI.
- `ml/src/evml/types.py` — immutable run/record/metric types.
- `ml/src/evml/repository.py` — validated CSV loading.
- `ml/src/evml/features.py` — chronological split and horizon samples.
- `ml/src/evml/baseline.py` — seasonal-naive prediction.
- `ml/src/evml/metrics.py` — MAE/WAPE.
- `ml/src/evml/ridge.py` — model fitting and champion selection.
- `ml/src/evml/forecast.py` — future 24-point run and physical bounds.
- `ml/src/evml/pipeline.py` — reproducible artifacts.
- `ml/src/evml/protocol.py` — Python frame/envelope implementation.
- `ml/src/evml/publisher.py` — server publish and acknowledgement.
- `ml/src/evml/last_good.py` — atomic last-good handling.
- `ml/src/evml/cli.py` — `train`, `forecast`, `publish`, `run-all`.
- `ml/tests/*` — focused pytest suites.

### Task 1: Package, immutable types, input validation, and chronological split

**Files:**
- Create: `ml/pyproject.toml`
- Create: `ml/src/evml/__init__.py`
- Create: `ml/src/evml/types.py`
- Create: `ml/src/evml/repository.py`
- Create: `ml/src/evml/features.py`
- Create: `ml/tests/test_repository_features.py`

**Interfaces:**
- Consumes: `runtime/ml/station_hourly_history.csv` from the data plan.
- Produces: `load_history(path) -> DataFrame`, `chronological_split(history) -> TimeSplit`, `build_supervised(history) -> DataFrame`.

- [ ] **Step 1: Define exact immutable output types**

```python
@dataclass(frozen=True)
class ForecastRecord:
    station_id: int
    forecast_at: str
    horizon_h: int
    predicted_load_kw: float
    predicted_busy_count: int
    predicted_idle_count: int
    congestion_level: Literal["low", "medium", "high"]
    is_peak: bool

@dataclass(frozen=True)
class MetricRow:
    model: str
    target: Literal["load_kw", "busy_count"]
    horizon_h: Literal[1, 6, 24]
    mae: float
    wape: float | None

@dataclass(frozen=True)
class ForecastRun:
    run_id: str
    generated_at: str
    data_cutoff: str
    model_version: str
    records: tuple[ForecastRecord, ...]
    metrics: tuple[MetricRow, ...]
```

- [ ] **Step 2: Write failing input/split/leakage tests**

```python
history = load_history(csv_fixture)
assert len(history) == 12960
assert history["station_id"].nunique() == 6
assert history.groupby("station_id")["observed_at"].nunique().eq(2160).all()
split = chronological_split(history)
assert split.train["target_at"].max() < split.validation["target_at"].min()
assert split.validation["target_at"].max() < split.test["target_at"].min()
supervised = build_supervised(history)
assert supervised["horizon_h"].between(1, 24).all()
assert (supervised["source_max_at"] <= supervised["origin_at"]).all()
```

Also reject duplicate station/hour, timezone-naive timestamp, gaps, nonfinite values, busy outside capacity and load outside rated power.

- [ ] **Step 3: Run and verify failure**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests/test_repository_features.py -v`

Expected: `ModuleNotFoundError` or missing functions.

- [ ] **Step 4: Implement loader, split and supervised rows**

CSV columns are `station_id,observed_at,pile_count,rated_power_kw,temperature_c,is_holiday,busy_count,load_kw`. Use timezone-aware pandas timestamps. Build horizons by joining future station/timestamp rows; record `source_max_at` from lag/rolling source timestamps.

- [ ] **Step 5: Run through the repository module path and pass**

The editable install uses `--break-system-packages` only to register the project's own `evml` package on the APT-managed system Python; it must never install third-party dependencies (NumPy/pandas/scikit-learn come from Ubuntu's system packages). Run:

```bash
PYTHONPATH=ml/src python3 -m pytest ml/tests/test_repository_features.py -v
```

Expected: PASS with a 62/14/14-day split.

- [ ] **Step 6: Commit**

```bash
git add ml
git commit -m "feat(ml): validate history and build chronological samples"
```

### Task 2: Leakage-safe features, seasonal baseline, and metrics

**Files:**
- Modify: `ml/src/evml/features.py`
- Create: `ml/src/evml/baseline.py`
- Create: `ml/src/evml/metrics.py`
- Create: `ml/tests/test_baseline_metrics.py`

**Interfaces:**
- Consumes: supervised rows from Task 1.
- Produces: feature matrix, baseline predictions and finite MAE/WAPE metric rows.

- [ ] **Step 1: Write failing feature/baseline tests**

Require columns:

```text
station_id, origin_at, target_at, horizon_h, pile_count, rated_power_kw,
target_hour_sin, target_hour_cos, target_dow_sin, target_dow_cos,
target_is_weekend, target_is_holiday, target_temperature_c,
load_lag_1, load_lag_24, load_roll_6, load_roll_24,
busy_lag_1, busy_lag_24, busy_roll_6, busy_roll_24,
seasonal_load_kw, seasonal_busy_count, target_load_kw, target_busy_count,
source_max_at
```

For a daily repeating fixture, seasonal prediction must equal truth and MAE/WAPE must be zero. WAPE is `sum(abs(error))/max(sum(abs(actual)),1e-9)`.

- [ ] **Step 2: Run and verify failure**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests/test_baseline_metrics.py -v`

Expected: FAIL.

- [ ] **Step 3: Implement lags/rolls and metrics**

Compute lags per station with `shift`; compute every rolling feature only after `shift(1)`. Target calendar features come from `target_at`. Expose:

```python
predict_seasonal_naive(frame: pd.DataFrame) -> pd.DataFrame
evaluate_predictions(truth, predictions, model, horizons=(1, 6, 24)) -> tuple[MetricRow, ...]
```

Use MAE for both targets and WAPE only for load; busy `wape` is `None`.

- [ ] **Step 4: Run and pass**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests/test_baseline_metrics.py -v`

Expected: PASS and exactly 6 metric rows per evaluated model.

- [ ] **Step 5: Commit**

```bash
git add ml/src/evml ml/tests/test_baseline_metrics.py
git commit -m "feat(ml): add leakage-safe features baseline and metrics"
```

### Task 3: Ridge models and honest champion selection

**Files:**
- Create: `ml/src/evml/ridge.py`
- Create: `ml/tests/test_ridge.py`

**Interfaces:**
- Consumes: train/validation/test matrices and baseline metrics.
- Produces: two fitted pipelines and target-specific champion labels.

- [ ] **Step 1: Write failing fit/selection tests**

```python
load_model = fit_ridge(train, target="load_kw")
busy_model = fit_ridge(train, target="busy_count")
assert np.isfinite(predict_ridge(load_model, validation)).all()
assert choose_champion(ridge_metrics, baseline_metrics, "load_kw") in {
    "ridge", "seasonal_naive"
}
```

For an intentionally bad Ridge metric fixture, assert baseline is chosen; for better Ridge, assert Ridge is chosen. Do not select using test metrics.

- [ ] **Step 2: Run and verify failure**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests/test_ridge.py -v`

Expected: FAIL.

- [ ] **Step 3: Implement the pipelines**

Use `ColumnTransformer` with OneHotEncoder for station ID, StandardScaler for numeric features, and `Ridge(alpha=1.0)`. Fit only train, compare average validation MAE across 1/6/24 to baseline, then evaluate the selected target model once on test.

- [ ] **Step 4: Add quality-report assertions**

On the fixed generated dataset, require finite metrics and write warnings when load WAPE exceeds 15% or busy MAE exceeds 1.0/1.5/2.0 at 1/6/24. These targets appear in the report; champion selection remains the honest fallback rather than falsifying metrics.

- [ ] **Step 5: Run and pass, then commit**

```bash
PYTHONPATH=ml/src python3 -m pytest ml/tests/test_ridge.py -v
git add ml/src/evml/ridge.py ml/tests/test_ridge.py
git commit -m "feat(ml): train Ridge and select honest champions"
```

### Task 4: Build a physically bounded 144-record forecast run

**Files:**
- Create: `ml/src/evml/forecast.py`
- Create: `ml/tests/test_forecast.py`

**Interfaces:**
- Consumes: selected models/baselines, last 24 hours, six station capacities, fixed cutoff.
- Produces: one immutable `ForecastRun` with horizons 1–24.

- [ ] **Step 1: Write failing completeness/bounds tests**

```python
run = build_forecast_run(...)
assert len(run.records) == 144
assert {(r.station_id, r.horizon_h) for r in run.records} == {
    (station_id, h) for station_id in range(1, 7) for h in range(1, 25)
}
for row in run.records:
    capacity = capacities[row.station_id]
    assert 0 <= row.predicted_load_kw <= capacity.rated_power_kw
    assert 0 <= row.predicted_busy_count <= capacity.pile_count
    assert row.predicted_idle_count == capacity.pile_count - row.predicted_busy_count
```

Assert one largest consecutive two-hour peak window per station; congestion is low below 50%, medium from 50% to below 80%, high at/above 80%.

- [ ] **Step 2: Run and verify failure**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests/test_forecast.py -v`

Expected: FAIL.

- [ ] **Step 3: Implement recursive horizon generation and bounds**

Generate horizons in order, using known calendar/simulated temperature and prior predictions only where a later horizon requires an unavailable lag. Clip load, round/clip busy, derive idle exactly, reject nonfinite values, and sort records by station/horizon.

- [ ] **Step 4: Make run identity deterministic**

`run_id` is `forecast-<cutoff compact>-<input hash first 8>`; model version records target champions, e.g. `load-ridge_busy-seasonal-v1`. Same input/cutoff/models produce identical records/run ID. Server-owned `activatedAt` is deliberately absent from run identity and from ML output.

- [ ] **Step 5: Run and pass, then commit**

```bash
PYTHONPATH=ml/src python3 -m pytest ml/tests/test_forecast.py -v
git add ml/src/evml/forecast.py ml/tests/test_forecast.py
git commit -m "feat(ml): produce bounded 24-hour station forecasts"
```

### Task 5: Reproducible pipeline, artifacts, and CLI

**Files:**
- Create: `ml/src/evml/pipeline.py`
- Create: `ml/src/evml/cli.py`
- Create: `ml/tests/test_pipeline_cli.py`
- Create: `ml/README.md`

**Interfaces:**
- Consumes: history CSV, explicit cutoff and output directory.
- Produces: models, metrics, candidate forecast and machine-readable run summary.

- [ ] **Step 1: Write a failing CLI test**

Invoke:

```bash
PYTHONPATH=ml/src python3 -m evml.cli run-all \
  --history ml/tests/fixtures/station_hourly_history.csv \
  --cutoff 2026-09-01T09:00:00+08:00 \
  --output-dir "$tmp_path"
```

Assert exit code 0 and these files exist: `model_load.joblib`, `model_busy.joblib`, `metrics.json`, `forecast_candidate.json`, `run_summary.json`; candidate contains 144 records and metrics include both models/targets at 1/6/24.

- [ ] **Step 2: Run and verify failure**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests/test_pipeline_cli.py -v`

Expected: FAIL because CLI is absent.

- [ ] **Step 3: Implement atomic artifact writes**

```python
run_pipeline(history_path: Path, cutoff: pd.Timestamp,
             output_dir: Path, seed: int = 20260901) -> ForecastRun
```

Write to sibling temp files, flush/fsync and `os.replace`; JSON uses UTF-8, `ensure_ascii=False`, `sort_keys=True`. No call uses current time unless supplied explicitly as `generated_at`. `metrics.json`/`run_summary.json` contain evaluation metrics; `forecast_candidate.json` contains only publishable run metadata and 144 records, not metrics.

`ml/src/evml/cli.py` exposes `main(...)` and ends with a module entry guard equivalent to `if __name__ == "__main__": raise SystemExit(main())`, so the required `python3 -m evml.cli ...` invocation executes the CLI without an installed console script.

- [ ] **Step 4: Run all non-network ML tests**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests -k 'not publisher' -v`

Expected: PASS and repeated fixed runs have identical metrics/forecast content.

- [ ] **Step 5: Document and commit**

README explains simulated data, split, features, baseline/Ridge choice, MAE/WAPE, target thresholds, artifacts and exact command. Then:

```bash
git add ml
git commit -m "feat(ml): add reproducible training and forecast CLI"
```

### Task 6: Framed forecast publishing and last-known-good

**Files:**
- Create: `ml/src/evml/protocol.py`
- Create: `ml/src/evml/publisher.py`
- Create: `ml/src/evml/last_good.py`
- Create: `ml/tests/test_protocol_publisher.py`
- Modify: `ml/src/evml/cli.py`

**Interfaces:**
- Consumes: one `ForecastRun`, host/port/service token.
- Produces: v1 `forecast.publish`, validated standard response and the canonical replay artifact `runtime/ml/forecast_last_good.json` only after `acceptedCount == 144`.

- [ ] **Step 1: Write failing framing tests**

Assert `encode_frame(message)[:4] == struct.pack('>I', len(body))`; `recv_frame` handles split reads, rejects zero/over-1MiB length and invalid JSON. Derive request ID deterministically from `runId` (for example `publish-<runId>`) so retries are idempotent. The request envelope is exactly:

```json
{"version":1,"requestId":"forecast-...","action":"forecast.publish","token":"...","payload":{"runId":"...","generatedAt":"...","dataCutoff":"...","modelVersion":"...","records":[]}}
```

- [ ] **Step 2: Write failing fake-server/last-good tests**

Fake server returns standard response with `ok:true`, `code:"OK"`, and `data:{runId:"...",acceptedCount:144,snapshotReady:true|false}`. Assert last-good is atomically replaced only for a matching run ID/count; `snapshotReady=false` is reported as a warning because DB commit is authoritative. Connection failure, wrong run ID, rejected response or accepted count ≠144 leaves pre-existing bytes unchanged. A local last-good write failure makes the CLI exit nonzero; republishing the identical payload/request ID receives an idempotent ACK and can repair the file.

- [ ] **Step 3: Run and verify failure**

Run: `PYTHONPATH=ml/src python3 -m pytest ml/tests/test_protocol_publisher.py -v`

Expected: FAIL.

- [ ] **Step 4: Implement exact publish API**

```python
publish_forecast(host: str, port: int, token: str,
                 run: ForecastRun, timeout_s: float = 3.0) -> PublishReceipt
save_last_good(path: Path, publish_payload: dict[str, Any]) -> None
```

Use `socket.create_connection`, `sendall`, exact-read loop and three-second timeout. Never log token. The last-good file is a replayable publish payload, not a second database authority; the server never reads it silently. CLI `publish` exits nonzero on network/ACK/local-write failure and prints run ID, accepted count and snapshot readiness on success.

- [ ] **Step 5: Run full ML verification and live publish**

```bash
PYTHONPATH=ml/src python3 -m pytest ml/tests -v
PYTHONPATH=ml/src python3 -m evml.cli run-all --history runtime/ml/station_hourly_history.csv \
  --cutoff "$DEMO_FORECAST_ORIGIN" --generated-at "$DEMO_MODEL_GENERATED_AT" \
  --output-dir runtime/ml/artifacts
PYTHONPATH=ml/src python3 -m evml.cli publish --candidate runtime/ml/artifacts/forecast_candidate.json \
  --host 127.0.0.1 --port 9100 --last-good runtime/ml/forecast_last_good.json
```

`DEMO_FORECAST_ORIGIN` and `DEMO_MODEL_GENERATED_AT` are project-specific release values recorded in config; do not run the live command until they are set. Expected: tests PASS; server reports accepted count 144; last-good, `forecast.latest`, SQLite active run and dashboard converge to the same run ID.

- [ ] **Step 6: Commit**

```bash
git add ml
git commit -m "feat(ml): publish forecasts with last-known-good fallback"
```
