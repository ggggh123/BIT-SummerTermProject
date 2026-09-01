# Five-System Integration, Demo, and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide safe reset/start/stop/smoke/rehearsal commands, explicit fallbacks, formal role evidence, and an objective release gate for the complete five-system demonstration.

**Architecture:** A small Python `democtl` package orchestrates only known workspace processes and validates TCP, HTTP, files and hashes. Shell scripts are stable entry points. The golden SQLite DB is runtime authority; the last-good publish payload and cached dashboard snapshot are explicit operator recovery artifacts, never silent competing authorities. Two successful rehearsals from the same commit are required before `v1.0-demo`.

**Tech Stack:** Bash, Python 3 standard library/pytest, SQLite, Qt offscreen smoke, HTTP static server, shared TCP JSON protocol, SHA-256, Git.

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- 2026-09-08 12:00 is feature freeze; 2026-09-09 12:00 is code freeze; 2026-09-10 is demo-only.
- Reset may replace only validated files below workspace `runtime/`; it must never target a directory or path outside the workspace.
- Golden artifacts are immutable during a run and verified by SHA-256 before copying.
- A fallback is visibly labeled cached/offline/stale and never presented as live data.
- Tencent map live API remains P0; cached route evidence is only an external-outage recovery aid.
- ML is a batch subsystem, not a required long-running process; its artifacts and last publish receipt prove health.
- Release requires zero open P0 defects and two distinct passing rehearsals on the same Git commit.

---

## Planned File Map

- `ops/democtl.py` — reset/start/stop/status commands.
- `ops/protocol.py` — standard-library v1 TCP health/client helpers.
- `ops/smoke.py` — subsystem and consistency checks.
- `ops/rehearsal.py` — deterministic business-flow exercise/report.
- `ops/release.py` — artifact manifest and release decision.
- `scripts/reset_demo.sh`, `start_demo.sh`, `stop_demo.sh`, `smoke_test.sh`, `rehearse_demo.sh`, `release_check.sh` — stable operator entry points.
- `tests/e2e/*` — controller, smoke, rehearsal, fallback and release tests.
- `docs/management/*`, `docs/review/*`, `docs/test/*`, `docs/release/*` — formal process and demo evidence.

### Task 1: Safe golden reset and process manifest

**Files:**
- Create: `ops/__init__.py`
- Create: `ops/democtl.py`
- Create: `scripts/reset_demo.sh`
- Create: `scripts/start_demo.sh`
- Create: `scripts/stop_demo.sh`
- Create: `tests/e2e/test_democtl.py`
- Create: `tests/e2e/test_shell_syntax.py`

**Interfaces:**
- Consumes: `runtime/golden/demo.db`, its hash, built binaries and local config.
- Produces: `runtime/demo.db`, `runtime/processes.json`, known log files and safe process lifecycle.

- [ ] **Step 1: Write failing path-safety and process tests**

Assert reset refuses `/`, workspace root, directories, symlinks escaping runtime and a source hash mismatch. It also refuses when the manifest contains a live matching server PID or a framed `system.health` succeeds on configured port 9100. Only a stopped-server reset copies through `runtime/.demo.db.tmp` then `os.replace`. Start records only child PIDs it created; stop signals only PIDs whose command and workspace match the manifest and never touches an unrelated PID.

- [ ] **Step 2: Write shell syntax tests**

```python
@pytest.mark.parametrize("script", [
    "scripts/reset_demo.sh", "scripts/start_demo.sh", "scripts/stop_demo.sh",
])
def test_bash_syntax(script):
    subprocess.run(["bash", "-n", script], check=True)
```

- [ ] **Step 3: Run and verify failure**

Run: `.venv/bin/pytest tests/e2e/test_democtl.py tests/e2e/test_shell_syntax.py -v`

Expected: FAIL because controller/scripts are absent.

- [ ] **Step 4: Implement exact process order**

`start` launches and logs:

```text
1. build/debug/apps/admin-server/ev_admin_server --db runtime/demo.db --host 127.0.0.1 --port 9100 --snapshot dashboard/runtime/dashboard_snapshot.json
2. build/debug/simulator/ev_charger_simulator --host 127.0.0.1 --port 9100 --seed 20260901 --interval-ms 3000
3. python3 -m http.server 8080 --bind 127.0.0.1 --directory dashboard
4. build/debug/apps/user-client/ev_user_client --host 127.0.0.1 --port 9100
```

Wait for TCP health before simulator/user; wait for HTTP 200 before reporting ready. Write each PID, executable, arguments, start time and log path to `runtime/processes.json`. Stop sends SIGTERM, waits 5 seconds, then reports a still-live process instead of killing unrelated targets.

- [ ] **Step 5: Add stable shell wrappers**

Each script resolves workspace root from its own path and executes `.venv/bin/python -m ops.democtl <command>`. No wrapper contains wildcard deletion or unresolved destructive path. `reset_demo.sh` is explicitly labeled **offline reset**; runtime reset is the admin-only `demo.reset` action and uses no file replacement.

- [ ] **Step 6: Run and pass, then commit**

```bash
.venv/bin/pytest tests/e2e/test_democtl.py tests/e2e/test_shell_syntax.py -v
git add ops scripts tests/e2e
git commit -m "build(demo): add safe reset and process controller"
```

### Task 2: Cross-system smoke and snapshot consistency

**Files:**
- Create: `ops/protocol.py`
- Create: `ops/smoke.py`
- Create: `scripts/smoke_test.sh`
- Create: `tests/e2e/test_smoke.py`
- Create: `tests/e2e/test_snapshot_consistency.py`

**Interfaces:**
- Consumes: server health response, HTTP dashboard, snapshot, ML artifacts and process manifest.
- Produces: one machine-readable `runtime/reports/smoke.json` and exit 0/1.

- [ ] **Step 1: Write failing smoke tests with fake endpoints/files**

Checks must fail independently for: server TCP unavailable/degraded; simulator/user PID missing; dashboard HTTP unavailable; snapshot invalid/stale; non-positive or mismatched snapshot version; golden DB integrity/hash mismatch; missing ML metrics/model/last-good; latest forecast count not 144; configured presentation time outside the 24-hour forecast window; or any disagreement among last-good run/payload hash, SQLite active run, `forecast.latest`, `system.health` and snapshot.

- [ ] **Step 2: Write standard-library protocol tests**

`ops.protocol.request()` must produce the exact v1 envelope/frame, handle split receive, cap at 1 MiB and validate standard response fields. Test against a fake socket.

- [ ] **Step 3: Run and verify failure**

Run: `.venv/bin/pytest tests/e2e/test_smoke.py tests/e2e/test_snapshot_consistency.py -v`

Expected: FAIL.

- [ ] **Step 4: Implement smoke checks**

`run_smoke(config) -> SmokeReport` reads manifest, confirms recorded commands, sends `system.health` and `forecast.latest`, loads/validates snapshot, fetches Web `index.html` and snapshot, opens SQLite read-only for `PRAGMA integrity_check` plus active run/version, and validates ML artifact JSON. Poll snapshot retry/heartbeat for at most 10 seconds. Require `last-good runId/hash = SQLite active run/hash = forecast.latest = health.forecastRunId = snapshot.forecastRun.runId` and `health.snapshotVersion = DB snapshot_meta.version = snapshot.snapshotVersion`.

- [ ] **Step 5: Verify real startup**

```bash
scripts/reset_demo.sh
scripts/start_demo.sh
scripts/smoke_test.sh
scripts/stop_demo.sh
```

Expected final line: `PASS: server, simulator, user, web and ML artifacts are healthy; snapshot consistent`.

- [ ] **Step 6: Commit**

```bash
git add ops scripts/smoke_test.sh tests/e2e
git commit -m "test(demo): add five-system smoke and consistency checks"
```

### Task 3: Explicit map, Web and ML degradation tests

**Files:**
- Create: `tests/e2e/test_degradation.py`
- Create: `docs/test/degradation-cases.md`
- Modify: `apps/user-client/src/ui/NavigationPage.cpp`
- Modify: `dashboard/assets/poller.js`
- Modify: `apps/admin-server/src/services/ForecastService.cpp`

**Interfaces:**
- Consumes: forced external/TCP/file failures.
- Produces: visible, truthful fallback state while last successful business data remains usable.

- [ ] **Step 1: Write failing degradation assertions**

Cases:

```text
Tencent request timeout -> user shows MAP_API_ERROR + Retry; station list remains usable
live snapshot malformed -> Web keeps previous charts + status error
no live snapshot on first load -> Web loads fallback + status cached
forecast publish interrupted before commit -> previous active run and last-good bytes unchanged
ACK lost after DB commit -> identical republish gets idempotent ACK and four-way run IDs converge
snapshot write failure after DB commit -> old file remains, retry publishes committed version
forecast activatedAt age >2h -> user/admin/Web show expired, records remain readable
server disconnect -> user/simulator show disconnected and do not replay mutations
```

- [ ] **Step 2: Run and verify failure**

Run: `.venv/bin/pytest tests/e2e/test_degradation.py -v`

Expected: at least one behavior or test hook is missing.

- [ ] **Step 3: Add deterministic failure injection**

Support only test/demo configuration flags `DEMO_FORCE_MAP_OFFLINE`, `DEMO_FORCE_SNAPSHOT_ERROR`, `DEMO_FORCE_FORECAST_STALE`. They affect visible status but never fabricate a successful live call. Do not expose them as normal product controls.

- [ ] **Step 4: Run module and degradation suites**

```bash
ctest --preset debug -R "user_tencentmap|forecast_snapshot" --output-on-failure
node --test dashboard/tests/*.test.mjs
.venv/bin/pytest tests/e2e/test_degradation.py -v
```

Expected: PASS.

- [ ] **Step 5: Document and commit**

`degradation-cases.md` records trigger, expected visible wording, preserved data and recovery step for each case.

```bash
git add apps dashboard tests/e2e docs/test/degradation-cases.md
git commit -m "test(demo): verify truthful external-service degradation"
```

### Task 4: Automated business rehearsal and eight-minute runbook

**Files:**
- Create: `ops/rehearsal.py`
- Create: `scripts/rehearse_demo.sh`
- Create: `tests/e2e/test_rehearsal.py`
- Create: `docs/release/demo-runbook.md`

**Interfaces:**
- Consumes: a freshly reset running system and fixed service credentials.
- Produces: JSON rehearsal report and a manual presenter script matching the same state changes.

- [ ] **Step 1: Write failing ordered-flow tests**

The rehearsal script first stops recorded processes, performs the verified **offline** reset, starts the system, waits for ready/snapshot consistency, and then the automated client executes exactly:

```text
health -> user login -> recharge -> station list -> reserve -> start
-> three telemetry acknowledgements -> stop -> settle
-> admin dashboard -> user freeze -> rejected reserve -> unfreeze
-> fault set -> restart -> forecast publish/latest -> snapshot consistency
```

Assert: charging count increases after start, returns after settlement; balance decreases by exact amount; revenue increases; event stream includes charge and restart; last-good/SQLite/user/admin/Web use one forecast run ID; snapshot versions advance after mutations; any failed assertion makes report `passed:false`. The two formal release rehearsals use only this offline reset path; online `demo.reset` is tested separately and shown only if time remains.

- [ ] **Step 2: Run and verify failure**

Run: `.venv/bin/pytest tests/e2e/test_rehearsal.py -v`

Expected: FAIL.

- [ ] **Step 3: Implement report content**

`run_rehearsal` records run ID, Git commit, golden hash, start/end, every request ID/result, before/after KPIs, forecast run, failures and overall status. It does not modify SQLite directly.

- [ ] **Step 4: Write the manual eight-minute runbook**

Use the approved speakers/times, fixed phone/address/station/charger, expected UI labels, exact clicks, reset/start commands and fallback paths. Include a “do not improvise” list: no live code edits, no manual DB updates, no new model training if the frozen candidate already passed.

- [ ] **Step 5: Run one rehearsal and commit**

```bash
scripts/rehearse_demo.sh --run-id rehearsal-dry-run \
  --report runtime/reports/rehearsal-dry-run.json
git add ops/rehearsal.py scripts/rehearse_demo.sh tests/e2e/test_rehearsal.py docs/release/demo-runbook.md
git commit -m "test(demo): automate and document the full rehearsal"
```

Expected: report contains `"passed": true`.

### Task 5: Formal role evidence and project documentation

**Files:**
- Create: `docs/management/project-plan.md`
- Create: `docs/management/daily-log.md`
- Create: `docs/management/risk-register.md`
- Create: `docs/review/review-checklist.md`
- Create: `docs/review/review-log.md`
- Create: `docs/test/test-cases.md`
- Create: `docs/test/test-report.md`
- Create: `docs/release/configuration-items.md`
- Create: `docs/release/deployment-guide.md`
- Create: `docs/release/user-guide.md`
- Create: `docs/release/presentation-outline.md`

**Interfaces:**
- Consumes: actual commits, test/rehearsal reports and approved design.
- Produces: concise evidence for PM/TL/PRL/SCML/PE responsibilities.

- [ ] **Step 1: Create evidence-backed templates**

Each record row contains date, participant/owner, artifact or commit, decision/result and follow-up. Do not backfill fictitious meetings. The review checklist covers requirement, protocol, state/money/time, error UI, tests and secret leakage.

- [ ] **Step 2: Populate project plan and risk register from approved facts**

Include the Sep 1–10 gates, five module owners, Key/network/Qt install/TCP/schema/ML/demo risks and named mitigation. Link architecture and implementation plan files.

- [ ] **Step 3: Generate test and configuration evidence from commands**

`test-report.md` records command, commit, time, pass/fail count and report path. `configuration-items.md` lists source, schema, protocol, seed, model, golden hash, config example, binaries, Web assets and docs with owner/version.

- [ ] **Step 4: Write deployment/user/presentation documents**

Deployment guide follows bootstrap→build→golden→reset→start→smoke→stop. User guide covers both Qt clients, simulator, Web and ML CLI. Presentation outline assigns the approved eight-minute segments and likely technical questions to the appropriate formal role.

- [ ] **Step 5: PRL review and commit**

#3 checks every factual claim against a runnable artifact/report; #4 checks paths/versions; #1 approves timing; #2 approves technical wording; #5 approves metrics.

```bash
git add docs/management docs/review docs/test docs/release
git commit -m "docs: add role evidence test and release documentation"
```

### Task 6: Objective release manifest and two-rehearsal gate

**Files:**
- Create: `ops/release.py`
- Create: `scripts/release_check.sh`
- Create: `tests/e2e/test_release_gate.py`
- Create: `docs/release/release-checklist.md`

**Interfaces:**
- Consumes: current Git commit, tests, golden hash, ML artifacts, snapshot, docs and two rehearsal reports.
- Produces: `runtime/reports/release-manifest.json`, `GO` or `NO-GO`, and `v1.0-demo` only on GO.

- [ ] **Step 1: Write failing release-gate tests**

Reject: one rehearsal; duplicate run IDs; different commits; any `passed:false`; changed golden hash; missing system artifact; forecast count !=144; missing 1/6/24 metric; stale/missing snapshot; open P0 defect; absent runbook/config item/test report.

- [ ] **Step 2: Run and verify failure**

Run: `.venv/bin/pytest tests/e2e/test_release_gate.py -v`

Expected: FAIL.

- [ ] **Step 3: Implement manifest and decision**

Hash all required artifacts, require a clean tracked worktree, validate two distinct reports against an explicitly captured release commit, and require both after code-freeze timestamp. Validate final golden hash, one active/144 records, presentation-window coverage, ML metrics, last-good payload hash, live four-way run ID convergence, cached fallback run/hash equality, snapshot version and all formal documents. Print every failed condition on NO-GO; do not silently waive it.

Complete `docs/release/release-checklist.md`, configuration records and all source changes before rehearsals, then commit them. Capture `release_commit=$(git rev-parse HEAD)` only after `git status --porcelain` is empty. Rehearsal reports and release manifest remain under `runtime/reports/` and are not committed after GO.

- [ ] **Step 4: Execute final gate**

```bash
release_commit="$(git rev-parse HEAD)"
cmake --build --preset release
ctest --preset release --output-on-failure
node --test dashboard/tests/*.test.mjs
.venv/bin/pytest database/tests ml/tests tests/e2e -v
scripts/rehearse_demo.sh --run-id rehearsal-1 --report runtime/reports/rehearsal-1.json
scripts/rehearse_demo.sh --run-id rehearsal-2 --report runtime/reports/rehearsal-2.json
scripts/release_check.sh --commit "$release_commit"
```

Expected final line: `GO: all tests and two same-commit rehearsals passed`.

- [ ] **Step 5: Tag the exact rehearsed commit; make no post-GO commit**

```bash
test "$(git rev-parse HEAD)" = "$release_commit"
test -z "$(git status --porcelain)"
git tag -a v1.0-demo "$release_commit" -m "Validated September 10 demo release"
```

If either test fails, the result is NO-GO and both formal rehearsals must be rerun on the new commit. Tagging does not modify the rehearsed commit.
