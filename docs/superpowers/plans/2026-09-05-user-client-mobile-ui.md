# Qt 用户端首批竖屏 UI 改造 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** 将已批准的浅色青绿竖屏设计落到真实 Qt 客户端，首批覆盖窗口、公共主题、登录、找站及站点详情。

**Architecture:** 继续使用 Qt Widgets 和现有 UserApi、腾讯地图、订单权限控制。布局与公共样式独立于业务服务；NearbyPage 在内部切换列表与详情。原有历史、账户、订单页面只做窄屏可用性适配，不扩展为整套重新设计。

**Tech Stack:** C++17 / Qt6 Widgets、Network、WebEngineWidgets、QtTest / CMake / Ninja。

**Spec:** `/mnt/hgfs/Desktop/SummerTermProject/design-drafts/2026-09-05-user-client-v3-flow/视觉与交互基准.md`；用户于本轮授权开始 Qt 改造。

## Global Constraints

- 工作区：`/mnt/hgfs/Desktop/SummerTermProject/worktrees/user-client`，分支 `feat/user-client-mobile-ui`，基线 `e21c73a`（origin/dev）。只做本地改动，不 commit、push、merge，不修改主目录。
- 不读取、检查、改动任何 `.xlsx`；不改变服务器、协议合同、ML/Web 代码或功能范围。
- 390 × 844 逻辑像素作为目标构图，窗口根据 Linux 可用高度适配；验证 390 × 720 和 390 × 844。没有假手机边框、系统状态栏或网页替代 Qt。
- 浅色青绿 #00856A，背景 #F5F7F9，白色卡片，正文 #172B3A，次级 #61717B，边框 #DFE5E9，危险 #BE4B42；Noto Sans CJK SC，正文16px、标题26px、辅助13–14px、价格/主数字层级突出；主要按钮48px，返回至少44×44，间距16、边距20、卡片内边距16、圆角12。
- 视觉复核后的间距解释：16px为主要节奏基线和固定卡片内边距；找站嵌套文本可用6px/2px、站卡之间12px，以恢复三站首屏主要信息，不降低上述触控与字号要求。
- 保留 objectName、信号、请求代际与会话隔离；不得削弱重连订单校验、进行中订单禁止找桩、选择后再次校验等规则。
- 数据来自现有模型与真实接口；没有 SOC、实时功率、虚假成功或新的支付方式。测试截图允许 fixture 数据，不得写入生产逻辑。
- 腾讯地图仍为 QWebEngineView + 现有 TencentMapClient；离线测试不得真实调用腾讯 API。两张新图属于概念稿，不意味着本批实现导航/结算重设计。
- 使用内置 ImageGen 生成独立登录插画；不把整张 UI 图片当成产品界面，不手画汽车，不改造为 React。图标使用一致的现成图标库，记录来源与许可，不自行拼 SVG 图案。
- 测试先 RED 后 GREEN，使用真实页面及边界级 TCP fixture。保持现有测试有效，不删测试或放宽断言隐藏回归。
- 构建目录 `/home/hushengyuan/.cache/ev-user-closeout-build`，全局环境，不安装虚拟环境。

## 文件职责

- `apps/user-client/src/ui/UiTheme.h/.cpp`：集中字体、QSS、标准组件属性、初始竖屏尺寸与一致资源图标；不得成为业务数据层。
- `src/main.cpp`、`src/ui/MainWindow.cpp/.h`：主题入口、适应屏幕的竖屏壳及底部导航。
- `src/ui/LoginPage.cpp/.h`：可滚动的欢迎/插画/手机号登录表单。
- `src/ui/NearbyPage.cpp/.h`：单列站点卡片与独立详情子视图；保持现有请求机制。
- `src/ui/HistoryPage.cpp`、`ProfilePage.cpp`、`ChargePage.cpp`：仅为避免窄屏溢出所必需的布局适配。
- `resources/ui/` 与 `resources/resources.qrc`：正式插画、现成图标、来源记录（实际 qrc 路径以仓库为准）。
- `tests/tst_mobileui.cpp`：窗口与登录行为/布局，后续找站详情行为和 opt-in 截图。CMake 注册 `user_mobileui`。
- `docs/test/evidence/user-mobile-ui-2026-09-05/`：测试场景截图；`design-qa.md`：对照设计后的实际结论。

### Task 1: 公共主题、竖屏窗口与登录

执行状态：已完成下列全部5步，完整回归通过；独立任务审查及一轮聚焦修复复核通过。原步骤保留为可复查的测试/实现说明。

**Files:** Create `apps/user-client/src/ui/UiTheme.h`, `UiTheme.cpp`, `tests/tst_mobileui.cpp`; modify `src/main.cpp`, `src/ui/MainWindow.cpp/.h`, `src/ui/LoginPage.cpp/.h`, `apps/user-client/CMakeLists.txt`, existing qrc; add `resources/ui/` assets + source/license note. Only minimal narrow-layout fixes to `HistoryPage.cpp`, `ProfilePage.cpp`, `ChargePage.cpp` where required by shell constraints.

**Interfaces:** Produce `namespace UiTheme { void apply(QApplication &application); QSize initialWindowSize(const QRect &availableGeometry); }`; QSS widget `role` values `pageTitle`, `sectionTitle`, `secondary`, `card`, `primary`, `danger`, `tab`, and `selected` property. Existing page constructors/signals/objectNames remain compatible. Task 2 consumes these tokens. Login illustration at `:/ui/login-illustration.png` (controller generates asset in parallel; integrate when delivered).

- [ ] Step 1 — Add a QtTest executable linked to `ev_user_client_core`, QtTest and qrc; RED tests for viewport and login actions. Use independently derived geometry expectations, not stylesheet source assertions. Example:

```cpp
LoginPage page;
page.resize(390, 720);
page.show();
QTest::qWait(20);
auto *button = page.findChild<QPushButton *>("loginButton");
auto *phone = page.findChild<QLineEdit *>("phoneEdit");
QSignalSpy submitted(&page, &LoginPage::loginRequested);
phone->setText("13800138000");
QVERIFY(button->height() >= 48);
QVERIFY(button->mapTo(&page, QPoint(0, button->height())).y() <= page.height());
QTest::mouseClick(button, Qt::LeftButton);
QCOMPARE(submitted.count(), 1);
QCOMPARE(submitted.first().first().toString(), QString("13800138000"));
page.setPending(true);
QTest::mouseClick(button, Qt::LeftButton);
QCOMPARE(submitted.count(), 1);
```

Add real MainWindow tests proving no page minimum width forces landscape at show, navigation sits below content after authenticated signals, current-order conditional visibility remains. Test errors remain reachable at smaller height. Protect resource availability via QPixmap non-null in consumer test once asset delivered.

- [ ] Step 2 — Build focused target and run `ctest --test-dir /home/hushengyuan/.cache/ev-user-closeout-build -R user_mobileui --output-on-failure`. Save expected RED evidence before production edits.
- [ ] Step 3 — Implement theme with Qt QSS, apply in main and tests, remove `window.resize(720, 480)`. Initial size should fit available screen rather than fixed844:

```cpp
const int height = qMax(1, qMin(844, availableGeometry.height() - 64));
return QSize(qMin(390, availableGeometry.width()), height);
```

Keep long pages scrollable and do not let inactive pages dictate wide shell minimum. Prefer a small responsive stack wrapper or scroll container that preserves `mainPages->currentWidget()` semantics used by tests. Do not hide real errors. Valid configuration text can be hidden; invalid configuration must remain visible. Move authenticatedNavigation below mainPages, equal width tabs, shorter label 找桩 allowed, icon+label coherent, conditional current-order unchanged. MainWindow tests must still see actual pages as stack currentWidget.

- [ ] Step 4 — Login matches reference `/mnt/hgfs/Desktop/SummerTermProject/design-drafts/2026-09-05-user-client-v2-mobile/01-login.png`: mint welcome illustration region above white form, title 电动汽车充电, copy 找到合适的站点 / 从容开启充电, 欢迎使用, 手机号, 登录 / 注册, 首次登录将自动创建账户. Actual error/connection text retained; no shield implying security guarantees. Illustration shrinks before form loses reachability. Focus and keyboard submission should be coherent without duplicate requests. Download only chosen official icon assets with license; no environment installation needed.
- [ ] Step 5 — Run focused tests GREEN and existing client regression, capture actual login at390×720 and390×844 to report directory using opt-in `EV_UI_SCREENSHOT_DIR` if convenient. Include initial-window/shell screenshot and narrow-page checks. No screenshots in normal test runs unless requested env set. Self-review then write task report with commands/results/changed files. No commits.

### Task 2: 手机找站卡片与站点详情子页面

执行状态：已完成下列全部5步，实施者完整回归与截图通过；独立任务审查发现缺站原因隐藏，补充真实刷新RED/GREEN后经聚焦复核通过。最终整批审查/回归另见验证记录。

**Files:** Modify `apps/user-client/src/ui/NearbyPage.cpp/.h`, `tests/tst_mobileui.cpp`; extend `UiTheme.cpp` only necessary shared role selectors. `tests/tst_tencentmap.cpp` 与 `tests/tst_userapi.cpp` 中已迁移的可见状态/ID/计数断言可改为检查对应独立标签，保留中文、无英文线协议文本与全部信号/行为校验。`tst_userapi.cpp` 的离线危险色字面断言仅从 red 改为批准的 #BE4B42；订单生命周期测试仅将“进入订单前选桩”的初始详情 fixture 改为 idle，再由原有 enterOrder(reserved/charging) 建立目标订单，保留全部订单、网络、缓存、generation、刷新和竞态断言，在报告逐例列出。不得通过改变测试掩盖真实的刷新缓存回归。`tests/test_navigation_html.mjs` 的资源清单断言需兼容已批准的独立UI资源（保留导航及默认头像精确映射、检查新增条目为批准的本地资源且文件存在，不删清单校验）。CMake/qrc only if additional existing-library icons required.

**Interfaces:** Consume `UiTheme::apply(QApplication&)` and shared `role` styling from Task1. Preserve public NearbyPage signals/slots and stable names `stationButton_<id>`, `chargerButton_<id>`, `navigateButton`, `detailTitle`, `addressBox`, `nearbySearchButton`, `nearbyStatus`, `detailStatus`, forecast labels. Add internal QStackedWidget (`nearbyViews`), list (`stationListView`), detail (`stationDetailView`), return (`stationDetailBackButton`).

测试迁移补充：从 idle fixture 获得记忆 selection 后、调用原 enterOrder 前，将该测试 selection.charger.status 恢复为原案例的订单对应状态，以保持关联桩权威事实一致。只修改 setup，不改变 ChargePage 门禁或测试中的业务断言。

- [ ] Step 1 — Write failing behavior tests using public displayStations/displayStationDetail and realistic complete model fixtures (see Models.h) plus a loopback TCP fixture for actual detail request and callbacks. Target regressions: clicking zero-idle station still opens details; detail back returns to cached list without reservation/network write; unavailable charger metadata visible but selection disabled; idle selection emits correct origin/station/charger exactly once; detail busy/offline followed by re-enable never enables occupied/fault charger; session reset returns list and clears previous detail; error after requesting detail is readable in current view. Example behavior assertion:

```cpp
QSignalSpy selected(&page, &NearbyPage::chargerSelected);
page.displayStations(stationListFixture);
page.displayStationDetail(stationDetailFixture);
auto *busy = page.findChild<QPushButton *>("chargerButton_1003");
QVERIFY(!busy->isEnabled());
QVERIFY(page.findChild<QLabel *>("chargerMetadata_1003")->isVisible());
busy->click();
QCOMPARE(selected.count(), 0);
page.findChild<QPushButton *>("stationDetailBackButton")->click();
QVERIFY(page.findChild<QWidget *>("stationListView")->isVisible());
```

Add 390×720/844 layout tests with long station/address/charger codes; actual user can scroll to last card and select action; no horizontal overflow in page content. Use offline tests only. Preserve old tests and record any changed expectation justified by explicit approved behavior.
- [ ] Step 2 — Run `user_mobileui` tests RED before implementation. Existing NearbyPage currently shows list+detail together and enables every charger, so assertions should fail for meaningful reasons.
- [ ] Step 3 — Implement list header, vertical search controls, white station cards matching V2 `02-nearby.png`; station name wrap, prominent price, availability counts, distance navigation affordance and separate 查看充电桩 action. Display absent distance as 未提供, not invented0.00. If no valid forecast, hide decorative unavailable prediction labels without removing optional forecast behavior/data/labels/tests; present existing forecast when available. Do not invent sort promise when current optional forecast ranking is active. Authenticated empty state must invite choosing an origin/searching, not retain 请重新登录 after a successful login; only fix page copy lifecycle and cover it in tests, do not auto-start network searches.
- [ ] Step 4 — Implement internal list/detail views matching V3 `04-station-detail.png`. Detail has top44px back, station summary card, address/price/count/distance and 导航到这里, section选择充电桩, rows with separate readable ID/code/type/额定功率/status and selection control. Distance uses detail value when present, otherwise matching station-list distance only if from the same displayed origin; unknown remains 未提供, not0.00. All idle rows selectable subject to existing gates; occupied/fault/reserved unavailable with status text retained. Store per-charger availability property and compose it with `setDetailControlsEnabled`; do not disable entire metadata cards. Selection only emits existing signal, never calls reserve/start. Preserve displayedDetailOrigin and request generation behavior. Returning from pending detail must not allow late response to force unexpected navigation; cancel pending read or track intended subview without invalidating authoritative business refresh unexpectedly. Details may remain scrollable, global nav fixed below. Reset/new successful search returns list; foreground failures preserve honest cached state. Both navigation entry points emit existing navigationRequested and same coordinates.
- [ ] Step 5 — Run focused tests GREEN, then complete `ctest --test-dir /home/hushengyuan/.cache/ev-user-closeout-build --output-on-failure` and `node --test apps/user-client/tests/test_navigation_html.mjs`. Controller已实测Node初始RED：15项中14通过，资源清单仍只允许navigation.html/default-avatar.svg，新增独立UI资源导致第1项失败。同步清单断言并保留其完整检查能力；不得改动实际导航HTML以躲避失败。 Capture login/list/detail (and long/offline/no-idle states) through test fixtures with explicit screenshot env. Self-review and report with RED/GREEN evidence. No commits.

## 最终核验与交付

Controller 独立检查完整改动（不含任何xlsx），运行最终构建与13个原有CTest加新增测试，打开真实 Qt 截图与原图同次对照，记录 `design-qa.md`。至少覆盖字体、间距、配色、插画资源、文案、390×720与390×844可用性。导航/结算新图留档在 workspace 的 design-drafts；不要将概念图声称为已经实现。交付本地分支、截图、验证结果与下一批边界。
