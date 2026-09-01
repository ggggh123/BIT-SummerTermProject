# Platform Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the reproducible Ubuntu toolchain, Monorepo skeleton, shared data constants, and tested length-prefixed JSON protocol that all five systems use.

**Architecture:** One CMake workspace builds the three Qt/C++ executables and shared libraries. `ev_contracts` owns action/status strings and `ev_protocol` owns request/response envelopes plus the 4-byte big-endian frame codec; no subsystem duplicates these definitions.

**Tech Stack:** Ubuntu 22.04+, Bash, Git, CMake, Ninja, C++17, Qt 6.2+ (`Core`, `Network`, `Test`), Python 3 with `venv`/`pytest`, Node.js for dependency-free Web unit tests.

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- Deadline is 2026-09-10; 2026-09-08 is feature freeze and 2026-09-09 is code freeze.
- Runtime business data is synthetic and deterministic; never claim it is real charging-platform data.
- Ubuntu 22.04+, Qt Creator 6.2+, Qt 6, C++17, SQLite, Socket and a visible multithreaded architecture are mandatory.
- Qt admin/server is the only runtime SQLite writer.
- TCP messages use a 4-byte unsigned big-endian payload length followed by one UTF-8 JSON object; maximum payload is 1 MiB.
- JSON uses camelCase; SQLite columns use snake_case. Money is integer fen; API timestamps are ISO 8601 with `+08:00`.
- Statuses are exactly user `active|frozen`, charger `idle|reserved|charging|fault|restarting`, order `reserved|charging|completed|cancelled`, congestion `low|medium|high`, and forecast run `active|superseded`.
- No Docker, MySQL, InfluxDB, MinIO, message queue or production authentication system.

---

## Planned File Map

- `.gitignore` — excludes build/runtime/secrets/virtual environments.
- `CMakeLists.txt` — root C++17 project and test entry point.
- `CMakePresets.json` — repeatable Debug/Release Ninja builds.
- `scripts/check_env.sh` — read-only prerequisite report.
- `scripts/bootstrap.sh` — installs the approved development dependencies and creates `.venv`.
- `requirements-dev.txt` — Python test/tool dependencies.
- `shared/contracts/Actions.h` — canonical protocol action constants.
- `shared/contracts/Statuses.h` — canonical enum strings and validation.
- `shared/contracts/Permissions.h` — canonical action-to-role authorization matrix.
- `shared/protocol/Envelope.h` — typed request/response values.
- `shared/protocol/FrameCodec.h`, `FrameCodec.cpp` — incremental frame encoder/decoder.
- `shared/protocol/JsonEnvelope.h`, `JsonEnvelope.cpp` — JSON validation/serialization.
- `shared/protocol/CMakeLists.txt` — `ev_protocol` library.
- `shared/protocol/fixtures/*.json` — cross-language contract examples.
- `tests/shared/tst_framecodec.cpp`, `tst_jsonenvelope.cpp` — Qt Test coverage.
- `docs/design/interface-contract.md` — human-readable frozen v1 contract.

### Task 1: Reproducible toolchain and repository skeleton

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `scripts/check_env.sh`
- Create: `scripts/bootstrap.sh`
- Create: `requirements-dev.txt`
- Create: `apps/user-client/CMakeLists.txt`
- Create: `apps/admin-server/CMakeLists.txt`
- Create: `simulator/CMakeLists.txt`
- Create: `shared/protocol/CMakeLists.txt`
- Create: `tests/shared/CMakeLists.txt`

**Interfaces:**
- Consumes: Ubuntu APT repositories and Python package index during bootstrap.
- Produces: `build/debug`, `build/release`, `.venv`, and a root CMake graph that later plans extend.

- [ ] **Step 1: Write the failing environment check**

Create `scripts/check_env.sh` with executable mode and the exact checks:

```bash
#!/usr/bin/env bash
set -euo pipefail
missing=0
for command_name in git cmake ninja qmake6 python3 node npm; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'MISSING %s\n' "$command_name"
    missing=1
  fi
done
python3 -c 'import pytest, numpy, pandas, sklearn, joblib' 2>/dev/null || {
  printf 'MISSING python-ml-dependencies\n'
  missing=1
}
exit "$missing"
```

- [ ] **Step 2: Run the check and record the expected failure**

Run: `bash scripts/check_env.sh`

Expected on the current VM: non-zero exit with at least `MISSING cmake`, `MISSING qmake6`, `MISSING node`, and `MISSING python-ml-dependencies`.

- [ ] **Step 3: Write the bootstrap script**

Create `scripts/bootstrap.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo apt-get install -y git cmake ninja-build pkg-config nodejs npm python3-venv \
  qt6-base-dev qt6-base-dev-tools qt6-webengine-dev qt6-charts-dev
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements-dev.txt
```

Create `requirements-dev.txt`:

```text
pytest>=8,<9
numpy>=2,<3
pandas>=2,<3
scikit-learn>=1.6,<2
joblib>=1.4,<2
```

- [ ] **Step 4: Create the root build files and empty subsystem targets**

Root `CMakeLists.txt` must contain:

```cmake
cmake_minimum_required(VERSION 3.22)
project(EvChargingPlatform VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
enable_testing()
add_subdirectory(shared/protocol)
add_subdirectory(apps/admin-server)
add_subdirectory(apps/user-client)
add_subdirectory(simulator)
add_subdirectory(tests/shared)
```

Each empty subsystem/shared/test `CMakeLists.txt` must contain a comment only until its own plan adds a target. `CMakePresets.json` defines Ninja presets named `debug` and `release`, writing to `build/debug` and `build/release`.

- [ ] **Step 5: Add safe ignores**

`.gitignore` must include:

```text
/build/
/.venv/
/runtime/
**/runtime/
*.user
*.user.*
*.db
*.db-wal
*.db-shm
*.log
config.local.ini
.env
```

- [ ] **Step 6: Bootstrap and verify**

Run:

```bash
bash scripts/bootstrap.sh
bash scripts/check_env.sh
cmake --preset debug
cmake --build --preset debug
```

Expected: environment check exits 0 and the empty root build configures successfully.

- [ ] **Step 7: Initialize version control and commit**

Run only if `git rev-parse --is-inside-work-tree` fails:

```bash
git init
git switch -c main
git add .gitignore CMakeLists.txt CMakePresets.json scripts requirements-dev.txt apps simulator
git commit -m "build: bootstrap EV charging monorepo"
git switch -c dev
```

### Task 2: Freeze action and status constants

**Files:**
- Create: `shared/contracts/Actions.h`
- Create: `shared/contracts/Statuses.h`
- Create: `shared/contracts/Permissions.h`
- Create: `tests/shared/tst_contracts.cpp`
- Modify: `tests/shared/CMakeLists.txt`

**Interfaces:**
- Consumes: approved action/status lists from the spec.
- Produces: `ev::actions::*`, `ev::status::*`, and validation functions used by every C++ subsystem.

- [ ] **Step 1: Write the failing contract tests**

Create `tests/shared/tst_contracts.cpp` with assertions equivalent to:

```cpp
void ContractsTest::actionsAreStable() {
    QCOMPARE(ev::actions::ChargeReserve, QStringLiteral("charge.reserve"));
    QCOMPARE(ev::actions::ForecastPublish, QStringLiteral("forecast.publish"));
    QCOMPARE(ev::actions::TelemetryPush, QStringLiteral("telemetry.push"));
}
void ContractsTest::statusesValidate() {
    QVERIFY(ev::status::isCharger("idle"));
    QVERIFY(ev::status::isCharger("restarting"));
    QVERIFY(!ev::status::isCharger("online"));
    QVERIFY(ev::status::isOrder("reserved"));
    QVERIFY(!ev::status::isOrder("settled"));
    QVERIFY(ev::status::isForecastRun("active"));
    QVERIFY(!ev::status::isForecastRun("draft"));
}
void ContractsTest::permissionsAreStable() {
    QVERIFY(ev::permissions::allows("", "system.health"));
    QVERIFY(ev::permissions::allows("user", "forecast.latest"));
    QVERIFY(ev::permissions::allows("admin", "demo.reset"));
    QVERIFY(!ev::permissions::allows("user", "demo.reset"));
}
```

- [ ] **Step 2: Run the test and verify failure**

Run: `cmake --preset debug && cmake --build --preset debug --target tst_contracts`

Expected: compile failure because the headers do not exist.

- [ ] **Step 3: Implement the constants without duplicate literals**

`Actions.h` exposes `inline const QString` constants for every action in section 8.2 of the spec. `Statuses.h` exposes the five allowed `QStringList` values and these exact functions:

```cpp
namespace ev::status {
bool isUser(QStringView value);
bool isCharger(QStringView value);
bool isOrder(QStringView value);
bool isCongestion(QStringView value);
bool isForecastRun(QStringView value);
}
```

`Permissions.h` exposes `bool allows(QStringView actorType, QStringView action)` and encodes the exact matrix in design section 8.3. Empty actor type is anonymous; `system.health` is allowed for every actor. No service may maintain a second permission table.

- [ ] **Step 4: Run and pass**

Run: `cmake --build --preset debug --target tst_contracts && ctest --preset debug -R contracts --output-on-failure`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add shared/contracts tests/shared
git commit -m "feat(protocol): freeze v1 actions and statuses"
```

### Task 3: Implement and test the incremental frame codec

**Files:**
- Create: `shared/protocol/FrameCodec.h`
- Create: `shared/protocol/FrameCodec.cpp`
- Create: `shared/protocol/CMakeLists.txt`
- Create: `tests/shared/tst_framecodec.cpp`
- Modify: `tests/shared/CMakeLists.txt`

**Interfaces:**
- Consumes: arbitrary TCP byte chunks.
- Produces: complete JSON payload byte arrays or a protocol error; maximum payload is `1'048'576` bytes.

- [ ] **Step 1: Write failing half-packet, sticky-packet and invalid-length tests**

```cpp
void FrameCodecTest::decodesFragmentedAndCoalescedFrames() {
    ev::protocol::FrameDecoder decoder;
    const QByteArray a = ev::protocol::encodeFrame(R"({"a":1})");
    const QByteArray b = ev::protocol::encodeFrame(R"({"b":2})");
    QCOMPARE(decoder.append(a.left(2)).size(), 0);
    QCOMPARE(decoder.append(a.mid(2) + b),
             QList<QByteArray>({R"({"a":1})", R"({"b":2})"}));
}
void FrameCodecTest::rejectsInvalidLengths() {
    ev::protocol::FrameDecoder decoder;
    QVERIFY_EXCEPTION_THROWN(decoder.append(QByteArray::fromHex("00000000")),
                             ev::protocol::FrameError);
    QVERIFY_EXCEPTION_THROWN(decoder.append(QByteArray::fromHex("00100001")),
                             ev::protocol::FrameError);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_framecodec`

Expected: compile failure for missing `FrameDecoder`.

- [ ] **Step 3: Implement the codec**

Public API:

```cpp
namespace ev::protocol {
inline constexpr quint32 MaxPayloadBytes = 1024U * 1024U;
class FrameError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
QByteArray encodeFrame(QByteArrayView payload);
class FrameDecoder {
public:
    QList<QByteArray> append(QByteArrayView bytes);
    void reset();
private:
    QByteArray buffer_;
};
}
```

Use `qToBigEndian<quint32>` and `qFromBigEndian<quint32>`. Do not discard an incomplete header or body; loop until no complete frame remains.

- [ ] **Step 4: Run and pass**

Run: `cmake --build --preset debug --target tst_framecodec && ctest --preset debug -R framecodec --output-on-failure`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add shared/protocol tests/shared
git commit -m "feat(protocol): add length-prefixed frame codec"
```

### Task 4: Validate JSON request and response envelopes

**Files:**
- Create: `shared/protocol/Envelope.h`
- Create: `shared/protocol/JsonEnvelope.h`
- Create: `shared/protocol/JsonEnvelope.cpp`
- Create: `shared/protocol/fixtures/request-health.json`
- Create: `shared/protocol/fixtures/response-health.json`
- Create: `tests/shared/tst_jsonenvelope.cpp`
- Modify: `shared/protocol/CMakeLists.txt`
- Modify: `tests/shared/CMakeLists.txt`

**Interfaces:**
- Consumes: one decoded JSON payload.
- Produces: `RequestEnvelope`, `ResponseEnvelope`, or `EnvelopeError{code,message}`.

- [ ] **Step 1: Write failing validation tests**

```cpp
void JsonEnvelopeTest::roundTripsRequest() {
    ev::protocol::RequestEnvelope request{1, "req-1", "system.health", "", {}};
    const auto parsed = ev::protocol::parseRequest(ev::protocol::toJson(request));
    QCOMPARE(parsed.requestId, "req-1");
    QCOMPARE(parsed.action, "system.health");
}
void JsonEnvelopeTest::rejectsMissingAndWrongFields() {
    QVERIFY_EXCEPTION_THROWN(ev::protocol::parseRequest(R"({"version":1})"),
                             ev::protocol::EnvelopeError);
    QVERIFY_EXCEPTION_THROWN(ev::protocol::parseRequest(
        R"({"version":2,"requestId":"x","action":"system.health","payload":{}})"),
        ev::protocol::EnvelopeError);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_jsonenvelope`

Expected: compile failure for missing envelope types.

- [ ] **Step 3: Implement exact types and parsers**

```cpp
struct RequestEnvelope {
    int version;
    QString requestId;
    QString action;
    QString token;
    QJsonObject payload;
};
struct ResponseEnvelope {
    QString requestId;
    bool ok;
    QString code;
    QString message;
    QJsonValue data;
};
```

Require `version == 1`, nonblank request ID/action, string token when present, and object payload. Responses require all five fields. Map failures to `INVALID_REQUEST` or `UNSUPPORTED_VERSION`.

- [ ] **Step 4: Add cross-language fixture files**

`request-health.json`:

```json
{"version":1,"requestId":"fixture-health","action":"system.health","token":"","payload":{}}
```

`response-health.json`:

```json
{"requestId":"fixture-health","ok":true,"code":"OK","message":"healthy","data":{"status":"ok"}}
```

Tests load both files and round-trip them without changing field values.

- [ ] **Step 5: Run all shared tests**

Run: `cmake --build --preset debug && ctest --preset debug --output-on-failure`

Expected: all shared tests PASS.

- [ ] **Step 6: Commit**

```bash
git add shared/protocol tests/shared
git commit -m "feat(protocol): validate v1 JSON envelopes"
```

### Task 5: Publish the frozen human-readable contract

**Files:**
- Create: `docs/design/interface-contract.md`
- Modify: `shared/contracts/Actions.h`
- Test: `tests/shared/tst_contracts.cpp`

**Interfaces:**
- Consumes: all v1 constants and fixtures from Tasks 2–4.
- Produces: the review artifact signed off by #2 TL and #3 PRL before 2026-09-02 18:00.

- [ ] **Step 1: Add a completeness test**

Define `ev::actions::all()` and assert it returns exactly the 27 actions listed in design section 8.2, with no duplicates or blank strings.

- [ ] **Step 2: Run and verify failure**

Run: `ctest --preset debug -R contracts --output-on-failure`

Expected: FAIL until `all()` is implemented.

- [ ] **Step 3: Implement `all()` and write the contract document**

The document must include: frame diagram, request/response JSON, maximum size, money/time conventions, all actions, all statuses, every error code, the action-to-role matrix, and for each of the 27 actions its exact payload fields/types/requiredness/ranges, success `data`, allowed state, owner service and main failures. It also records transaction ownership, `dashboard_snapshot.json` ownership, and the “additive changes only after freeze” rule. Copy exact values from the spec rather than paraphrasing them.

- [ ] **Step 4: Run the full foundation verification**

```bash
bash scripts/check_env.sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

Expected: exit 0 and all tests PASS.

- [ ] **Step 5: Tag the contract milestone**

```bash
git add shared docs/design/interface-contract.md tests/shared
git commit -m "docs(protocol): publish frozen v1 interface contract"
git tag -a v0.1-contract -m "Frozen v1 protocol and data contract"
```
