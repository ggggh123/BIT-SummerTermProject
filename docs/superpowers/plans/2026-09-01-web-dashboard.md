# Web ECharts Dashboard Implementation Plan

> **状态：可选参考计划。** 2026-09-04 起，Web 大屏不纳入 core acceptance 或 core release；本计划的 checkbox、测试记录和既有成果均保留，只有在显式启用 Web optional profile 时才作为独立参考执行。当前范围见 [`2026-09-04-core-scope-rebaseline.md`](2026-09-04-core-scope-rebaseline.md)。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a polished single-page ECharts operations dashboard that displays current KPIs, station/charger health, trends, events and ML forecasts from the server's atomic JSON snapshot.

**Architecture:** The browser loads only static local assets and polls same-origin `runtime/dashboard_snapshot.json` every 2 seconds. Pure validation/model functions reject an incomplete snapshot before rendering; the UI keeps the last successful data and marks it stale instead of clearing or partially updating charts.

**Tech Stack:** Semantic HTML, CSS, vanilla ES modules, locally vendored Apache ECharts 5.6.0, Node.js built-in `node:test`, Python static HTTP server.

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- Web is read-only and never opens SQLite or sends business mutations.
- All assets required for the demo, including ECharts, are local; runtime does not depend on a CDN.
- Poll `dashboard/runtime/dashboard_snapshot.json` every 2000 ms with cache bypass.
- Render a snapshot only after complete v1 validation; do not mix values from two generations.
- Preserve last successful values on fetch/parse/validation failure and show generated time plus a visible stale/error banner.
- Forecast older than 2 hours is visibly labeled expired; dashboard data older than 10 seconds is visibly disconnected.
- One responsive page only; no login, CRUD, hidden navigation or fake map tiles.

---

## Planned File Map

- `dashboard/index.html` — one-page semantic layout.
- `dashboard/assets/styles.css` — responsive presentation.
- `dashboard/assets/contracts.js` — snapshot validation and formatters.
- `dashboard/assets/models.js` — ECharts option builders.
- `dashboard/assets/dashboard.js` — polling, rendering and lifecycle.
- `dashboard/vendor/echarts.min.js` — pinned local runtime.
- `dashboard/runtime/dashboard_snapshot.json` — generated runtime file, ignored by Git.
- `dashboard/fallback/dashboard_snapshot.json` — explicit cached demo snapshot.
- `dashboard/tests/*.test.mjs` — pure Node tests.
- `dashboard/tests/fixtures/*.json` — frozen valid/invalid samples.
- `dashboard/README.md` — run and failure behavior.

### Task 1: Vendor ECharts and freeze the snapshot fixture

**Files:**
- Create: `scripts/vendor_dashboard_assets.sh`
- Create: `dashboard/vendor/echarts.min.js`
- Create: `dashboard/tests/fixtures/valid_snapshot.json`
- Create: `dashboard/tests/fixtures/invalid_snapshot.json`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: ECharts npm package `5.6.0` and design section 9.
- Produces: local ECharts runtime and canonical JSON fixture used by server and Web tests.

- [x] **Step 1: Write the vendor script**

```bash
#!/usr/bin/env bash
set -euo pipefail
asset_cache="dashboard/.vendor-cache"
npm install --prefix "$asset_cache" --no-save echarts@5.6.0
cp "$asset_cache/node_modules/echarts/dist/echarts.min.js" dashboard/vendor/echarts.min.js
test -s dashboard/vendor/echarts.min.js
```

- [x] **Step 2: Create the exact valid fixture**

The fixture uses the reset state of 6 forecast-enabled stations, 48 chargers, 7 revenue points, 144 actual-load points, 144 forecast points, at least 4 recent events and these top-level keys only:

```text
schemaVersion, snapshotVersion, generatedAt, kpis, revenue7d, actualLoad24h,
chargerStatus, stationRanking, stations, events, forecastRun, forecast24h
```

`invalid_snapshot.json` is the same fixture with `kpis.todayRevenueFen` removed.

- [x] **Step 3: Vendor and verify no remote ECharts reference**

Run:

```bash
bash scripts/vendor_dashboard_assets.sh
test -s dashboard/vendor/echarts.min.js
```

Expected: local file exists. Add `dashboard/.vendor-cache/` to `.gitignore` but commit `dashboard/vendor/echarts.min.js`.

- [x] **Step 4: Commit**

```bash
git add scripts/vendor_dashboard_assets.sh dashboard/vendor dashboard/tests/fixtures .gitignore
git commit -m "build(web): vendor ECharts and freeze snapshot fixture"
```

### Task 2: Snapshot validation and pure formatters

**Files:**
- Create: `dashboard/assets/contracts.js`
- Create: `dashboard/tests/contracts.test.mjs`

**Interfaces:**
- Consumes: untrusted parsed JSON.
- Produces: validated v1 snapshot or an error containing the exact field path.

- [x] **Step 1: Write failing Node tests**

```javascript
test('accepts the frozen valid snapshot', () => {
  assert.doesNotThrow(() => validateSnapshot(valid));
});
test('reports the missing field path', () => {
  assert.throws(() => validateSnapshot(invalid), /kpis\.todayRevenueFen/);
});
test('formats values consistently', () => {
  assert.equal(formatFen(123456), '¥1,234.56');
  assert.equal(formatPercent(87.5), '87.5%');
  assert.equal(formatKwh(12.345), '12.35 kWh');
});
```

Also reject `schemaVersion != 1`, non-positive/integer `snapshotVersion`, non-ISO generatedAt, revenue length other than 7, actual load length other than 144, forecast length other than 0 or 144, a status sum unequal to `chargerStatus.total`, unknown status/congestion, NaN/nonfinite numbers and predicted busy+idle not equal the matching forecast-enabled station capacity. Require `forecastRun=null` exactly when `forecast24h=[]`; otherwise require `activatedAt`, canonical lowercase `payloadHash` and all run fields.

- [x] **Step 2: Run and verify failure**

Run: `node --test dashboard/tests/contracts.test.mjs`

Expected: FAIL because functions are absent.

- [x] **Step 3: Implement without DOM dependencies**

Export exactly:

```javascript
export function validateSnapshot(value) {}
export function formatFen(value) {}
export function formatPercent(value) {}
export function formatKwh(value) {}
export function isDashboardStale(lastSuccessfulFetchAt, now, thresholdMs = 10000) {}
export function isForecastStale(activatedAt, now, thresholdMs = 7200000) {}
```

Each validator throws `Error('snapshot.<field path>: <reason>')`; it does not coerce strings into numbers.

- [x] **Step 4: Run and pass**

Run: `node --test dashboard/tests/contracts.test.mjs`

Expected: PASS.

- [x] **Step 5: Commit**

```bash
git add dashboard/assets/contracts.js dashboard/tests/contracts.test.mjs
git commit -m "feat(web): validate atomic dashboard snapshot contract"
```

### Task 3: Build ECharts models from one snapshot

**Files:**
- Create: `dashboard/assets/models.js`
- Create: `dashboard/tests/models.test.mjs`

**Interfaces:**
- Consumes: a validated snapshot.
- Produces: deterministic option objects for revenue, load+forecast, status, station ranking and station distribution.

- [ ] **Step 1: Write failing model tests**

Assert: revenue has 7 labels; a forecast-enabled seeded station has 24 historical actual-load points and 24 future forecast points; a newly created `forecastEnabled=false` station returns an explicit no-forecast model instead of throwing; status order is `idle,reserved,charging,fault,restarting`; ranking is descending by utilization; station scatter uses longitude/latitude and includes name/idle/total/revenue/forecast availability in tooltip data; peak forecast points are marked; no model contains `undefined` or nonfinite numbers.

- [ ] **Step 2: Run and verify failure**

Run: `node --test dashboard/tests/models.test.mjs`

Expected: FAIL.

- [ ] **Step 3: Implement exact builders**

```javascript
export function buildRevenueOption(snapshot) {}
export function buildLoadForecastOption(snapshot, stationId) {}
export function buildStatusOption(snapshot) {}
export function buildRankingOption(snapshot) {}
export function buildStationOption(snapshot) {}
```

Use Chinese labels and explicit units (`元`, `kW`, `%`, `个`). The option objects contain data only; no DOM access or ECharts instance creation.

- [ ] **Step 4: Run and pass**

Run: `node --test dashboard/tests/models.test.mjs`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add dashboard/assets/models.js dashboard/tests/models.test.mjs
git commit -m "feat(web): build deterministic ECharts data models"
```

### Task 4: Implement the polished one-page layout

**Files:**
- Create: `dashboard/index.html`
- Create: `dashboard/assets/styles.css`
- Create: `dashboard/assets/dashboard.js`

**Interfaces:**
- Consumes: local ECharts, formatter/model functions and a validated snapshot.
- Produces: visible KPI, charts, station detail, events, update time and connection status.

- [ ] **Step 1: Write the semantic page skeleton**

Include IDs: `connection-state`, `generated-at`, four KPI values, `station-map`, `load-chart`, `revenue-chart`, `status-chart`, `ranking-chart`, `event-list`, `station-detail`. Load `vendor/echarts.min.js` and `assets/dashboard.js`; no remote scripts/styles/images.

- [ ] **Step 2: Implement responsive CSS**

At ≥1200 px use a wide operations layout; below 800 px stack panels. Maintain readable Chinese text, visible focus, chart minimum heights and non-color status labels. Do not hide any KPI or chart on narrow screens.

- [ ] **Step 3: Implement chart lifecycle**

Create one instance per container, call option builders in `renderSnapshot(snapshot)`, replace events as text nodes (never `innerHTML` from snapshot), update selected station on chart click, and debounce resize by 150 ms. Re-render all components from the same snapshot object only.

- [ ] **Step 4: Run a static-fixture visual smoke**

```bash
mkdir -p dashboard/runtime
cp dashboard/tests/fixtures/valid_snapshot.json dashboard/runtime/dashboard_snapshot.json
python3 -m http.server 8080 --directory dashboard
```

Open `http://127.0.0.1:8080`; expected: all four KPI values, five chart regions, station detail, events and generated time are visible with no console error at 1920×1080 and 1366×768.

- [ ] **Step 5: Commit**

```bash
git add dashboard/index.html dashboard/assets
git commit -m "feat(web): render single-page ECharts operations dashboard"
```

### Task 5: Polling, stale-state and cached fallback

**Files:**
- Modify: `dashboard/assets/dashboard.js`
- Create: `dashboard/assets/poller.js`
- Create: `dashboard/fallback/dashboard_snapshot.json`
- Create: `dashboard/tests/poller.test.mjs`

**Interfaces:**
- Consumes: live and fallback URLs.
- Produces: one atomic chart transition per valid new `snapshotVersion`, plus heartbeat/fetch-status updates without unnecessary redraw.

- [ ] **Step 1: Write failing polling tests**

Using an injected `fetchImpl`, assert: initial load happens immediately; equal `snapshotVersion` with newer `generatedAt` refreshes the heartbeat/status but does not redraw charts; a new version redraws exactly once even when timestamps share the same second; HTTP error, malformed JSON or invalid contract preserves the previous snapshot; when no previous snapshot exists, fallback loads and `source == 'cached'`; forecast staleness uses `activatedAt` and dashboard connection staleness uses the last successful fetch time.

- [ ] **Step 2: Run and verify failure**

Run: `node --test dashboard/tests/poller.test.mjs`

Expected: FAIL.

- [ ] **Step 3: Implement the poller**

```javascript
export function createPoller({fetchImpl, liveUrl, fallbackUrl, intervalMs, onData, onStatus}) {}
```

Use `fetch(liveUrl + '?ts=' + Date.now(), {cache:'no-store'})`; validate before any state change; keep `lastSnapshot`, `lastSnapshotVersion` and `lastSuccessfulFetchAt`. Call `onData` only for a new version, but call `onStatus` for valid heartbeats. Expose statuses `live|stale|cached|error`. The page starts it with interval `2000`.

- [ ] **Step 4: Verify failure/recovery manually**

Start with valid live data, replace live JSON with malformed content, verify charts remain and error banner appears, restore a newer valid file, verify banner clears and all components advance together. During final release, regenerate `dashboard/fallback/dashboard_snapshot.json` from the approved golden run (without claiming it is live); the poller supplies the visible `cached` label.

- [ ] **Step 5: Run and commit**

```bash
node --test dashboard/tests/*.test.mjs
git add dashboard
git commit -m "feat(web): add atomic polling and explicit cached fallback"
```

### Task 6: Server-contract integration and delivery verification

**Files:**
- Create: `dashboard/tests/server-fixture.test.mjs`
- Create: `dashboard/README.md`
- Modify: `apps/admin-server/tests/tst_forecast_snapshot.cpp`

**Interfaces:**
- Consumes: actual snapshot generated by `SnapshotWriter`.
- Produces: one shared fixture contract and a demo-ready dashboard.

- [ ] **Step 1: Add the cross-subsystem contract test**

Admin/server test writes a fresh snapshot to a temp path. A small CTest wrapper then executes:

```bash
node --test dashboard/tests/server-fixture.test.mjs -- /absolute/path/to/generated.json
```

The Node test imports `validateSnapshot`, verifies the reset fixture's 144/144 forecast-series lengths, positive version and KPI values, and verifies all status counts against dynamic `chargerStatus.total`. A second server-generated fixture creates a non-predicted station and chargers, then proves validation still passes while the ML arrays remain 144.

- [ ] **Step 2: Run and fix only field-contract mismatches**

Run:

```bash
cmake --build --preset debug --target tst_forecast_snapshot
ctest --preset debug -R forecast_snapshot --output-on-failure
node --test dashboard/tests/*.test.mjs
```

Expected: PASS with identical camelCase field names.

- [ ] **Step 3: Document exact runtime behavior**

README includes local start command, snapshot path/owner, 2-second polling, all visible charts, fallback wording, cache behavior, supported resolutions and the rule that the Web app never writes business data. The release check must prove the fallback snapshot carries the same approved forecast run/payload as the final golden DB while the UI still labels its source `cached`.

- [ ] **Step 4: Execute final Web smoke**

With server, simulator and Web running: start one charge, observe charging count/event/load change within one poll; stop/settle, observe revenue and order event; publish a forecast, observe run time/curve/high-peak mark. Disconnect server and confirm old data remains with a visible warning.

- [ ] **Step 5: Commit**

```bash
git add dashboard apps/admin-server/tests
git commit -m "test(web): verify live snapshot and dashboard degradation"
```
