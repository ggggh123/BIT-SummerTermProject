#include "app/UserAppConfig.h"
#include "domain/Models.h"
#include "net/TcpJsonClient.h"
#include "services/UserApi.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "ui/ChargePage.h"
#include "ui/LoginPage.h"
#include "ui/MainWindow.h"
#include "ui/NearbyPage.h"
#include "ui/UiTheme.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <QtTest>

namespace {

constexpr auto kMobile = "13800138000";

template<typename Widget>
Widget *required(QObject *parent, const char *objectName)
{
    return parent->findChild<Widget *>(QString::fromLatin1(objectName));
}

ev::protocol::RequestEnvelope takeRequest(QTcpSocket *peer, int timeoutMs = 5'000)
{
    QElapsedTimer timer;
    timer.start();
    while (peer->bytesAvailable() < 4 && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < 4) {
        return {};
    }
    const QByteArray header = peer->read(4);
    const quint32 length = qFromBigEndian<quint32>(header.constData());
    while (peer->bytesAvailable() < static_cast<qint64>(length)
           && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < static_cast<qint64>(length)) {
        return {};
    }
    return ev::protocol::parseRequest(peer->read(length));
}

void reply(QTcpSocket *peer, const QString &requestId, const QJsonValue &data)
{
    const ev::protocol::ResponseEnvelope response{
        requestId, true, QStringLiteral("OK"), {}, data};
    const QByteArray frame = ev::protocol::encodeFrame(ev::protocol::toJson(response));
    QCOMPARE(peer->write(frame), qint64{frame.size()});
    QVERIFY(peer->flush());
}

QJsonObject userObject()
{
    return {
        {QStringLiteral("userId"), 42},
        {QStringLiteral("mobile"), QString::fromLatin1(kMobile)},
        {QStringLiteral("nickname"), QStringLiteral("测试用户")},
        {QStringLiteral("avatarPath"), QString()},
        {QStringLiteral("balanceFen"), 12345},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("registeredAt"), QStringLiteral("2026-09-01T08:30:45+08:00")},
    };
}

UserAppConfig usableConfig(quint16 port)
{
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = port;
    config.tencentMapKey = QStringLiteral("test-map-key");
    return config;
}

QTcpSocket *waitForPeer(QTcpServer &server, int timeoutMs = 5'000)
{
    QElapsedTimer timer;
    timer.start();
    while (!server.hasPendingConnections() && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    return server.nextPendingConnection();
}

void completeLoginWithoutOrder(MainWindow &window, QTcpSocket *peer,
                              QJsonValue currentOrder = QJsonValue(QJsonValue::Null))
{
    auto *phone = required<QLineEdit>(&window, "phoneEdit");
    auto *button = required<QPushButton>(&window, "loginButton");
    QVERIFY(phone != nullptr);
    QVERIFY(button != nullptr);
    phone->setText(QString::fromLatin1(kMobile));
    button->click();
    auto request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    reply(peer, request.requestId,
          QJsonObject{{QStringLiteral("token"), QStringLiteral("mobile-ui-token")},
                      {QStringLiteral("user"), userObject()}});
    request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("order.current"));
    reply(peer, request.requestId,
          QJsonObject{{QStringLiteral("order"), currentOrder}});
}

void saveScreenshotIfRequested(QWidget *widget, const QString &fileName)
{
    const QString directory = qEnvironmentVariable("EV_UI_SCREENSHOT_DIR");
    if (directory.isEmpty()) {
        return;
    }
    QTest::qWait(30); // Allow pending layout and font geometry changes to reach the rendered frame.
    QDir().mkpath(directory);
    QVERIFY2(widget->grab().save(QDir(directory).filePath(fileName)),
             qPrintable(QStringLiteral("无法保存界面截图：%1").arg(fileName)));
}

bool horizontallyInsideViewport(QWidget *widget, QScrollArea *viewport)
{
    const QRect widgetRect(widget->mapTo(viewport->viewport(), QPoint()), widget->size());
    return widgetRect.left() >= 0
        && widgetRect.right() < viewport->viewport()->width();
}

bool intersectsViewport(QWidget *widget, QScrollArea *viewport)
{
    const QRect widgetRect(widget->mapTo(viewport->viewport(), QPoint()), widget->size());
    return viewport->viewport()->rect().intersects(widgetRect);
}

int renderedPixelsNearColor(const QPixmap &pixmap, const QRect &logicalArea,
                            const QColor &expected, int tolerance = 6)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    const qreal scale = pixmap.devicePixelRatio();
    const QRect pixelArea(qFloor(logicalArea.x() * scale),
                          qFloor(logicalArea.y() * scale),
                          qCeil(logicalArea.width() * scale),
                          qCeil(logicalArea.height() * scale));
    const QRect bounded = pixelArea.intersected(image.rect());
    int matchingPixels = 0;
    for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
        for (int x = bounded.left(); x <= bounded.right(); ++x) {
            const QColor actual = image.pixelColor(x, y);
            if (actual.alpha() > 0
                && qAbs(actual.red() - expected.red()) <= tolerance
                && qAbs(actual.green() - expected.green()) <= tolerance
                && qAbs(actual.blue() - expected.blue()) <= tolerance) {
                ++matchingPixels;
            }
        }
    }
    return matchingPixels;
}

QRect renderedColorBounds(const QPixmap &pixmap, const QRect &logicalArea,
                          const QColor &expected, int tolerance = 6)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    const qreal scale = pixmap.devicePixelRatio();
    const QRect pixelArea(qFloor(logicalArea.x() * scale),
                          qFloor(logicalArea.y() * scale),
                          qCeil(logicalArea.width() * scale),
                          qCeil(logicalArea.height() * scale));
    const QRect bounded = pixelArea.intersected(image.rect());
    QRect colorBounds;
    for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
        for (int x = bounded.left(); x <= bounded.right(); ++x) {
            const QColor actual = image.pixelColor(x, y);
            if (actual.alpha() > 0
                && qAbs(actual.red() - expected.red()) <= tolerance
                && qAbs(actual.green() - expected.green()) <= tolerance
                && qAbs(actual.blue() - expected.blue()) <= tolerance) {
                colorBounds = colorBounds.united(QRect(x, y, 1, 1));
            }
        }
    }
    return colorBounds;
}

ev::user::StationListResult stationFixture(bool longText = false)
{
    ev::user::StationListResult result{{39.95, 116.31}, {}};
    const QStringList names{QStringLiteral("中关村示例充电站"),
                            QStringLiteral("海淀示例充电站"), QStringLiteral("学院路示例充电站")};
    for (int i = 0; i < (longText ? 12 : 3); ++i) {
        result.stations.append({i + 1, longText ? QStringLiteral("北京中关村科技园超长站点名称东区地下停车场充电服务站") : names[i],
            longText ? QStringLiteral("北京市海淀区中关村科技园创新大道一百八十八号地下停车场东区最里面第九排停车位")
                     : QStringLiteral("北京市海淀区示例路18号"),
            39.98, 116.32, 135 + i * 10, false, 4, i == 2 ? 0 : 2,
            1.5 + i * .8});
    }
    return result;
}

ev::user::StationDetailResult detailFixture(bool longText = false)
{
    ev::user::StationDetailResult result{stationFixture(longText).stations.first(), {}};
    result.station.distanceKm.reset(); // The detail contract may omit origin-relative distance.
    const QStringList states{QStringLiteral("idle"), QStringLiteral("idle"),
                             QStringLiteral("charging"), QStringLiteral("fault"),
                             QStringLiteral("reserved")};
    for (int i = 0; i < (longText ? 16 : 5); ++i) {
        result.chargers.append({1001 + i, 1,
            longText ? QStringLiteral("BEIJING-ZHONGGUANCUN-UNDERGROUND-EAST-CHARGER-000%1").arg(i)
                     : QStringLiteral("A-%1").arg(i + 1, 2, 10, QLatin1Char('0')),
            QStringLiteral("fast"), 60.0, states[i % 5], 12, 7200,
            QStringLiteral("2026-09-01T08:30:45+08:00")});
    }
    result.station.chargerCount = result.chargers.size();
    result.station.idleCount = longText ? 7 : 2;
    return result;
}

QJsonObject detailObject(const ev::user::StationDetailResult &detail)
{
    const auto &s = detail.station;
    QJsonArray chargers;
    for (const auto &c : detail.chargers) {
        chargers.append(QJsonObject{{"chargerId", c.chargerId}, {"stationId", c.stationId},
            {"code", c.code}, {"type", c.type}, {"powerKw", c.powerKw}, {"status", c.status},
            {"chargeCount", c.chargeCount}, {"totalDurationSec", c.totalDurationSec}, {"updatedAt", c.updatedAt}});
    }
    return {{"station", QJsonObject{{"stationId", s.stationId}, {"name", s.name}, {"address", s.address},
        {"latitude", s.latitude}, {"longitude", s.longitude}, {"priceFenPerKwh", s.priceFenPerKwh},
        {"forecastEnabled", s.forecastEnabled}, {"chargerCount", s.chargerCount}, {"idleCount", s.idleCount}}},
        {"chargers", chargers}};
}

} // namespace

class MobileUiTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void initialWindowSizeUsesPortraitWidthAndAvailableHeight();
    void loginActionsStayReachableAndPendingBlocksDuplicateSubmission();
    void loginKeyboardSubmissionEmitsExactlyOnce();
    void loginErrorCanBeScrolledIntoViewAtSmallHeight();
    void loginIllustrationResourceIsAvailable();
    void authenticatedShellStaysPortraitWithBottomNavigation();
    void invalidConfigurationRemainsVisibleInPortraitShell();
    void unavailableChargerMetadataRemainsReadableAcrossConnectionChanges();
    void detailBackResetAndNewSearchReturnToList();
    void zeroIdleStationOpensDetailAndBackIgnoresLateResponse();
    void detailFailureIsReadableInCurrentView();
    void nearbyLongContentRemainsReachable_data();
    void nearbyLongContentRemainsReachable();
    void authenticatedNearbyClearsExpiredSessionCopy();
    void restoredChargeMetricsAndActionsFitPortrait_data();
    void restoredChargeMetricsAndActionsFitPortrait();
};

void MobileUiTest::initTestCase()
{
    UiTheme::apply(*qApp);
}

void MobileUiTest::initialWindowSizeUsesPortraitWidthAndAvailableHeight()
{
    QCOMPARE(UiTheme::initialWindowSize(QRect(0, 0, 1920, 1080)), QSize(390, 844));
    QCOMPARE(UiTheme::initialWindowSize(QRect(0, 0, 390, 784)), QSize(390, 720));
    QCOMPARE(UiTheme::initialWindowSize(QRect(0, 0, 360, 500)), QSize(360, 436));
    QCOMPARE(UiTheme::initialWindowSize(QRect(0, 0, 320, 40)), QSize(320, 1));
}

void MobileUiTest::loginActionsStayReachableAndPendingBlocksDuplicateSubmission()
{
    LoginPage page;
    page.resize(390, 720);
    page.show();
    QTest::qWait(20);

    auto *button = required<QPushButton>(&page, "loginButton");
    auto *phone = required<QLineEdit>(&page, "phoneEdit");
    QVERIFY(button != nullptr);
    QVERIFY(phone != nullptr);
    QSignalSpy submitted(&page, &LoginPage::loginRequested);
    saveScreenshotIfRequested(&page, QStringLiteral("login-offline-390x720.png"));
    page.setConnectionAvailable(true);
    saveScreenshotIfRequested(&page, QStringLiteral("login-390x720.png"));
    phone->setText(QString::fromLatin1(kMobile));

    QVERIFY(button->height() >= 48);
    QVERIFY(button->mapTo(&page, QPoint(0, button->height())).y() <= page.height());
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(submitted.first().first().toString(), QString::fromLatin1(kMobile));

    page.setPending(true);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(submitted.count(), 1);

    page.setPending(false);
    phone->clear();
    page.resize(390, 844);
    QTest::qWait(20);
    QVERIFY(button->mapTo(&page, QPoint(0, button->height())).y() <= page.height());
    saveScreenshotIfRequested(&page, QStringLiteral("login-390x844.png"));
}

void MobileUiTest::loginKeyboardSubmissionEmitsExactlyOnce()
{
    LoginPage page;
    page.resize(390, 720);
    page.show();
    auto *phone = required<QLineEdit>(&page, "phoneEdit");
    QVERIFY(phone != nullptr);
    QSignalSpy submitted(&page, &LoginPage::loginRequested);
    phone->setText(QString::fromLatin1(kMobile));
    phone->setFocus();
    QTest::keyClick(phone, Qt::Key_Return);
    QCOMPARE(submitted.count(), 1);

    page.setPending(true);
    QTest::keyClick(phone, Qt::Key_Return);
    QCOMPARE(submitted.count(), 1);
}

void MobileUiTest::loginErrorCanBeScrolledIntoViewAtSmallHeight()
{
    LoginPage page;
    page.resize(390, 480);
    page.setError(QStringLiteral("登录失败，请检查手机号后重试。此错误必须保持可见。"));
    page.show();
    QTest::qWait(20);

    auto *scroll = page.findChild<QScrollArea *>(QStringLiteral("loginScrollArea"));
    auto *error = required<QLabel>(&page, "loginError");
    QVERIFY2(scroll != nullptr, "登录页必须提供小高度下可滚动的内容区域");
    QVERIFY(error != nullptr);
    scroll->ensureWidgetVisible(error, 0, 12);
    QTest::qWait(20);
    const QRect visibleError(error->mapTo(scroll->viewport(), QPoint()), error->size());
    QVERIFY(scroll->viewport()->rect().intersects(visibleError));
}

void MobileUiTest::loginIllustrationResourceIsAvailable()
{
    const QPixmap illustration(QStringLiteral(":/ui/login-illustration.png"));
    QVERIFY(!illustration.isNull());
    QVERIFY(illustration.width() > illustration.height());
}

void MobileUiTest::authenticatedShellStaysPortraitWithBottomNavigation()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.resize(390, 720);
    window.show();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer != nullptr);
    saveScreenshotIfRequested(&window, QStringLiteral("initial-shell-390x720.png"));
    completeLoginWithoutOrder(window, peer.data());

    auto *pages = required<QStackedWidget>(&window, "mainPages");
    auto *navigation = required<QWidget>(&window, "authenticatedNavigation");
    auto *contentViewport =
        window.findChild<QScrollArea *>(QStringLiteral("contentViewport"));
    auto *nearby = required<QPushButton>(&window, "nearbyNavigationButton");
    auto *current = required<QPushButton>(&window, "currentOrderNavigationButton");
    auto *history = required<QPushButton>(&window, "historyNavigationButton");
    auto *profile = required<QPushButton>(&window, "profileNavigationButton");
    QVERIFY(pages != nullptr);
    QVERIFY(navigation != nullptr);
    QVERIFY(nearby != nullptr);
    QVERIFY(current != nullptr);
    QVERIFY(history != nullptr);
    QVERIFY(profile != nullptr);
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("nearbyPage"));
    QTRY_VERIFY(navigation->isVisibleTo(&window));
    QVERIFY2(contentViewport != nullptr, "认证页面必须提供不受非活动页面最小宽度影响的内容视口");

    QCOMPARE(window.width(), 390);
    QVERIFY(window.minimumSizeHint().width() <= 390);
    QVERIFY(navigation->mapTo(&window, QPoint()).y()
            >= contentViewport->mapTo(&window, QPoint()).y() + contentViewport->height());
    QVERIFY(nearby->height() >= 56);
    QVERIFY(navigation->height() >= 60);
    QVERIFY(navigation->height() <= 76);
    QCOMPARE(nearby->text(), QStringLiteral("找桩"));
    QCOMPARE(current->text(), QStringLiteral("当前订单"));
    QCOMPARE(history->text(), QStringLiteral("历史订单"));
    QCOMPARE(profile->text(), QStringLiteral("我的账户"));
    QVERIFY(qAbs(nearby->width() - history->width()) <= 1);
    QVERIFY(qAbs(history->width() - profile->width()) <= 1);
    QVERIFY(!nearby->icon().isNull());
    QVERIFY(!history->icon().isNull());
    QVERIFY(!profile->icon().isNull());
    QVERIFY(nearby->property("selected").toBool());
    QVERIFY(current->isHidden());

    // Catches painting the resource SVG's fixed green instead of the real tab palette.
    const QColor selectedText(QStringLiteral("#00856A"));
    const QColor unselectedText(QStringLiteral("#61717B"));
    QCOMPARE(nearby->palette().color(QPalette::ButtonText), selectedText);
    QCOMPARE(history->palette().color(QPalette::ButtonText), unselectedText);
    const QRect nearbyIconBand(0, 0, nearby->width(), nearby->height() / 2);
    const QRect historyIconBand(0, 0, history->width(), history->height() / 2);
    const QPixmap nearbyRendering = nearby->grab();
    const QPixmap historyRendering = history->grab();
    QVERIFY(renderedPixelsNearColor(nearbyRendering, nearbyIconBand, selectedText) > 12);
    QVERIFY2(renderedPixelsNearColor(historyRendering, historyIconBand, unselectedText) > 12,
             "未选中底栏图标必须与灰色标签使用相同的实际调色板颜色");
    QCOMPARE(renderedPixelsNearColor(historyRendering, historyIconBand, selectedText), 0);
    const QRect nearbyColorBounds = renderedColorBounds(
        nearbyRendering, nearbyIconBand, selectedText);
    const QRect historyColorBounds = renderedColorBounds(
        historyRendering, historyIconBand, unselectedText);
    QVERIFY2(nearbyColorBounds.width() / nearbyRendering.devicePixelRatio() >= 12
                 && nearbyColorBounds.height() / nearbyRendering.devicePixelRatio() >= 16,
             qPrintable(QStringLiteral("选中图标高 DPI 渲染被裁切：%1x%2")
                            .arg(nearbyColorBounds.width()).arg(nearbyColorBounds.height())));
    QVERIFY2(historyColorBounds.width() / historyRendering.devicePixelRatio() >= 16
                 && historyColorBounds.height() / historyRendering.devicePixelRatio() >= 16,
             qPrintable(QStringLiteral("未选中图标高 DPI 渲染被裁切：%1x%2")
                            .arg(historyColorBounds.width()).arg(historyColorBounds.height())));

    ev::user::Order activeOrder;
    activeOrder.orderId = 99;
    activeOrder.stationId = 3;
    activeOrder.chargerId = 7;
    activeOrder.status = QStringLiteral("reserved");
    auto *chargePage = required<ChargePage>(&window, "chargePage");
    QVERIFY(chargePage != nullptr);
    emit chargePage->currentAuthorityObserved({}, {activeOrder}, false, 0);
    QTRY_VERIFY(current->isVisible());
    QVERIFY(!current->icon().isNull());
    emit chargePage->currentAuthorityObserved({}, {}, true, activeOrder.orderId);
    QTRY_VERIFY(current->isHidden());

    saveScreenshotIfRequested(&window, QStringLiteral("authenticated-shell-390x720.png"));
    window.resize(390, 844);
    QTest::qWait(20);
    saveScreenshotIfRequested(&window, QStringLiteral("authenticated-shell-390x844.png"));

    window.resize(390, 720);
    profile->click();
    auto request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("user.get"));
    reply(peer.data(), request.requestId,
          QJsonObject{{QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("profilePage"));
    auto *nickname = required<QLineEdit>(&window, "nicknameEdit");
    auto *recharge = required<QLineEdit>(&window, "rechargeEdit");
    auto *rechargeButton = required<QPushButton>(&window, "rechargeButton");
    QVERIFY(nickname != nullptr);
    QVERIFY(recharge != nullptr);
    QVERIFY(rechargeButton != nullptr);
    QVERIFY(horizontallyInsideViewport(nickname, contentViewport));
    QVERIFY(horizontallyInsideViewport(recharge, contentViewport));
    QVERIFY(horizontallyInsideViewport(rechargeButton, contentViewport));
    contentViewport->ensureWidgetVisible(rechargeButton, 0, 8);
    QTest::qWait(20);
    QVERIFY(intersectsViewport(rechargeButton, contentViewport));
    saveScreenshotIfRequested(&window, QStringLiteral("profile-390x720.png"));

    history->click();
    request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("order.list"));
    reply(peer.data(), request.requestId,
          QJsonObject{{QStringLiteral("items"), QJsonArray{}},
                      {QStringLiteral("total"), 0}});
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("historyPage"));
    auto *historyRetry = required<QPushButton>(&window, "historyRetryButton");
    QVERIFY(historyRetry != nullptr);
    QVERIFY(horizontallyInsideViewport(historyRetry, contentViewport));
    contentViewport->ensureWidgetVisible(historyRetry, 0, 8);
    QTest::qWait(20);
    QVERIFY(intersectsViewport(historyRetry, contentViewport));
    saveScreenshotIfRequested(&window, QStringLiteral("history-390x720.png"));

    auto *nearbyPage = required<NearbyPage>(&window, "nearbyPage");
    QVERIFY(nearbyPage != nullptr);
    ev::user::Station station;
    station.stationId = 3;
    station.name = QStringLiteral("测试充电站");
    ev::user::Charger charger;
    charger.chargerId = 7;
    charger.stationId = station.stationId;
    charger.code = QStringLiteral("A-07");
    charger.status = QStringLiteral("idle");
    emit nearbyPage->chargerSelected({{39.95, 116.31}, station, charger, 1});
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("chargePage"));
    auto *reserve = required<QPushButton>(&window, "chargeReserveButton");
    QVERIFY(reserve != nullptr);
    QVERIFY(horizontallyInsideViewport(reserve, contentViewport));
    contentViewport->ensureWidgetVisible(reserve, 0, 8);
    QTest::qWait(20);
    QVERIFY(intersectsViewport(reserve, contentViewport));
    saveScreenshotIfRequested(&window, QStringLiteral("charge-390x720.png"));
}

void MobileUiTest::invalidConfigurationRemainsVisibleInPortraitShell()
{
    UserAppConfig config;
    config.validationErrors.append(QStringLiteral("测试配置错误"));
    MainWindow window(config);
    window.resize(390, 480);
    window.show();
    QTest::qWait(20);
    auto *message = required<QLabel>(&window, "configurationMessage");
    QVERIFY(message != nullptr);
    QVERIFY(message->isVisibleTo(&window));
    QVERIFY(message->text().contains(QStringLiteral("测试配置错误")));
    QVERIFY(message->mapTo(&window, QPoint()).y() + message->height() <= window.height());
}

void MobileUiTest::unavailableChargerMetadataRemainsReadableAcrossConnectionChanges()
{
    // Catches accidentally enabling every charger when a detail or reconnect completes.
    NearbyPage page(nullptr, nullptr);
    page.resize(390, 844);
    page.show();
    page.setConnectionAvailable(true);
    page.displayStations(stationFixture());
    page.displayStationDetail(detailFixture());
    QSignalSpy selected(&page, &NearbyPage::chargerSelected);
    for (const char *name : {"chargerButton_1003", "chargerButton_1004", "chargerButton_1005"}) {
        auto *button = required<QPushButton>(&page, name);
        QVERIFY(button);
        QVERIFY2(!button->isEnabled(), "充电中、故障和已预约的桩不允许选择");
        button->click();
    }
    QCOMPARE(selected.count(), 0);
    auto *metadata = required<QLabel>(&page, "chargerMetadata_1003");
    QVERIFY(metadata);
    QTRY_VERIFY(metadata->isVisible());
    QVERIFY(metadata->isEnabled());
    QVERIFY(metadata->text().contains(QStringLiteral("60")));
    page.setConnectionAvailable(false);
    // Cached idle selection remains available; MainWindow owns the existing offline order gate.
    QVERIFY(required<QPushButton>(&page, "chargerButton_1001")->isEnabled());
    QVERIFY(!required<QPushButton>(&page, "chargerButton_1003")->isEnabled());
    QVERIFY(metadata->isEnabled());
    auto *detailStatus = required<QLabel>(&page, "detailStatus");
    QVERIFY(detailStatus);
    QCOMPARE(detailStatus->property("role").toString(), QStringLiteral("danger"));
    QCOMPARE(detailStatus->palette().color(QPalette::WindowText), QColor(QStringLiteral("#BE4B42")));
    saveScreenshotIfRequested(&page, QStringLiteral("detail-offline-390x844.png"));
    page.setConnectionAvailable(true);
    QVERIFY(!detailStatus->text().contains(QStringLiteral("连接不可用")));
    QCOMPARE(detailStatus->property("role").toString(), QStringLiteral("secondary"));
    QCOMPARE(detailStatus->palette().color(QPalette::WindowText), QColor(QStringLiteral("#61717B")));
    saveScreenshotIfRequested(&page, QStringLiteral("detail-recovered-390x844.png"));
    QVERIFY(!required<QPushButton>(&page, "chargerButton_1003")->isEnabled());
    auto *idle = required<QPushButton>(&page, "chargerButton_1001");
    QVERIFY(idle->isEnabled());
    idle->click();
    QCOMPARE(selected.count(), 1);
    const auto selection = qvariant_cast<ev::user::StationSelection>(selected.first().first());
    QCOMPARE(selection.origin.latitude, 39.95);
    QCOMPARE(selection.origin.longitude, 116.31);
    QCOMPARE(selection.station.stationId, qint64{1});
    QCOMPARE(selection.charger.chargerId, qint64{1001});
}

void MobileUiTest::detailBackResetAndNewSearchReturnToList()
{
    // Catches back/reset leaving the old station detail visible or clearing the cached list.
    NearbyPage page(nullptr, nullptr);
    page.resize(390, 720);
    page.show();
    page.setConnectionAvailable(true);
    page.displayStations(stationFixture());
    page.displayStationDetail(detailFixture());
    auto *back = required<QPushButton>(&page, "stationDetailBackButton");
    QVERIFY2(back, "详情需要可返回列表的独立子页面");
    QVERIFY(back->height() >= 44);
    QVERIFY(required<QWidget>(&page, "stationDetailView")->isVisible());
    QSignalSpy selected(&page, &NearbyPage::chargerSelected);
    back->click();
    QVERIFY(required<QWidget>(&page, "stationListView")->isVisible());
    QVERIFY(required<QPushButton>(&page, "stationButton_3")->isEnabled());
    QCOMPARE(selected.count(), 0);
    page.displayStationDetail(detailFixture());
    auto *distance = required<QLabel>(&page, "detailDistance");
    QVERIFY(distance);
    QVERIFY(distance->text().contains(QStringLiteral("1.50")));
    page.displayStations({{40.0, 116.5}, {}});
    QVERIFY(required<QWidget>(&page, "stationListView")->isVisible());
    page.displayStationDetail(detailFixture());
    QVERIFY(required<QLabel>(&page, "detailDistance")->text().contains(QStringLiteral("未提供")));
    page.resetForSessionExpiry();
    QVERIFY(required<QWidget>(&page, "stationListView")->isVisible());
    QVERIFY(!required<QPushButton>(&page, "chargerButton_1001"));
}

void MobileUiTest::zeroIdleStationOpensDetailAndBackIgnoresLateResponse()
{
    // Exercise an actual detail request, callback, cancellation, and navigation without business writes.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    UserApi api(&client);
    NearbyPage page(&api, nullptr);
    client.connectToServer();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer);
    QSignalSpy login(&api, &UserApi::loginSucceeded);
    api.loginByPhone(QString::fromLatin1(kMobile));
    auto request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    reply(peer.data(), request.requestId, QJsonObject{{"token", "fixture-token"}, {"user", userObject()}});
    QTRY_COMPARE(login.count(), 1);
    page.resize(390, 720);
    page.show();
    page.setConnectionAvailable(true);
    auto list = stationFixture();
    list.stations.first().idleCount = 0;
    page.displayStations(list);
    required<QPushButton>(&page, "stationButton_1")->click();
    request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("station.detail"));
    QCOMPARE(request.payload.value(QStringLiteral("stationId")).toInteger(), qint64{1});
    auto *back = required<QPushButton>(&page, "stationDetailBackButton");
    QVERIFY2(back && back->isVisible(), "零空闲站点仍进入可返回的详情页");
    back->click();
    reply(peer.data(), request.requestId, detailObject(detailFixture()));
    QTest::qWait(30);
    QVERIFY(required<QWidget>(&page, "stationListView")->isVisible());
    required<QPushButton>(&page, "stationButton_1")->click();
    request = takeRequest(peer.data());
    auto detail = detailFixture();
    detail.station.idleCount = 0;
    for (auto &charger : detail.chargers) charger.status = QStringLiteral("charging");
    reply(peer.data(), request.requestId, detailObject(detail));
    QTRY_VERIFY(required<QLabel>(&page, "detailTitle"));
    QVERIFY(required<QWidget>(&page, "stationDetailView")->isVisible());
    QVERIFY(!required<QPushButton>(&page, "chargerButton_1001")->isEnabled());
    saveScreenshotIfRequested(&page, QStringLiteral("detail-no-idle-390x720.png"));
    QSignalSpy navigation(&page, &NearbyPage::navigationRequested);
    required<QPushButton>(&page, "navigateButton")->click();
    QCOMPARE(navigation.count(), 1);
    back->click();
    auto *listNavigation = required<QPushButton>(&page, "stationNavigateButton_1");
    QVERIFY(listNavigation);
    listNavigation->click();
    QCOMPARE(navigation.count(), 2);
    for (const auto &args : navigation) {
        QCOMPARE(qvariant_cast<ev::user::GeoPoint>(args[0]).latitude, 39.95);
        QCOMPARE(qvariant_cast<ev::user::Station>(args[1]).longitude, 116.32);
    }
    QTest::qWait(30);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
}

void MobileUiTest::detailFailureIsReadableInCurrentView()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.resize(390, 720);
    window.show();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer);
    completeLoginWithoutOrder(window, peer.data());
    auto *page = required<NearbyPage>(&window, "nearbyPage");
    QTRY_VERIFY(page->isVisible());
    page->displayStations(stationFixture());
    required<QPushButton>(page, "stationButton_1")->click();
    auto request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("station.detail"));
    const ev::protocol::ResponseEnvelope failure{request.requestId, false, QStringLiteral("ENTITY_NOT_FOUND"), QStringLiteral("Station missing"), QJsonObject{}};
    peer->write(ev::protocol::encodeFrame(ev::protocol::toJson(failure)));
    peer->flush();
    auto *status = required<QLabel>(page, "detailStatus");
    QTRY_VERIFY(status->text().contains(QStringLiteral("未找到")));
    QVERIFY(status->isVisibleTo(&window));
    const QString displayedError = status->text();
    page->setConnectionAvailable(true);
    QCOMPARE(status->text(), displayedError);
    auto *detailView = required<QWidget>(page, "stationDetailView");
    QVERIFY2(detailView && detailView->isVisible(), "请求错误必须留在当前详情子页，用户可读错误后返回");
}

void MobileUiTest::nearbyLongContentRemainsReachable_data()
{
    QTest::addColumn<int>("height");
    QTest::newRow("720") << 720;
    QTest::newRow("844") << 844;
}

void MobileUiTest::nearbyLongContentRemainsReachable()
{
    // Catches layout minimum-size pressure, horizontal clipping and unreachable last actions.
    QFETCH(int, height);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.resize(390, height);
    window.show();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer);
    completeLoginWithoutOrder(window, peer.data());
    auto *page = required<NearbyPage>(&window, "nearbyPage");
    QTRY_VERIFY(page->isVisible());
    page->displayStations(stationFixture());
    QTest::qWait(30);
    auto *firstPriceUnit = required<QLabel>(page, "stationPrice_1Unit");
    QVERIFY2(firstPriceUnit, "首卡价格单位须独立保留可见布局空间，不能被富文本行高裁掉");
    QVERIFY(firstPriceUnit->isVisible());
    QVERIFY(firstPriceUnit->text().contains(QStringLiteral("度")));
    QVERIFY(firstPriceUnit->height() >= firstPriceUnit->fontMetrics().height());
    auto *priceViewport = required<QScrollArea>(page, "stationListScroll");
    QVERIFY(priceViewport);
    const QRect unitRect(firstPriceUnit->mapTo(priceViewport->viewport(), QPoint()), firstPriceUnit->size());
    QVERIFY(priceViewport->viewport()->rect().contains(unitRect));
    saveScreenshotIfRequested(&window, QStringLiteral("nearby-390x%1.png").arg(height));
    auto canonicalDetail = detailFixture();
    canonicalDetail.chargers.removeLast(); // Four reference rows; reserved is covered separately.
    canonicalDetail.station.chargerCount = 4;
    page->displayStationDetail(canonicalDetail);
    QTest::qWait(30);
    saveScreenshotIfRequested(&window, QStringLiteral("detail-390x%1.png").arg(height));
    page->displayStations(stationFixture(true));
    auto *listScroll = required<QScrollArea>(page, "stationListScroll");
    QVERIFY2(listScroll, "站点列表必须支持纵向滚动");
    auto *lastStation = required<QPushButton>(page, "stationButton_12");
    QVERIFY(lastStation);
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        listScroll->ensureWidgetVisible(lastStation, 0, 8);
        QTest::qWait(10); // Let queued layout/range updates settle before validating visibility.
        return intersectsViewport(lastStation, listScroll);
    }()), 1'000);
    QVERIFY(horizontallyInsideViewport(lastStation, listScroll));
    QCOMPARE(listScroll->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(window.width(), 390);
    saveScreenshotIfRequested(&window, QStringLiteral("nearby-long-390x%1.png").arg(height));
    page->displayStationDetail(detailFixture(true));
    auto *detailScroll = required<QScrollArea>(page, "stationDetailScroll");
    QVERIFY(detailScroll);
    auto *lastCharger = required<QPushButton>(page, "chargerButton_1016");
    QVERIFY(lastCharger);
    QTRY_VERIFY_WITH_TIMEOUT(([&] {
        detailScroll->ensureWidgetVisible(lastCharger, 0, 8);
        QTest::qWait(10); // Let queued layout/range updates settle before validating visibility.
        return intersectsViewport(lastCharger, detailScroll);
    }()), 1'000);
    QVERIFY(horizontallyInsideViewport(lastCharger, detailScroll));
    QCOMPARE(detailScroll->horizontalScrollBar()->maximum(), 0);
    for (auto *label : detailScroll->findChildren<QLabel *>()) {
        if (label->isVisible()) QVERIFY(horizontallyInsideViewport(label, detailScroll));
    }
    auto *navigation = required<QWidget>(&window, "authenticatedNavigation");
    QVERIFY(navigation->mapTo(&window, QPoint()).y() + navigation->height() <= window.height());
    saveScreenshotIfRequested(&window, QStringLiteral("detail-long-390x%1.png").arg(height));
    QSignalSpy selected(page, &NearbyPage::chargerSelected);
    QTest::mouseClick(lastCharger, Qt::LeftButton);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(qvariant_cast<ev::user::StationSelection>(selected.first().first()).charger.chargerId, qint64{1016});
}

void MobileUiTest::authenticatedNearbyClearsExpiredSessionCopy()
{
    // Real login emits session reset before success; the authenticated empty state must recover.
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer);
    completeLoginWithoutOrder(window, peer.data());
    auto *page = required<NearbyPage>(&window, "nearbyPage");
    QTRY_VERIFY(page->isVisible());
    const QString status = required<QLabel>(page, "nearbyStatus")->text();
    QVERIFY2(!status.contains(QStringLiteral("重新登录")), qPrintable(status));
    QVERIFY(status.contains(QStringLiteral("地址")));
    QTest::qWait(30);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
}

void MobileUiTest::restoredChargeMetricsAndActionsFitPortrait_data()
{
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("ended");
    QTest::addColumn<bool>("longText");
    for (int height : {720, 844}) {
        QTest::newRow(qPrintable(QStringLiteral("charging-%1").arg(height))) << height << false << false;
        QTest::newRow(qPrintable(QStringLiteral("settlement-%1").arg(height))) << height << true << false;
        QTest::newRow(qPrintable(QStringLiteral("long-receipt-%1").arg(height))) << height << true << true;
    }
}

void MobileUiTest::restoredChargeMetricsAndActionsFitPortrait()
{
    QFETCH(int, height);
    QFETCH(bool, ended);
    QFETCH(bool, longText);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.resize(390, height);
    window.show();
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer);
    const auto detail = detailFixture(longText);
    const QJsonObject order{{"orderId", 1028}, {"userId", 42}, {"chargerId", 1001}, {"stationId", 1},
        {"stationName", detail.station.name}, {"chargerCode", detail.chargers.first().code},
        {"status", "charging"}, {"reservedAt", "2026-09-05T09:58:00+08:00"},
        {"startedAt", "2026-09-05T10:00:00+08:00"},
        {"endedAt", ended ? QJsonValue("2026-09-05T10:26:40+08:00") : QJsonValue(QJsonValue::Null)},
        {"energyKwh", 12.480}, {"amountFen", 1685}, {"elapsedSec", 1600}};
    completeLoginWithoutOrder(window, peer.data(), order);
    const auto facts = takeRequest(peer.data());
    QCOMPARE(facts.action, QStringLiteral("station.detail"));
    reply(peer.data(), facts.requestId, detailObject(detail));
    auto *page = required<ChargePage>(&window, "chargePage");
    auto *viewport = required<QScrollArea>(&window, "contentViewport");
    auto *action = required<QPushButton>(page, ended ? "chargeSettleButton" : "chargeStopButton");
    QVERIFY(page && viewport && action);
    QTRY_VERIFY(page->isVisible() && action->isEnabled());
    QVERIFY(action->height() >= 48);
    QCOMPARE(required<QLabel>(page, "chargeMeter")->text(), ended ? QStringLiteral("16.85") : QStringLiteral("12.480"));
    QCOMPARE(required<QLabel>(page, "chargeDuration")->text(), QStringLiteral("26:40"));
    QVERIFY(!required<QPushButton>(&window, "nearbyNavigationButton")->isEnabled());
    QVERIFY(required<QPushButton>(&window, "currentOrderNavigationButton")->isVisible());
    QTest::qWait(30);
    QCOMPARE(viewport->horizontalScrollBar()->maximum(), 0);
    auto *notice = required<QLabel>(page, "chargeNotice");
    QVERIFY(notice);
    const auto *card = notice->parentWidget();
    const int actionGap = action->mapTo(page, QPoint()).y()
        - card->mapTo(page, QPoint(0, card->height())).y();
    QVERIFY2(actionGap <= 32, qPrintable(QStringLiteral("无错误时主要操作前不应保留空白错误块：%1 px").arg(actionGap)));
    for (auto *label : page->findChildren<QLabel *>()) {
        if (label->isVisible()) QVERIFY2(horizontallyInsideViewport(label, viewport), qPrintable(label->objectName()));
    }
    viewport->verticalScrollBar()->setValue(0);
    saveScreenshotIfRequested(&window, QStringLiteral("%1-390x%2.png")
        .arg(longText ? "settlement-long" : ended ? "settlement" : "charging").arg(height));
    viewport->ensureWidgetVisible(action, 0, 8);
    QTest::qWait(30);
    const QRect actionRect(action->mapTo(viewport->viewport(), QPoint()), action->size());
    QVERIFY(viewport->viewport()->rect().contains(actionRect));
    if (longText) saveScreenshotIfRequested(&window, QStringLiteral("settlement-long-action-390x%1.png").arg(height));

    // Real button dispatch, no optimistic success and no duplicate mutation while awaiting ACK.
    action->click();
    auto request = takeRequest(peer.data());
    QCOMPARE(request.action, ended ? QStringLiteral("charge.settle") : QStringLiteral("charge.stop"));
    QCOMPARE(request.payload.value("orderId").toInt(), 1028);
    QVERIFY(!action->isEnabled());
    action->click();
    QVERIFY(!required<QLabel>(page, "chargeStatus")->text().contains(QStringLiteral("成功")));
    QTest::qWait(30);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
}

QTEST_MAIN(MobileUiTest)
#include "tst_mobileui.moc"
