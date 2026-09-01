# Qt User Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Qt desktop user client that performs phone auto-registration/login, Tencent-map station discovery/navigation, profile and wallet maintenance, and the complete reservation-to-settlement charging journey through the shared server.

**Architecture:** A Qt Widgets shell owns thin pages and a typed `UserApi`; `TcpJsonClient` is the only business transport and no user code opens SQLite. Tencent address parsing uses QNetworkAccessManager; QWebEngineView loads a bundled route page that calls Tencent JavaScript API GL and supports driving/walking modes.

**Tech Stack:** C++17, CMake, Qt 6.2+ (`Core`, `Gui`, `Widgets`, `Network`, `WebEngineWidgets`, `Test`), Tencent WebService and JavaScript API GL.

**Spec:** `docs/plans/2026-09-01-ev-charging-platform-design.md`

## Global Constraints

- Consume shared protocol/actions/statuses; do not access SQLite or calculate authoritative settlement locally.
- Read server host/port and Tencent Key from `config.local.ini` or environment; never commit a real Key.
- Required actions are `system.health`, `auth.user_login`, `user.get`, `user.update`, `wallet.recharge`, `station.list`, `station.detail`, `charger.list`, `charge.reserve`, `charge.start`, `charge.stop`, `charge.settle`, `order.current`, `order.list`, `order.cancel`, `forecast.latest`.
- Phone is exactly 11 digits matching `^1[3-9][0-9]{9}$`; amount input supports at most two decimals and is converted to integer fen.
- Server responses are authoritative for order state, energy, amount and balance.
- Map failure must be visible and retryable; it must not crash the app or prevent the charging flow using already loaded stations.
- User app must show loading, empty, connection-lost and business-error states in Chinese.

---

## Planned File Map

- `apps/user-client/src/main.cpp` — QApplication startup.
- `apps/user-client/src/app/UserAppConfig.*` — config and Key loading.
- `apps/user-client/src/domain/Models.h` — typed user/station/charger/order values.
- `apps/user-client/src/domain/Formatters.*` — phone, money, distance and timestamp formatting.
- `apps/user-client/src/net/TcpJsonClient.*` — framed JSON transport and safe reconnect.
- `apps/user-client/src/net/TencentMapClient.*` — geocoding.
- `apps/user-client/src/services/UserApi.*` — typed action wrappers/decoders.
- `apps/user-client/src/ui/MainWindow.*` — navigation and current-order guard.
- `apps/user-client/src/ui/LoginPage.*`, `NearbyPage.*`, `NavigationPage.*`, `ProfilePage.*`, `ChargePage.*`, `HistoryPage.*` — visible workflow.
- `apps/user-client/resources/map/navigation.html` — Tencent map and route rendering.
- `apps/user-client/resources/resources.qrc` — route page and default avatar.
- `apps/user-client/tests/*` — Qt Test and local fake-server coverage.

### Task 1: Buildable shell, config, and pure domain helpers

**Files:**
- Create: `apps/user-client/src/main.cpp`
- Create: `apps/user-client/src/app/UserAppConfig.h`
- Create: `apps/user-client/src/app/UserAppConfig.cpp`
- Create: `apps/user-client/src/domain/Models.h`
- Create: `apps/user-client/src/domain/Formatters.h`
- Create: `apps/user-client/src/domain/Formatters.cpp`
- Create: `apps/user-client/src/ui/MainWindow.h`
- Create: `apps/user-client/src/ui/MainWindow.cpp`
- Create: `apps/user-client/tests/tst_formatters.cpp`
- Modify: `apps/user-client/CMakeLists.txt`

**Interfaces:**
- Consumes: `EV_SERVER_HOST`, `EV_SERVER_PORT`, `EV_TENCENT_MAP_KEY` or equivalent local INI values.
- Produces: `UserAppConfig::load()`, `isValidPhone`, `parsePositiveFen`, `formatFen`, `haversineKm` and shared view models.

- [ ] **Step 1: Write failing formatter tests**

```cpp
void FormattersTest::phoneAndMoney() {
    QVERIFY(isValidPhone("13800138000"));
    QVERIFY(!isValidPhone("1380013800"));
    QCOMPARE(parsePositiveFen("12.34").value(), 1234);
    QVERIFY(!parsePositiveFen("12.345").has_value());
    QVERIFY(!parsePositiveFen("0").has_value());
    QCOMPARE(formatFen(1234), QStringLiteral("12.34"));
}
void FormattersTest::distanceIsStable() {
    QCOMPARE(qRound(haversineKm(39.9042,116.4074,39.9142,116.4074) * 10.0), 11);
}
```

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_user_formatters`

Expected: compile failure for missing helpers.

- [ ] **Step 3: Implement helpers and typed models**

Models contain camelCase API fields for `User`, `Station`, `Charger`, `CurrentOrder`, `HistoryOrder` and `ApiError`. Use `qint64` for fen, `double` for kWh/km and the frozen status strings. Money parsing uses regex `^[1-9][0-9]*(\.[0-9]{1,2})?$` and integer string conversion.

- [ ] **Step 4: Build a minimal stacked-window shell**

Configure the target with Qt Core/Gui/Widgets/Network/WebEngineWidgets and `ev_protocol`. `MainWindow` initially displays a login placeholder; it does not silently substitute a missing server host, port or map Key.

- [ ] **Step 5: Run and pass**

Run: `cmake --build --preset debug --target ev_user_client tst_user_formatters && ctest --preset debug -R user_formatters --output-on-failure`

Expected: PASS and the executable opens a window.

- [ ] **Step 6: Commit**

```bash
git add apps/user-client
git commit -m "feat(user): scaffold Qt client and domain helpers"
```

### Task 2: Length-prefixed TCP JSON client

**Files:**
- Create: `apps/user-client/src/net/TcpJsonClient.h`
- Create: `apps/user-client/src/net/TcpJsonClient.cpp`
- Create: `apps/user-client/tests/tst_tcpjsonclient.cpp`
- Modify: `apps/user-client/CMakeLists.txt`

**Interfaces:**
- Consumes: `ev_protocol` frames and envelopes.
- Produces: `send(action,payload,token) -> requestId`, response/error signals, connection state and safe reconnect.

- [ ] **Step 1: Write the failing fake-server tests**

Start a local `QTcpServer`. Assert a client request is one big-endian frame with v1 envelope; split a response across two writes and assert one signal; coalesce two replies and assert two signals; disconnect mid-frame and assert one transport error; send an oversized header and assert protocol rejection.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_user_tcpjsonclient`

Expected: FAIL because `TcpJsonClient` is absent.

- [ ] **Step 3: Implement the exact API**

```cpp
class TcpJsonClient final : public QObject {
    Q_OBJECT
public:
    void configure(QString host, quint16 port);
    QString send(QString action, QJsonObject payload, QString token = {});
    void connectToServer();
    void disconnectFromServer();
signals:
    void responseReceived(ev::protocol::ResponseEnvelope response);
    void transportFailed(QString requestId, QString code, QString message);
    void connectionChanged(bool connected);
};
```

Use one `QTcpSocket`, shared codec, a request timeout of 10 seconds, and reconnect delays 1/2/4 seconds capped at 4. Never automatically replay mutation actions; only `system.health`, `station.list`, `station.detail`, `charger.list`, `order.current`, `order.list`, `forecast.latest` may be retried once.

- [ ] **Step 4: Run and pass**

Run: `ctest --preset debug -R user_tcpjsonclient --output-on-failure`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add apps/user-client/src/net apps/user-client/tests
git commit -m "feat(user): add resilient framed TCP client"
```

### Task 3: Typed API, phone login, and current-order guard

**Files:**
- Create: `apps/user-client/src/services/UserApi.h`
- Create: `apps/user-client/src/services/UserApi.cpp`
- Create: `apps/user-client/src/ui/LoginPage.h`
- Create: `apps/user-client/src/ui/LoginPage.cpp`
- Create: `apps/user-client/tests/tst_userapi.cpp`
- Modify: `apps/user-client/src/ui/MainWindow.cpp`

**Interfaces:**
- Consumes: login and current-order responses.
- Produces: `loginByPhone`, `loadCurrentOrder`, typed success/error signals and an in-memory session token.

- [ ] **Step 1: Write failing response-decoder tests**

Assert `auth.user_login` sends `{mobile:"13800138000"}`; a successful response decodes user ID/mobile/nickname/balance/status/token; invalid response fields produce `INVALID_RESPONSE`; server code `USER_FROZEN` remains unchanged. After login, `order.current` with a charging order routes directly to ChargePage, while null routes to NearbyPage.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_user_api`

Expected: FAIL.

- [ ] **Step 3: Implement login UI and API routing**

Login page contains one phone edit, login button, connection banner and inline error. Disable button while pending. `MainWindow` stores the returned user/token, immediately requests `order.current`, and performs the guard before enabling station selection.

- [ ] **Step 4: Verify offline behavior**

Run: `EV_SERVER_PORT=1 build/debug/apps/user-client/ev_user_client`

Expected: window remains responsive and shows “服务器连接不可用”; no modal loop or crash.

- [ ] **Step 5: Run tests and commit**

```bash
ctest --preset debug -R user_api --output-on-failure
git add apps/user-client/src/services apps/user-client/src/ui apps/user-client/tests
git commit -m "feat(user): add phone login and active-order guard"
```

### Task 4: Nearby stations, address geocoding, and Tencent route page

**Files:**
- Create: `apps/user-client/src/net/TencentMapClient.h`
- Create: `apps/user-client/src/net/TencentMapClient.cpp`
- Create: `apps/user-client/src/ui/NearbyPage.h`
- Create: `apps/user-client/src/ui/NearbyPage.cpp`
- Create: `apps/user-client/src/ui/NavigationPage.h`
- Create: `apps/user-client/src/ui/NavigationPage.cpp`
- Create: `apps/user-client/resources/map/navigation.html`
- Create: `apps/user-client/resources/resources.qrc`
- Create: `apps/user-client/tests/tst_tencentmap.cpp`
- Create: `docs/test/tencent-map-smoke.md`
- Modify: `apps/user-client/src/domain/Models.h`
- Modify: `apps/user-client/src/services/UserApi.cpp`

**Interfaces:**
- Consumes: Tencent address result and station/charger APIs.
- Produces: geocoded origin, distance-sorted cards, detailed charger list, and a QWebEngineView route for `driving|walking`.

- [ ] **Step 1: Write failing geocode and route-parameter tests**

Assert the geocode request targets `https://apis.map.qq.com/ws/geocoder/v1/` with encoded `address`, `key` and `output=json`; a Tencent nonzero status emits `MAP_API_ERROR`. The browser URL itself must remain the fixed local `qrc:/map/navigation.html` and contain no Key. Test separate escaped scripts from `buildConfigureMapScript(key)` and `buildRenderRouteScript(from,to,mode,stationName)`; invalid coordinates/mode are rejected, and no log/error text contains the Key.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_user_tencentmap`

Expected: FAIL.

- [ ] **Step 3: Implement address and nearby workflow**

Use QNetworkAccessManager with a 5-second timeout. Preset area selection supplies a known address; manual address calls geocode. Then call `station.list` with `{latitude,longitude}` and sort defensively by `distanceKm`. Cards show name, `priceFenPerKwh`, total/idle and distance. For `forecastEnabled=true`, join the 1-hour record from `forecast.latest`; a newly created non-predicted station shows “暂无预测” and still remains selectable.

- [ ] **Step 4: Implement the bundled route page**

The resource page initially contains no Key and exposes `window.configureMap({key})`; that function dynamically loads Tencent JavaScript API GL, then creates the map. It also exposes `window.renderRoute({from,to,mode,stationName})` and `window.lastRouteStatus`. `NavigationPage` keeps the fixed qrc URL, enables `LocalContentCanAccessRemoteUrls`, calls `configureMap` once with JSON-escaped config, and later injects only validated route JSON with `runJavaScript`.

Add an in-memory `LastRoute {origin,destination,stationName,mode,generatedAt}` after JS reports success. A later load/API failure shows its reason and Retry while leaving the last successful map/route visible; if none exists, show an explicit empty state. The cache is labeled as last successful output and never reported as a fresh Tencent response.

- [ ] **Step 5: Perform the required live smoke test**

With a valid Key: enter one fixed address, verify coordinates, open a fixed station, switch driving→walking, and verify the rendered route changes. Save the smoke result in `docs/test/tencent-map-smoke.md` with date, environment and screenshots; never include the Key.

- [ ] **Step 6: Run tests and commit**

```bash
ctest --preset debug -R user_tencentmap --output-on-failure
git add apps/user-client docs/test/tencent-map-smoke.md
git commit -m "feat(user): add Tencent geocoding and embedded navigation"
```

### Task 5: Profile, nickname, and simulated recharge

**Files:**
- Create: `apps/user-client/src/ui/ProfilePage.h`
- Create: `apps/user-client/src/ui/ProfilePage.cpp`
- Modify: `apps/user-client/src/services/UserApi.h`
- Modify: `apps/user-client/src/services/UserApi.cpp`
- Modify: `apps/user-client/tests/tst_userapi.cpp`

**Interfaces:**
- Consumes: `user.get`, `user.update`, `wallet.recharge`.
- Produces: default avatar display, persisted nickname and authoritative updated balance.

- [ ] **Step 1: Add failing tests**

Assert trimmed nonblank nickname sends `{nickname:"新昵称"}`; blank nickname is rejected locally. Recharge `12.34` sends `{amountFen:1234}`; success updates balance only from the response; failure preserves the old value.

- [ ] **Step 2: Run and verify failure**

Run: `ctest --preset debug -R user_api --output-on-failure`

Expected: new cases FAIL.

- [ ] **Step 3: Implement page and typed methods**

Show the bundled default avatar, nickname edit, phone, balance and recharge input. Disable each save button while pending; show server errors inline. Do not add avatar-upload persistence because it is explicitly outside the frozen scope.

- [ ] **Step 4: Run and pass, then commit**

```bash
ctest --preset debug -R user_api --output-on-failure
git add apps/user-client
git commit -m "feat(user): add profile and simulated recharge"
```

### Task 6: Reservation, charging meter, stopping, and settlement

**Files:**
- Create: `apps/user-client/src/ui/ChargePage.h`
- Create: `apps/user-client/src/ui/ChargePage.cpp`
- Create: `apps/user-client/tests/tst_chargepage.cpp`
- Modify: `apps/user-client/src/services/UserApi.*`
- Modify: `apps/user-client/src/ui/MainWindow.cpp`

**Interfaces:**
- Consumes: `charge.reserve/start/stop/settle`, `order.current/cancel`.
- Produces: one server-driven UI state machine and refreshed balance/order/station state.

- [ ] **Step 1: Write failing UI-state tests**

```text
no order + idle charger -> Reserve button enabled
reserved order -> Start and Cancel enabled
charging + endedAt null -> meter visible, Stop enabled
charging + endedAt set -> settlement summary visible, Settle enabled
completed -> success state and Back to stations enabled
```

Assert mutation buttons remain disabled until matching request ID returns, and stale responses cannot move the current page.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_user_chargepage`

Expected: FAIL.

- [ ] **Step 3: Implement typed action methods and UI transitions**

Poll `order.current` every 2 seconds only while charging. Display server-provided elapsed time, kWh and amount; never derive final debit. On stop, show final summary returned by server. On settle, replace session balance with returned `balanceFen`, mark completed and refresh nearby counts.

- [ ] **Step 4: Map business errors explicitly**

Provide Chinese messages for `USER_FROZEN`, `ACTIVE_ORDER_EXISTS`, `CHARGER_NOT_AVAILABLE`, `ORDER_STATE_CONFLICT`, `INSUFFICIENT_BALANCE`, `DB_BUSY`; then reload current order and charger rather than forcing a local state.

- [ ] **Step 5: Run unit and live vertical-slice tests**

Run:

```bash
ctest --preset debug -R user_chargepage --output-on-failure
# With server and seed running: login -> recharge -> reserve -> start -> three telemetry ticks -> stop -> settle
```

Expected: order and UI follow the frozen state machine and final wallet matches admin/server.

- [ ] **Step 6: Commit**

```bash
git add apps/user-client
git commit -m "feat(user): complete reservation charging and settlement"
```

### Task 7: Order history, ML recommendation, reconnect, and delivery check

**Files:**
- Create: `apps/user-client/src/ui/HistoryPage.h`
- Create: `apps/user-client/src/ui/HistoryPage.cpp`
- Create: `apps/user-client/tests/tst_recommendation.cpp`
- Modify: `apps/user-client/src/ui/NearbyPage.cpp`
- Modify: `apps/user-client/src/services/UserApi.*`
- Create: `apps/user-client/README.md`

**Interfaces:**
- Consumes: `order.list`, exact `forecast.latest -> {forecastRun,records}` data and connection state.
- Produces: newest-first history, deterministic recommendation order and visible reconnect behavior.

- [ ] **Step 1: Write failing history/recommendation tests**

History must sort by `endedAt` descending and show station/charger/time/kWh/amount/status. Recommendation sort key is congestion severity (`low`, `medium`, `high`), then predicted idle count descending, then distance ascending. Forecast freshness uses `forecastRun.activatedAt`; stale records remain visible with source `generatedAt/dataCutoff` but are omitted from ranking priority. `forecastRun=null` and non-predicted stations render “暂无预测” without breaking distance sorting.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build --preset debug --target tst_user_recommendation`

Expected: FAIL.

- [ ] **Step 3: Implement history, forecast labels and connection banner**

Keep essential cached station/order data on disconnect, show a persistent red banner and Retry button, and resume safe reads only after reconnect. Do not replay reserve/start/stop/settle/recharge automatically.

- [ ] **Step 4: Document and verify the user app**

README contains prerequisites, config example without Key, build/run commands, exact golden phone/address/station flow, error meanings and map fallback. Run:

```bash
cmake --build --preset debug --target ev_user_client
ctest --preset debug -R "user_" --output-on-failure
```

Expected: all user tests PASS.

- [ ] **Step 5: Commit**

```bash
git add apps/user-client
git commit -m "test(user): finish history prediction and reconnect flow"
```
