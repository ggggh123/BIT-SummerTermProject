#include "app/UserAppConfig.h"
#include "net/TcpJsonClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "services/UserApi.h"
#include "ui/LoginPage.h"
#include "ui/MainWindow.h"
#include "ui/NearbyPage.h"
#include "ui/ProfilePage.h"

#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <QtEndian>

namespace {

constexpr auto kMobile = "13800138000";
constexpr auto kTimestamp = "2026-09-01T08:30:45.123+08:00";

QJsonObject userObject(QString mobile = QString::fromLatin1(kMobile), QString status = QStringLiteral("active"))
{
    return {
        {QStringLiteral("userId"), 42},
        {QStringLiteral("mobile"), std::move(mobile)},
        {QStringLiteral("nickname"), QStringLiteral("测试用户")},
        {QStringLiteral("avatarPath"), QString()},
        {QStringLiteral("balanceFen"), 12345},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("registeredAt"), QString::fromLatin1(kTimestamp)},
    };
}

QJsonObject orderObject(QString status = QStringLiteral("charging"))
{
    const bool charging = status == QStringLiteral("charging");
    return {
        {QStringLiteral("orderId"), 99},
        {QStringLiteral("userId"), 42},
        {QStringLiteral("chargerId"), 7},
        {QStringLiteral("stationId"), 3},
        {QStringLiteral("stationName"), QStringLiteral("星火充电站")},
        {QStringLiteral("chargerCode"), QStringLiteral("A-07")},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("reservedAt"), QString::fromLatin1(kTimestamp)},
        {QStringLiteral("startedAt"), charging ? QJsonValue(QString::fromLatin1(kTimestamp)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("endedAt"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("energyKwh"), 12.5},
        {QStringLiteral("amountFen"), 2345},
        {QStringLiteral("elapsedSec"), 360},
    };
}

QJsonObject stationObject(qint64 stationId, double distanceKm, bool includeDistance = true,
                          bool forecastEnabled = true)
{
    QJsonObject station{
        {QStringLiteral("stationId"), stationId},
        {QStringLiteral("name"), QStringLiteral("测试充电站%1").arg(stationId)},
        {QStringLiteral("address"), QStringLiteral("北京市海淀区测试路%1号").arg(stationId)},
        {QStringLiteral("latitude"), 39.95 + stationId / 1000.0},
        {QStringLiteral("longitude"), 116.31 + stationId / 1000.0},
        {QStringLiteral("priceFenPerKwh"), 135},
        {QStringLiteral("forecastEnabled"), forecastEnabled},
        {QStringLiteral("chargerCount"), 4},
        {QStringLiteral("idleCount"), 2},
    };
    if (includeDistance) {
        station.insert(QStringLiteral("distanceKm"), distanceKm);
    }
    return station;
}

QJsonObject chargerObject(qint64 chargerId, qint64 stationId,
                          QString status = QStringLiteral("idle"))
{
    return {
        {QStringLiteral("chargerId"), chargerId},
        {QStringLiteral("stationId"), stationId},
        {QStringLiteral("code"), QStringLiteral("T-%1").arg(chargerId)},
        {QStringLiteral("type"), QStringLiteral("fast")},
        {QStringLiteral("powerKw"), 60.0},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("chargeCount"), 12},
        {QStringLiteral("totalDurationSec"), 3600},
        {QStringLiteral("updatedAt"), QString::fromLatin1(kTimestamp)},
    };
}

QJsonObject forecastRunObject(bool stale = false)
{
    return {
        {QStringLiteral("runId"), QStringLiteral("run-20260901")},
        {QStringLiteral("generatedAt"), QStringLiteral("2026-09-01T08:00:00+08:00")},
        {QStringLiteral("dataCutoff"), QStringLiteral("2026-09-01T07:00:00+08:00")},
        {QStringLiteral("activatedAt"), QStringLiteral("2026-09-01T08:01:00+08:00")},
        {QStringLiteral("modelVersion"), QStringLiteral("forecast-v1")},
        {QStringLiteral("payloadHash"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("stale"), stale},
    };
}

QJsonObject forecastRecordObject(qint64 stationId, int horizonH)
{
    const QDateTime forecastAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T07:00:00+08:00"), Qt::ISODate).addSecs(horizonH * 3600);
    return {
        {QStringLiteral("stationId"), stationId},
        {QStringLiteral("forecastAt"), forecastAt.toString(Qt::ISODate)},
        {QStringLiteral("horizonH"), horizonH},
        {QStringLiteral("predictedLoadKw"), 75.5},
        {QStringLiteral("predictedBusyCount"), 2},
        {QStringLiteral("predictedIdleCount"), 2},
        {QStringLiteral("congestionLevel"), QStringLiteral("medium")},
        {QStringLiteral("isPeak"), false},
    };
}

QByteArray responseFrame(const QString &requestId, bool ok, QString code, QString message, QJsonValue data)
{
    return ev::protocol::encodeFrame(ev::protocol::toJson({requestId, ok, std::move(code), std::move(message), std::move(data)}));
}

bool connectToFakeServer(TcpJsonClient &client, QTcpServer &server)
{
    bool accepted = server.hasPendingConnections();
    bool connected = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    const auto quitWhenReady = [&] {
        if (accepted && connected) {
            loop.quit();
        }
    };
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
        accepted = true;
        quitWhenReady();
    });
    QObject::connect(&client, &TcpJsonClient::connectionChanged, &loop, [&](bool connectedNow) {
        connected = connectedNow;
        quitWhenReady();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    client.connectToServer();
    timeout.start(5'000);
    loop.exec();
    return accepted && connected;
}

ev::protocol::RequestEnvelope takeRequest(QTcpSocket *peer)
{
    QElapsedTimer timer;
    timer.start();
    while (peer->bytesAvailable() < 4 && timer.elapsed() < 5'000) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < 4) {
        return {};
    }
    const QByteArray header = peer->read(4);
    const quint32 length = qFromBigEndian<quint32>(header.constData());
    while (peer->bytesAvailable() < static_cast<qint64>(length) && timer.elapsed() < 5'000) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < static_cast<qint64>(length)) {
        return {};
    }
    return ev::protocol::parseRequest(peer->read(length));
}

void reply(QTcpSocket *peer, const QString &requestId, bool ok, const QString &code, const QString &message, const QJsonValue &data)
{
    const QByteArray frame = responseFrame(requestId, ok, code, message, data);
    QCOMPARE(peer->write(frame), qint64{frame.size()});
    QVERIFY(peer->flush());
}

UserAppConfig usableConfig(quint16 port)
{
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = port;
    config.tencentMapKey = QStringLiteral("not-required-for-login");
    return config;
}

} // namespace

class UserApiTest final : public QObject {
    Q_OBJECT

private slots:
    void loginSendsOnlyPhoneAndDecodesCompleteSession();
    void malformedLoginSuccessIsInvalidResponseAndServerErrorIsPreserved();
    void staleLoginResponseCannotReplaceNewerSession();
    void currentOrderUsesOwnedTokenAndRequiresActiveOrderShape();
    void decoderAcceptsMaxSafeIntegerAndRejectsTwoToThe53();
    void nullableOrderTimestampsDecodeAsEmptyStrings();
    void loginPageDisablesWhilePendingAndShowsConnectionFailure();
    void currentOrderGuardRoutesChargingAndNullToStablePages();
    void nearbyStationsUseOwnedSessionValidateDistanceAndSortTies();
    void stationDetailDecodesCompleteAuthoritativeObjects();
    void latestForecastDecodesCompleteRunAndExactNoPrediction();
    void latestForecastRejectsRecordsOutsideMatchingStationSnapshot();
    void task4ResultSignalsCarryIdsAcrossIdenticalArgumentRaces();
    void nearbyPageRejectsReversedSameOriginAndStationResults();
    void nearbyPageScopesFailuresAndUsesChinesePendingEmptyStates();
    void profileActionsUseOwnedSessionAndAuthoritativeResponses();
    void profileValidationAndFailuresPreserveCachedUser();
    void profileCorrelationDropsUnknownResponseIds();
    void uncertainRechargeReconcilesAfterReconnectWithoutReplay();
    void authenticatedProfilePageIsReachableWithStableControls();
    void profilePageKeepsUncertainStateUntilAuthoritativeReconciliation();
};

void UserApiTest::loginSendsOnlyPhoneAndDecodesCompleteSession()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy success(&api, &UserApi::loginSucceeded);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    QCOMPARE(request.token, QString());
    const QJsonObject expectedPayload{{QStringLiteral("mobile"), QString::fromLatin1(kMobile)}};
    QCOMPARE(request.payload, expectedPayload);

    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("opaque-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(success.size(), 1);
    const ev::user::User user = qvariant_cast<ev::user::User>(success.takeFirst().at(0));
    QCOMPARE(user.userId, qint64{42});
    QCOMPARE(user.mobile, QString::fromLatin1(kMobile));
    QCOMPARE(user.nickname, QStringLiteral("测试用户"));
    QCOMPARE(user.balanceFen, qint64{12345});
    QCOMPARE(user.status, QStringLiteral("active"));
    QVERIFY(api.sessionUser().has_value());
    QCOMPARE(api.sessionUser()->userId, qint64{42});
}

void UserApiTest::malformedLoginSuccessIsInvalidResponseAndServerErrorIsPreserved()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto malformedRequest = takeRequest(peer);
    QJsonObject incomplete = userObject();
    incomplete.remove(QStringLiteral("registeredAt"));
    reply(peer, malformedRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("opaque-token")}, {QStringLiteral("user"), incomplete}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto frozenRequest = takeRequest(peer);
    reply(peer, frozenRequest.requestId, false, QStringLiteral("USER_FROZEN"), QStringLiteral("冻结用户不能预约"), QJsonObject{});
    QTRY_COMPARE(failures.size(), 1);
    const ev::user::ApiError failure = qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0));
    QCOMPARE(failure.code, QStringLiteral("USER_FROZEN"));
    QCOMPARE(failure.message, QStringLiteral("冻结用户不能预约"));
}

void UserApiTest::staleLoginResponseCannotReplaceNewerSession()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy successes(&api, &UserApi::loginSucceeded);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto first = takeRequest(peer);
    api.loginByPhone(QStringLiteral("13900139000"));
    const auto second = takeRequest(peer);
    reply(peer, second.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("new-token")}, {QStringLiteral("user"), userObject(QStringLiteral("13900139000"))}});
    QTRY_COMPARE(successes.size(), 1);
    reply(peer, first.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("old-token")}, {QStringLiteral("user"), userObject()}});
    QTest::qWait(50);
    QCOMPARE(successes.size(), 1);
    QVERIFY(api.sessionUser().has_value());
    QCOMPARE(api.sessionUser()->mobile, QStringLiteral("13900139000"));
}

void UserApiTest::currentOrderUsesOwnedTokenAndRequiresActiveOrderShape()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy orders(&api, &UserApi::currentOrderLoaded);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("owned-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    api.loadCurrentOrder();
    const auto badOrder = takeRequest(peer);
    QCOMPARE(badOrder.action, QStringLiteral("order.current"));
    QCOMPARE(badOrder.token, QStringLiteral("owned-token"));
    QCOMPARE(badOrder.payload, QJsonObject{});
    reply(peer, badOrder.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("completed"))}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));

    api.loadCurrentOrder();
    const auto nullOrder = takeRequest(peer);
    reply(peer, nullOrder.requestId, true, QStringLiteral("OK"), QString(), QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_COMPARE(orders.size(), 1);
    QVERIFY(!qvariant_cast<ev::user::CurrentOrderResult>(orders.takeFirst().at(0)).order.has_value());
}

void UserApiTest::decoderAcceptsMaxSafeIntegerAndRejectsTwoToThe53()
{
    constexpr qint64 maxSafeInteger = 9'007'199'254'740'991LL;
    constexpr double twoToThe53 = 9'007'199'254'740'992.0;

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy logins(&api, &UserApi::loginSucceeded);
    QSignalSpy orders(&api, &UserApi::currentOrderLoaded);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    QJsonObject maximumUser = userObject();
    maximumUser.insert(QStringLiteral("userId"), static_cast<double>(maxSafeInteger));
    maximumUser.insert(QStringLiteral("balanceFen"), static_cast<double>(maxSafeInteger));
    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto maximumLogin = takeRequest(peer);
    reply(peer, maximumLogin.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("maximum-token")}, {QStringLiteral("user"), maximumUser}});
    QTRY_COMPARE(logins.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::User>(logins.takeFirst().at(0)).userId, maxSafeInteger);

    QJsonObject maximumOrder = orderObject();
    maximumOrder.insert(QStringLiteral("orderId"), static_cast<double>(maxSafeInteger));
    maximumOrder.insert(QStringLiteral("userId"), static_cast<double>(maxSafeInteger));
    maximumOrder.insert(QStringLiteral("chargerId"), static_cast<double>(maxSafeInteger));
    maximumOrder.insert(QStringLiteral("stationId"), static_cast<double>(maxSafeInteger));
    maximumOrder.insert(QStringLiteral("amountFen"), static_cast<double>(maxSafeInteger));
    maximumOrder.insert(QStringLiteral("elapsedSec"), static_cast<double>(maxSafeInteger));
    api.loadCurrentOrder();
    const auto maximumOrderRequest = takeRequest(peer);
    reply(peer, maximumOrderRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), maximumOrder}});
    QTRY_COMPARE(orders.size(), 1);
    const auto maximumResult = qvariant_cast<ev::user::CurrentOrderResult>(orders.takeFirst().at(0));
    QVERIFY(maximumResult.order.has_value());
    QCOMPARE(maximumResult.order->orderId, maxSafeInteger);
    QCOMPARE(maximumResult.order->amountFen, maxSafeInteger);
    QCOMPARE(maximumResult.order->elapsedSec, maxSafeInteger);

    QJsonObject oversizedAmountOrder = orderObject();
    oversizedAmountOrder.insert(QStringLiteral("amountFen"), twoToThe53);
    api.loadCurrentOrder();
    const auto oversizedAmountRequest = takeRequest(peer);
    reply(peer, oversizedAmountRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), oversizedAmountOrder}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));

    QJsonObject oversizedUser = userObject();
    oversizedUser.insert(QStringLiteral("userId"), twoToThe53);
    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto oversizedLogin = takeRequest(peer);
    reply(peer, oversizedLogin.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("oversized-token")}, {QStringLiteral("user"), oversizedUser}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));
}

void UserApiTest::nullableOrderTimestampsDecodeAsEmptyStrings()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy orders(&api, &UserApi::currentOrderLoaded);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("nullable-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    api.loadCurrentOrder();
    const auto currentOrder = takeRequest(peer);
    reply(peer, currentOrder.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    QTRY_COMPARE(orders.size(), 1);
    const auto result = qvariant_cast<ev::user::CurrentOrderResult>(orders.takeFirst().at(0));
    QVERIFY(result.order.has_value());
    QVERIFY(result.order->startedAt.isEmpty());
    QVERIFY(result.order->endedAt.isEmpty());
}

void UserApiTest::loginPageDisablesWhilePendingAndShowsConnectionFailure()
{
    LoginPage page;
    auto *phone = page.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *button = page.findChild<QPushButton *>(QStringLiteral("loginButton"));
    auto *banner = page.findChild<QLabel *>(QStringLiteral("connectionBanner"));
    QVERIFY(phone != nullptr);
    QVERIFY(button != nullptr);
    QVERIFY(banner != nullptr);
    phone->setText(QString::fromLatin1(kMobile));
    page.setPending(true);
    QVERIFY(!button->isEnabled());
    QCOMPARE(button->text(), QStringLiteral("登录中…"));
    page.setPending(false);
    QVERIFY(button->isEnabled());
    QCOMPARE(button->text(), QStringLiteral("登录"));
    page.setConnectionAvailable(false);
    QCOMPARE(banner->text(), QStringLiteral("服务器连接不可用"));
}

void UserApiTest::currentOrderGuardRoutesChargingAndNullToStablePages()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const auto exerciseGuard = [&](const QJsonValue &order, const QString &expectedName) {
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        auto *login = window.findChild<LoginPage *>(QStringLiteral("loginPage"));
        QVERIFY(login != nullptr);
        auto *phone = login->findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        auto *button = login->findChild<QPushButton *>(QStringLiteral("loginButton"));
        QVERIFY(phone != nullptr);
        QVERIFY(button != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer != nullptr);
        phone->setText(QString::fromLatin1(kMobile));
        button->click();
        const auto loginRequest = takeRequest(peer);
        reply(peer, loginRequest.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("guard-token")}, {QStringLiteral("user"), userObject()}});
        const auto currentRequest = takeRequest(peer);
        QCOMPARE(currentRequest.action, QStringLiteral("order.current"));
        reply(peer, currentRequest.requestId, true, QStringLiteral("OK"), QString(), QJsonObject{{QStringLiteral("order"), order}});
        auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
        QVERIFY(pages != nullptr);
        QTRY_COMPARE(pages->currentWidget()->objectName(), expectedName);
    };

    exerciseGuard(orderObject(), QStringLiteral("chargePage"));
    exerciseGuard(QJsonValue(QJsonValue::Null), QStringLiteral("nearbyPage"));
}

void UserApiTest::nearbyStationsUseOwnedSessionValidateDistanceAndSortTies()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy loaded(&api, &UserApi::nearbyStationsLoaded);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("station-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const ev::user::GeoPoint origin{39.958, 116.317};
    const QString listRequestId = api.loadNearbyStations(origin);
    const auto list = takeRequest(peer);
    QCOMPARE(list.action, QStringLiteral("station.list"));
    QCOMPARE(list.token, QStringLiteral("station-token"));
    QCOMPARE(list.payload, QJsonObject({{QStringLiteral("latitude"), origin.latitude},
                                       {QStringLiteral("longitude"), origin.longitude}}));
    reply(peer, list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{stationObject(9, 2.0), stationObject(3, 1.0), stationObject(2, 1.0)}}});
    QTRY_COMPARE(loaded.size(), 1);
    const auto resultArgs = loaded.takeFirst();
    QCOMPARE(resultArgs.at(0).toString(), listRequestId);
    const auto result = qvariant_cast<ev::user::StationListResult>(resultArgs.at(1));
    QCOMPARE(result.origin.latitude, origin.latitude);
    QCOMPARE(result.stations.size(), 3);
    QCOMPARE(result.stations.at(0).stationId, qint64{2});
    QCOMPARE(result.stations.at(1).stationId, qint64{3});
    QCOMPARE(result.stations.at(2).stationId, qint64{9});
    QVERIFY(result.stations.at(0).distanceKm.has_value());
    QCOMPARE(*result.stations.at(0).distanceKm, 1.0);

    const QString malformedRequestId = api.loadNearbyStations(origin);
    QVERIFY(!malformedRequestId.isEmpty());
    const auto malformed = takeRequest(peer);
    reply(peer, malformed.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{stationObject(3, 0.0, false)}}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));
}

void UserApiTest::stationDetailDecodesCompleteAuthoritativeObjects()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy loaded(&api, &UserApi::stationDetailLoaded);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("detail-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const QString detailRequestId = api.loadStationDetail(3);
    const auto detail = takeRequest(peer);
    QCOMPARE(detail.action, QStringLiteral("station.detail"));
    const QJsonObject expectedDetailPayload{{QStringLiteral("stationId"), 3}};
    QCOMPARE(detail.payload, expectedDetailPayload);
    reply(peer, detail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"), QJsonArray{chargerObject(30, 3), chargerObject(31, 3),
                                                               chargerObject(32, 3, QStringLiteral("charging")),
                                                               chargerObject(33, 3, QStringLiteral("charging"))}}});
    QTRY_COMPARE(loaded.size(), 1);
    const auto detailArgs = loaded.takeFirst();
    QCOMPARE(detailArgs.at(0).toString(), detailRequestId);
    const auto result = qvariant_cast<ev::user::StationDetailResult>(detailArgs.at(1));
    QCOMPARE(result.station.stationId, qint64{3});
    QCOMPARE(result.station.chargerCount, qint64{4});
    QCOMPARE(result.station.idleCount, qint64{2});
    QCOMPARE(result.chargers.size(), 4);
    QCOMPARE(result.chargers.first().chargerId, qint64{30});

    const QString mismatchedRequestId = api.loadStationDetail(3);
    QVERIFY(!mismatchedRequestId.isEmpty());
    const auto mismatched = takeRequest(peer);
    reply(peer, mismatched.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"), QJsonArray{chargerObject(40, 4)}}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));
}

void UserApiTest::latestForecastDecodesCompleteRunAndExactNoPrediction()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy loaded(&api, &UserApi::latestForecastLoaded);
    QSignalSpy stationSnapshot(&api, &UserApi::nearbyStationsLoaded);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("forecast-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const ev::user::GeoPoint origin{39.958, 116.317};
    const QString stationSnapshotRequestId = api.loadNearbyStations(origin);
    const auto stationsRequest = takeRequest(peer);
    QJsonArray snapshotStations;
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        snapshotStations.append(stationObject(stationId, stationId / 10.0));
    }
    reply(peer, stationsRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), snapshotStations}});
    QTRY_COMPARE(stationSnapshot.size(), 1);

    QJsonArray records;
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        for (int horizonH = 1; horizonH <= 24; ++horizonH) {
            records.append(forecastRecordObject(stationId, horizonH));
        }
    }
    const QString latestRequestId = api.loadLatestForecast(stationSnapshotRequestId);
    const auto latest = takeRequest(peer);
    QCOMPARE(latest.action, QStringLiteral("forecast.latest"));
    QCOMPARE(latest.token, QStringLiteral("forecast-token"));
    QCOMPARE(latest.payload, QJsonObject{});
    reply(peer, latest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject(true)}, {QStringLiteral("records"), records}});
    QTRY_COMPARE(loaded.size(), 1);
    auto forecastArgs = loaded.takeFirst();
    QCOMPARE(forecastArgs.at(0).toString(), latestRequestId);
    auto result = qvariant_cast<ev::user::ForecastLatestResult>(forecastArgs.at(1));
    QVERIFY(result.forecastRun.has_value());
    QVERIFY(result.forecastRun->stale);
    QCOMPARE(result.forecastRun->payloadHash, QString(64, QLatin1Char('a')));
    QCOMPARE(result.records.size(), 144);
    QCOMPARE(result.records.first().horizonH, 1);

    const QString emptyRequestId = api.loadLatestForecast(stationSnapshotRequestId);
    const auto empty = takeRequest(peer);
    reply(peer, empty.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)}, {QStringLiteral("records"), QJsonArray{}}});
    QTRY_COMPARE(loaded.size(), 1);
    forecastArgs = loaded.takeFirst();
    QCOMPARE(forecastArgs.at(0).toString(), emptyRequestId);
    result = qvariant_cast<ev::user::ForecastLatestResult>(forecastArgs.at(1));
    QVERIFY(!result.forecastRun.has_value());
    QVERIFY(result.records.isEmpty());

    const QString contradictoryRequestId = api.loadLatestForecast(stationSnapshotRequestId);
    QVERIFY(!contradictoryRequestId.isEmpty());
    const auto contradictory = takeRequest(peer);
    reply(peer, contradictory.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)},
                      {QStringLiteral("records"), QJsonArray{forecastRecordObject(1, 1)}}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));
}

void UserApiTest::latestForecastRejectsRecordsOutsideMatchingStationSnapshot()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy stationsLoaded(&api, &UserApi::nearbyStationsLoaded);
    QSignalSpy forecastsLoaded(&api, &UserApi::latestForecastLoaded);
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("snapshot-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const ev::user::GeoPoint origin{39.958, 116.317};
    const QString stationSnapshotRequestId = api.loadNearbyStations(origin);
    const auto stationList = takeRequest(peer);
    QJsonArray stations;
    for (qint64 stationId = 11; stationId <= 16; ++stationId) {
        stations.append(stationObject(stationId, stationId / 10.0));
    }
    stations.append(stationObject(99, 9.9, true, false));
    reply(peer, stationList.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), stations}});
    QTRY_COMPARE(stationsLoaded.size(), 1);

    QJsonArray wrongIds;
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        for (int horizonH = 1; horizonH <= 24; ++horizonH) {
            wrongIds.append(forecastRecordObject(stationId, horizonH));
        }
    }
    const QString wrongIdsOperationId = api.loadLatestForecast(stationSnapshotRequestId);
    QVERIFY(!wrongIdsOperationId.isEmpty());
    const auto wrongIdRequest = takeRequest(peer);
    reply(peer, wrongIdRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject()},
                      {QStringLiteral("records"), wrongIds}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code,
             QStringLiteral("INVALID_RESPONSE"));
    QCOMPARE(forecastsLoaded.size(), 0);

    QJsonArray wrongCounts;
    for (qint64 stationId = 11; stationId <= 16; ++stationId) {
        for (int horizonH = 1; horizonH <= 24; ++horizonH) {
            QJsonObject record = forecastRecordObject(stationId, horizonH);
            if (stationId == 11 && horizonH == 1) {
                record.insert(QStringLiteral("predictedBusyCount"), 3);
                record.insert(QStringLiteral("predictedIdleCount"), 2);
                record.insert(QStringLiteral("congestionLevel"), QStringLiteral("medium"));
            }
            wrongCounts.append(record);
        }
    }
    const QString wrongCountsOperationId = api.loadLatestForecast(stationSnapshotRequestId);
    QVERIFY(!wrongCountsOperationId.isEmpty());
    const auto wrongCountRequest = takeRequest(peer);
    reply(peer, wrongCountRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject()},
                      {QStringLiteral("records"), wrongCounts}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code,
             QStringLiteral("INVALID_RESPONSE"));
    QCOMPARE(forecastsLoaded.size(), 0);

    const QString shortSnapshotRequestId = api.loadNearbyStations(origin);
    const auto shortList = takeRequest(peer);
    QJsonArray shortStations;
    for (qint64 stationId = 21; stationId <= 25; ++stationId) {
        shortStations.append(stationObject(stationId, stationId / 10.0));
    }
    reply(peer, shortList.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), shortStations}});
    QTRY_COMPARE(stationsLoaded.size(), 2);
    const QString rejectedForecastId = api.loadLatestForecast(shortSnapshotRequestId);
    QVERIFY(rejectedForecastId.isEmpty());
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code,
             QStringLiteral("INVALID_RESPONSE"));
    QCOMPARE(peer->bytesAvailable(), qint64{0});
}

void UserApiTest::task4ResultSignalsCarryIdsAcrossIdenticalArgumentRaces()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    QSignalSpy stationResults(&api, &UserApi::nearbyStationsLoaded);
    QSignalSpy detailResults(&api, &UserApi::stationDetailLoaded);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("race-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const ev::user::GeoPoint origin{39.958, 116.317};
    const QString firstStationsOperationId = api.loadNearbyStations(origin);
    QVERIFY(!firstStationsOperationId.isEmpty());
    const auto firstStations = takeRequest(peer);
    const QString secondStationsOperationId = api.loadNearbyStations(origin);
    QVERIFY(!secondStationsOperationId.isEmpty());
    const auto secondStations = takeRequest(peer);
    reply(peer, secondStations.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{stationObject(2, 0.2)}}});
    reply(peer, firstStations.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{stationObject(1, 0.1)}}});
    QTRY_COMPARE(stationResults.size(), 2);
    QCOMPARE(stationResults.at(0).size(), 2);
    QCOMPARE(stationResults.at(0).at(0).toString(), secondStations.requestId);
    QCOMPARE(qvariant_cast<ev::user::StationListResult>(stationResults.at(0).at(1))
                 .stations.first().stationId,
             qint64{2});
    QCOMPARE(stationResults.at(1).at(0).toString(), firstStations.requestId);

    const QString firstDetailOperationId = api.loadStationDetail(3);
    QVERIFY(!firstDetailOperationId.isEmpty());
    const auto firstDetail = takeRequest(peer);
    const QString secondDetailOperationId = api.loadStationDetail(3);
    QVERIFY(!secondDetailOperationId.isEmpty());
    const auto secondDetail = takeRequest(peer);
    const QJsonArray newerChargers{
        chargerObject(30, 3), chargerObject(31, 3),
        chargerObject(32, 3, QStringLiteral("charging")),
        chargerObject(33, 3, QStringLiteral("charging"))};
    const QJsonArray olderChargers{
        chargerObject(40, 3), chargerObject(41, 3),
        chargerObject(42, 3, QStringLiteral("charging")),
        chargerObject(43, 3, QStringLiteral("charging"))};
    reply(peer, secondDetail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"), newerChargers}});
    reply(peer, firstDetail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"), olderChargers}});
    QTRY_COMPARE(detailResults.size(), 2);
    QCOMPARE(detailResults.at(0).size(), 2);
    QCOMPARE(detailResults.at(0).at(0).toString(), secondDetail.requestId);
    QCOMPARE(qvariant_cast<ev::user::StationDetailResult>(detailResults.at(0).at(1))
                 .chargers.first().chargerId,
             qint64{30});
    QCOMPARE(detailResults.at(1).at(0).toString(), firstDetail.requestId);
}

void UserApiTest::nearbyPageRejectsReversedSameOriginAndStationResults()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    NearbyPage page(&api, nullptr);
    QSignalSpy selections(&page, &NearbyPage::chargerSelected);
    QSignalSpy forecastResults(&api, &UserApi::latestForecastLoaded);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("page-race-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const ev::user::GeoPoint origin{39.958, 116.317};
    page.origin_ = origin;
    page.originGeneration_ = 1;
    page.requestNearbyStations(origin);
    const auto firstStations = takeRequest(peer);
    page.requestNearbyStations(origin);
    const auto secondStations = takeRequest(peer);
    QJsonArray canonicalStations;
    canonicalStations.append(stationObject(2, 0.2));
    for (qint64 stationId = 11; stationId <= 15; ++stationId) {
        canonicalStations.append(stationObject(stationId, stationId / 10.0));
    }
    reply(peer, secondStations.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), canonicalStations}});
    QTRY_VERIFY(page.findChild<QLabel *>(QStringLiteral("stationName_2")) != nullptr);
    const auto firstForecast = takeRequest(peer);
    QCOMPARE(firstForecast.action, QStringLiteral("forecast.latest"));
    QJsonArray currentForecastRecords;
    const QList<qint64> forecastStationIds{2, 11, 12, 13, 14, 15};
    for (qint64 stationId : forecastStationIds) {
        for (int horizonH = 1; horizonH <= 24; ++horizonH) {
            currentForecastRecords.append(forecastRecordObject(stationId, horizonH));
        }
    }
    page.requestNearbyStations(origin);
    const auto thirdStations = takeRequest(peer);
    reply(peer, firstForecast.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject()},
                      {QStringLiteral("records"), currentForecastRecords}});
    QTRY_COMPARE(forecastResults.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(([&page] {
        const auto *label = page.findChild<QLabel *>(QStringLiteral("forecastLabel_2"));
        return label != nullptr && label->text() == QStringLiteral("暂无预测");
    })(), 5'000);
    reply(peer, thirdStations.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), canonicalStations}});
    const auto secondForecast = takeRequest(peer);
    QCOMPARE(secondForecast.action, QStringLiteral("forecast.latest"));
    page.requestNearbyStations(origin);
    const auto fourthStations = takeRequest(peer);
    reply(peer, fourthStations.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), canonicalStations}});
    const auto thirdForecast = takeRequest(peer);
    QCOMPARE(thirdForecast.action, QStringLiteral("forecast.latest"));
    reply(peer, thirdForecast.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject()},
                      {QStringLiteral("records"), currentForecastRecords}});
    QTRY_VERIFY_WITH_TIMEOUT(([&page] {
        const auto *label = page.findChild<QLabel *>(QStringLiteral("forecastLabel_2"));
        return label != nullptr && label->text().contains(QStringLiteral("1小时预测"));
    })(), 5'000);
    reply(peer, secondForecast.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)},
                      {QStringLiteral("records"), QJsonArray{}}});
    QTRY_VERIFY_WITH_TIMEOUT(([&page] {
        const auto *label = page.findChild<QLabel *>(QStringLiteral("forecastLabel_2"));
        return label != nullptr && label->text().contains(QStringLiteral("1小时预测"));
    })(), 5'000);
    reply(peer, firstStations.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{stationObject(1, 0.1)}}});
    QTRY_VERIFY(page.findChild<QLabel *>(QStringLiteral("stationName_2")) != nullptr);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("stationName_1")) == nullptr);

    ev::user::Station detailStation;
    detailStation.stationId = 3;
    detailStation.name = QStringLiteral("详情站");
    detailStation.address = QStringLiteral("北京市海淀区详情路");
    detailStation.latitude = 39.96;
    detailStation.longitude = 116.32;
    detailStation.priceFenPerKwh = 135;
    detailStation.forecastEnabled = false;
    detailStation.chargerCount = 4;
    detailStation.idleCount = 2;
    page.requestStationDetail(detailStation);
    const auto firstDetail = takeRequest(peer);
    page.requestStationDetail(detailStation);
    const auto secondDetail = takeRequest(peer);
    reply(peer, secondDetail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"), QJsonArray{
                           chargerObject(30, 3), chargerObject(31, 3),
                           chargerObject(32, 3, QStringLiteral("charging")),
                           chargerObject(33, 3, QStringLiteral("charging"))}}});
    QTRY_VERIFY(page.findChild<QPushButton *>(QStringLiteral("chargerButton_30")) != nullptr);
    reply(peer, firstDetail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"), QJsonArray{
                           chargerObject(40, 3), chargerObject(41, 3),
                           chargerObject(42, 3, QStringLiteral("charging")),
                           chargerObject(43, 3, QStringLiteral("charging"))}}});
    QTRY_VERIFY(page.findChild<QPushButton *>(QStringLiteral("chargerButton_30")) != nullptr);
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("chargerButton_40")) == nullptr);

    const ev::user::GeoPoint newerOrigin{40.0, 116.4};
    page.origin_ = newerOrigin;
    ++page.originGeneration_;
    page.findChild<QPushButton *>(QStringLiteral("chargerButton_30"))->click();
    QCOMPARE(selections.size(), 1);
    const auto selection = qvariant_cast<ev::user::StationSelection>(selections.takeFirst().at(0));
    QCOMPARE(selection.origin.latitude, origin.latitude);
    QCOMPARE(selection.origin.longitude, origin.longitude);
}

void UserApiTest::nearbyPageScopesFailuresAndUsesChinesePendingEmptyStates()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    NearbyPage page(&api, nullptr);

    auto *connectionBanner = page.findChild<QLabel *>(QStringLiteral("nearbyConnectionBanner"));
    QVERIFY(connectionBanner != nullptr);
    QVERIFY(QMetaObject::invokeMethod(&page, "setConnectionAvailable", Q_ARG(bool, false)));
    QCOMPARE(connectionBanner->text(), QStringLiteral("服务器连接不可用"));

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("state-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const ev::user::GeoPoint origin{39.958, 116.317};
    page.origin_ = origin;
    page.originGeneration_ = 1;
    ev::user::Station retained;
    retained.stationId = 7;
    retained.name = QStringLiteral("保留站");
    retained.address = QStringLiteral("北京市海淀区保留路");
    retained.latitude = 39.97;
    retained.longitude = 116.33;
    retained.priceFenPerKwh = 120;
    retained.chargerCount = 0;
    retained.idleCount = 0;
    retained.distanceKm = 0.7;
    page.displayStations({origin, {retained}});
    auto *search = page.findChild<QPushButton *>(QStringLiteral("nearbySearchButton"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("nearbyStatus"));
    QVERIFY(search != nullptr);
    QVERIFY(status != nullptr);

    page.requestNearbyStations(origin);
    const auto stale = takeRequest(peer);
    page.requestNearbyStations(origin);
    const auto active = takeRequest(peer);
    QVERIFY(!search->isEnabled());
    reply(peer, stale.requestId, false, QStringLiteral("TRANSPORT_ERROR"),
          QStringLiteral("connection lost before response"), QJsonObject{});
    QTRY_VERIFY(!search->isEnabled());
    QVERIFY(!status->text().contains(QStringLiteral("connection"), Qt::CaseInsensitive));
    reply(peer, active.requestId, false, QStringLiteral("TIMEOUT"),
          QStringLiteral("response timed out"), QJsonObject{});
    QTRY_VERIFY(search->isEnabled());
    QCOMPARE(status->text(), QStringLiteral("服务器响应超时，请重试"));
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("stationName_7")) != nullptr);

    page.requestNearbyStations(origin);
    const auto empty = takeRequest(peer);
    reply(peer, empty.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{}}});
    QTRY_VERIFY(search->isEnabled());
    QCOMPARE(status->text(), QStringLiteral("附近暂无充电站"));

    ev::user::Station detailStation = retained;
    detailStation.stationId = 8;
    detailStation.name = QStringLiteral("详情保留站");
    detailStation.chargerCount = 4;
    detailStation.idleCount = 2;
    page.displayStations({origin, {detailStation}});
    ev::user::StationDetailResult retainedDetail;
    retainedDetail.station = detailStation;
    retainedDetail.chargers = {
        {80, 8, QStringLiteral("T-80"), QStringLiteral("fast"), 60.0,
         QStringLiteral("idle"), 12, 3600, QString::fromLatin1(kTimestamp)},
        {81, 8, QStringLiteral("T-81"), QStringLiteral("fast"), 60.0,
         QStringLiteral("idle"), 12, 3600, QString::fromLatin1(kTimestamp)},
        {82, 8, QStringLiteral("T-82"), QStringLiteral("fast"), 60.0,
         QStringLiteral("charging"), 12, 3600, QString::fromLatin1(kTimestamp)},
        {83, 8, QStringLiteral("T-83"), QStringLiteral("fast"), 60.0,
         QStringLiteral("charging"), 12, 3600, QString::fromLatin1(kTimestamp)},
    };
    page.displayStationDetail(retainedDetail);
    auto *detailStatus = page.findChild<QLabel *>(QStringLiteral("detailStatus"));
    auto *stationButton = page.findChild<QPushButton *>(QStringLiteral("stationButton_8"));
    auto *retainedCharger = page.findChild<QPushButton *>(QStringLiteral("chargerButton_80"));
    QVERIFY(detailStatus != nullptr);
    QVERIFY(stationButton != nullptr);
    QVERIFY(retainedCharger != nullptr);

    page.requestStationDetail(detailStation);
    const auto staleDetailRequest = takeRequest(peer);
    page.requestStationDetail(detailStation);
    const auto activeDetailRequest = takeRequest(peer);
    QVERIFY(!stationButton->isEnabled());
    QVERIFY(!retainedCharger->isEnabled());
    QCOMPARE(detailStatus->text(), QStringLiteral("正在加载充电桩…"));
    reply(peer, staleDetailRequest.requestId, false, QStringLiteral("TIMEOUT"),
          QStringLiteral("stale raw timeout"), QJsonObject{});
    QTRY_VERIFY(!stationButton->isEnabled());
    QVERIFY(!retainedCharger->isEnabled());
    QCOMPARE(detailStatus->text(), QStringLiteral("正在加载充电桩…"));
    reply(peer, activeDetailRequest.requestId, false, QStringLiteral("TRANSPORT_ERROR"),
          QStringLiteral("raw socket failure"), QJsonObject{});
    QTRY_VERIFY(stationButton->isEnabled());
    QVERIFY(retainedCharger->isEnabled());
    QCOMPARE(detailStatus->text(), QStringLiteral("服务器连接中断，请重试"));
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("chargerButton_80")) != nullptr);
}

void UserApiTest::profileActionsUseOwnedSessionAndAuthoritativeResponses()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy profileChanged(&api, &UserApi::profileUserChanged);
    QSignalSpy mutationPending(&api, &UserApi::profileMutationPendingChanged);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("profile-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    mutationPending.clear();

    const QString getId = api.loadProfile();
    QVERIFY(!getId.isEmpty());
    const auto get = takeRequest(peer);
    QCOMPARE(get.requestId, getId);
    QCOMPARE(get.action, QStringLiteral("user.get"));
    QCOMPARE(get.token, QStringLiteral("profile-token"));
    QCOMPARE(get.payload, QJsonObject{});
    QJsonObject refreshed = userObject();
    refreshed.insert(QStringLiteral("nickname"), QStringLiteral("刷新后的昵称"));
    refreshed.insert(QStringLiteral("avatarPath"), QStringLiteral("/server/avatar.png"));
    refreshed.insert(QStringLiteral("balanceFen"), 22222);
    refreshed.insert(QStringLiteral("status"), QStringLiteral("frozen"));
    reply(peer, get.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), refreshed}});
    QTRY_COMPARE(profileChanged.size(), 1);
    QCOMPARE(api.sessionUser()->nickname, QStringLiteral("刷新后的昵称"));
    QCOMPARE(api.sessionUser()->avatarPath, QStringLiteral("/server/avatar.png"));
    QCOMPARE(api.sessionUser()->balanceFen, qint64{22222});
    QCOMPARE(api.sessionUser()->status, QStringLiteral("frozen"));

    const QString updateId = api.updateNickname(QStringLiteral("  新昵称  "));
    QVERIFY(!updateId.isEmpty());
    const auto update = takeRequest(peer);
    QCOMPARE(update.requestId, updateId);
    QCOMPARE(update.action, QStringLiteral("user.update"));
    QCOMPARE(update.token, QStringLiteral("profile-token"));
    const QJsonObject expectedUpdatePayload{
        {QStringLiteral("nickname"), QStringLiteral("新昵称")}};
    QCOMPARE(update.payload, expectedUpdatePayload);
    QCOMPARE(mutationPending.size(), 1);
    QVERIFY(mutationPending.first().first().toBool());
    QVERIFY(api.rechargeWallet(QStringLiteral("1.00")).isEmpty());
    QCOMPARE(peer->bytesAvailable(), qint64{0});

    QJsonObject authoritative = refreshed;
    authoritative.insert(QStringLiteral("nickname"), QStringLiteral("服务器规范昵称"));
    authoritative.insert(QStringLiteral("avatarPath"), QStringLiteral("/server/new-avatar.png"));
    authoritative.insert(QStringLiteral("balanceFen"), 33333);
    authoritative.insert(QStringLiteral("status"), QStringLiteral("active"));
    reply(peer, update.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), authoritative}});
    QTRY_COMPARE(profileChanged.size(), 2);
    QCOMPARE(api.sessionUser()->nickname, QStringLiteral("服务器规范昵称"));
    QCOMPARE(api.sessionUser()->balanceFen, qint64{33333});
    QCOMPARE(api.sessionUser()->avatarPath, QStringLiteral("/server/new-avatar.png"));
    QCOMPARE(mutationPending.size(), 2);
    QVERIFY(!mutationPending.last().first().toBool());

    const ev::user::User beforeRecharge = *api.sessionUser();
    const QString rechargeId = api.rechargeWallet(QStringLiteral("12.34"));
    QVERIFY(!rechargeId.isEmpty());
    const auto recharge = takeRequest(peer);
    QCOMPARE(recharge.requestId, rechargeId);
    QCOMPARE(recharge.action, QStringLiteral("wallet.recharge"));
    QCOMPARE(recharge.token, QStringLiteral("profile-token"));
    const QJsonObject expectedRechargePayload{{QStringLiteral("amountFen"), 1234}};
    QCOMPARE(recharge.payload, expectedRechargePayload);
    reply(peer, recharge.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("userId"), 42},
                      {QStringLiteral("balanceFen"), 987654}});
    QTRY_COMPARE(profileChanged.size(), 3);
    QCOMPARE(api.sessionUser()->balanceFen, qint64{987654});
    QCOMPARE(api.sessionUser()->nickname, beforeRecharge.nickname);
    QCOMPARE(api.sessionUser()->mobile, beforeRecharge.mobile);
    QCOMPARE(api.sessionUser()->avatarPath, beforeRecharge.avatarPath);
    QCOMPARE(api.sessionUser()->status, beforeRecharge.status);
    QCOMPARE(api.sessionUser()->registeredAt, beforeRecharge.registeredAt);
}

void UserApiTest::profileValidationAndFailuresPreserveCachedUser()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy failures(&api, &UserApi::profileRequestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("preserve-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    const ev::user::User original = *api.sessionUser();

    QVERIFY(api.updateNickname(QStringLiteral(" \t ")).isEmpty());
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().first()).code,
             QStringLiteral("INVALID_NICKNAME"));
    QVERIFY(api.rechargeWallet(QStringLiteral("90071992547409.92")).isEmpty());
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().first()).code,
             QStringLiteral("INVALID_AMOUNT"));
    QCOMPARE(peer->bytesAvailable(), qint64{0});
    QCOMPARE(api.sessionUser()->nickname, original.nickname);
    QCOMPARE(api.sessionUser()->balanceFen, original.balanceFen);

    const QString malformedGetId = api.loadProfile();
    QVERIFY(!malformedGetId.isEmpty());
    const auto malformedGet = takeRequest(peer);
    QJsonObject incomplete = userObject();
    incomplete.remove(QStringLiteral("registeredAt"));
    reply(peer, malformedGet.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), incomplete}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().first()).code,
             QStringLiteral("INVALID_RESPONSE"));
    QCOMPARE(api.sessionUser()->nickname, original.nickname);
    QCOMPARE(api.sessionUser()->balanceFen, original.balanceFen);

    const QString frozenRechargeId = api.rechargeWallet(QStringLiteral("1.00"));
    QVERIFY(!frozenRechargeId.isEmpty());
    const auto frozenRecharge = takeRequest(peer);
    reply(peer, frozenRecharge.requestId, false, QStringLiteral("USER_FROZEN"),
          QStringLiteral("冻结用户不能充值"), QJsonObject{});
    QTRY_COMPARE(failures.size(), 1);
    const auto frozenFailure =
        qvariant_cast<ev::user::ApiError>(failures.takeFirst().first());
    QCOMPARE(frozenFailure.code, QStringLiteral("USER_FROZEN"));
    QCOMPARE(frozenFailure.message, QStringLiteral("冻结用户不能充值"));
    QCOMPARE(api.sessionUser()->balanceFen, original.balanceFen);

    const QString wrongUserRechargeId = api.rechargeWallet(QStringLiteral("1.00"));
    QVERIFY(!wrongUserRechargeId.isEmpty());
    const auto wrongUserRecharge = takeRequest(peer);
    reply(peer, wrongUserRecharge.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("userId"), 43},
                      {QStringLiteral("balanceFen"), 999999}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().first()).code,
             QStringLiteral("INVALID_RESPONSE"));
    QCOMPARE(api.sessionUser()->nickname, original.nickname);
    QCOMPARE(api.sessionUser()->balanceFen, original.balanceFen);
}

void UserApiTest::profileCorrelationDropsUnknownResponseIds()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy changed(&api, &UserApi::profileUserChanged);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("correlation-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const QString profileId = api.loadProfile();
    const auto profile = takeRequest(peer);
    QCOMPARE(profile.requestId, profileId);
    QJsonObject stale = userObject();
    stale.insert(QStringLiteral("nickname"), QStringLiteral("不应生效"));
    reply(peer, QStringLiteral("unknown-response-id"), true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), stale}});
    QTest::qWait(50);
    QCOMPARE(changed.size(), 0);
    QCOMPARE(api.sessionUser()->nickname, QStringLiteral("测试用户"));

    QJsonObject current = userObject();
    current.insert(QStringLiteral("nickname"), QStringLiteral("匹配响应"));
    reply(peer, profile.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), current}});
    QTRY_COMPARE(changed.size(), 1);
    QCOMPARE(api.sessionUser()->nickname, QStringLiteral("匹配响应"));
}

void UserApiTest::uncertainRechargeReconcilesAfterReconnectWithoutReplay()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    UserApi api(&client);
    QSignalSpy uncertain(&api, &UserApi::profileReconciliationRequired);
    QSignalSpy reconciled(&api, &UserApi::profileReconciled);
    QSignalSpy failures(&api, &UserApi::profileRequestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("uncertain-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    const qint64 oldBalance = api.sessionUser()->balanceFen;

    const QString rechargeId = api.rechargeWallet(QStringLiteral("12.34"));
    const auto recharge = takeRequest(peer);
    QCOMPARE(recharge.requestId, rechargeId);
    peer->abort();
    QTRY_COMPARE_WITH_TIMEOUT(uncertain.size(), 1, 5'000);
    QCOMPARE(api.sessionUser()->balanceFen, oldBalance);
    QVERIFY(api.rechargeWallet(QStringLiteral("1.00")).isEmpty());
    QTRY_VERIFY(!failures.isEmpty());
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.last().first()).code,
             QStringLiteral("RECONCILIATION_REQUIRED"));

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QTcpSocket *reconnectedPeer = server.nextPendingConnection();
    QVERIFY(reconnectedPeer != nullptr);
    const auto reconcile = takeRequest(reconnectedPeer);
    QCOMPARE(reconcile.action, QStringLiteral("user.get"));
    QCOMPARE(reconcile.token, QStringLiteral("uncertain-token"));
    QCOMPARE(reconcile.payload, QJsonObject{});
    QVERIFY(reconcile.requestId != recharge.requestId);
    QJsonObject reconciledUser = userObject();
    reconciledUser.insert(QStringLiteral("balanceFen"), 13579);
    QString rechargeAttemptBeforeReconciledSignal;
    connect(&api, &UserApi::profileUserChanged, &api,
            [&api, &rechargeAttemptBeforeReconciledSignal](const ev::user::User &) {
        rechargeAttemptBeforeReconciledSignal = api.rechargeWallet(QStringLiteral("1.00"));
    });
    reply(reconnectedPeer, reconcile.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), reconciledUser}});
    QTRY_COMPARE(reconciled.size(), 1);
    QVERIFY(rechargeAttemptBeforeReconciledSignal.isEmpty());
    QCOMPARE(api.sessionUser()->balanceFen, qint64{13579});

    const QString nextRechargeId = api.rechargeWallet(QStringLiteral("1.00"));
    QVERIFY(!nextRechargeId.isEmpty());
    const auto nextRecharge = takeRequest(reconnectedPeer);
    QCOMPARE(nextRecharge.action, QStringLiteral("wallet.recharge"));
    const QJsonObject expectedNextRechargePayload{{QStringLiteral("amountFen"), 100}};
    QCOMPARE(nextRecharge.payload, expectedNextRechargePayload);
}

void UserApiTest::authenticatedProfilePageIsReachableWithStableControls()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    QVERIFY(phone != nullptr);
    QVERIFY(loginButton != nullptr);
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("page-profile-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto current = takeRequest(peer);
    QCOMPARE(current.action, QStringLiteral("order.current"));
    reply(peer, current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *profileNavigation =
        window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"));
    QVERIFY(profileNavigation != nullptr);
    QTRY_VERIFY(profileNavigation->isVisible());
    profileNavigation->click();
    const auto get = takeRequest(peer);
    QCOMPARE(get.action, QStringLiteral("user.get"));
    QCOMPARE(get.token, QStringLiteral("page-profile-token"));
    reply(peer, get.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), userObject()}});

    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    QVERIFY(pages != nullptr);
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("profilePage"));
    const QStringList requiredNames{
        QStringLiteral("profileAvatar"), QStringLiteral("nicknameEdit"),
        QStringLiteral("nicknameSaveButton"), QStringLiteral("profileMobile"),
        QStringLiteral("profileBalance"), QStringLiteral("rechargeEdit"),
        QStringLiteral("rechargeButton"), QStringLiteral("profileStatus"),
        QStringLiteral("profileError"), QStringLiteral("profileRetryButton")};
    for (const QString &name : requiredNames) {
        QVERIFY2(window.findChild<QWidget *>(name) != nullptr, qPrintable(name));
    }
    auto *avatar = window.findChild<QLabel *>(QStringLiteral("profileAvatar"));
    auto *mobile = window.findChild<QLineEdit *>(QStringLiteral("profileMobile"));
    auto *balance = window.findChild<QLabel *>(QStringLiteral("profileBalance"));
    auto *nickname = window.findChild<QLineEdit *>(QStringLiteral("nicknameEdit"));
    auto *nicknameButton =
        window.findChild<QPushButton *>(QStringLiteral("nicknameSaveButton"));
    auto *rechargeButton = window.findChild<QPushButton *>(QStringLiteral("rechargeButton"));
    QVERIFY(avatar != nullptr);
    QVERIFY(mobile != nullptr);
    QVERIFY(balance != nullptr);
    QVERIFY(nickname != nullptr);
    QVERIFY(nicknameButton != nullptr);
    QVERIFY(rechargeButton != nullptr);
    QVERIFY(!avatar->pixmap(Qt::ReturnByValue).isNull());
    QVERIFY(mobile->isReadOnly());
    QCOMPARE(mobile->text(), QString::fromLatin1(kMobile));
    QVERIFY(balance->text().contains(QStringLiteral("123.45")));

    QTRY_VERIFY(nicknameButton->isEnabled());
    QTRY_VERIFY(rechargeButton->isEnabled());
    nickname->setText(QStringLiteral("  页面昵称  "));
    nicknameButton->click();
    const auto update = takeRequest(peer);
    QCOMPARE(update.action, QStringLiteral("user.update"));
    const QJsonObject expectedPageUpdatePayload{
        {QStringLiteral("nickname"), QStringLiteral("页面昵称")}};
    QCOMPARE(update.payload, expectedPageUpdatePayload);
    QVERIFY(!nicknameButton->isEnabled());
    QVERIFY(!rechargeButton->isEnabled());
    QJsonObject updated = userObject();
    updated.insert(QStringLiteral("nickname"), QStringLiteral("服务端页面昵称"));
    reply(peer, update.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), updated}});
    QTRY_VERIFY(nicknameButton->isEnabled());
    QTRY_VERIFY(rechargeButton->isEnabled());
    QCOMPARE(nickname->text(), QStringLiteral("服务端页面昵称"));
}

void UserApiTest::profilePageKeepsUncertainStateUntilAuthoritativeReconciliation()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    UserApi api(&client);
    ProfilePage page(&api);
    page.show();
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("uncertain-page-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    page.refresh();
    const auto initialGet = takeRequest(peer);
    reply(peer, initialGet.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), userObject()}});

    auto *rechargeEdit = page.findChild<QLineEdit *>(QStringLiteral("rechargeEdit"));
    auto *rechargeButton = page.findChild<QPushButton *>(QStringLiteral("rechargeButton"));
    auto *nicknameButton =
        page.findChild<QPushButton *>(QStringLiteral("nicknameSaveButton"));
    auto *status = page.findChild<QLabel *>(QStringLiteral("profileStatus"));
    auto *error = page.findChild<QLabel *>(QStringLiteral("profileError"));
    auto *balance = page.findChild<QLabel *>(QStringLiteral("profileBalance"));
    QVERIFY(rechargeEdit != nullptr);
    QVERIFY(rechargeButton != nullptr);
    QVERIFY(nicknameButton != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(error != nullptr);
    QVERIFY(balance != nullptr);
    QTRY_VERIFY(rechargeButton->isEnabled());

    rechargeEdit->setText(QStringLiteral("1.00"));
    rechargeButton->click();
    const auto frozenRecharge = takeRequest(peer);
    reply(peer, frozenRecharge.requestId, false, QStringLiteral("USER_FROZEN"),
          QStringLiteral("冻结用户不能充值"), QJsonObject{});
    QTRY_COMPARE(error->text(), QStringLiteral("冻结用户不能充值"));
    QVERIFY(balance->text().contains(QStringLiteral("123.45")));
    QTRY_VERIFY(rechargeButton->isEnabled());

    rechargeEdit->setText(QStringLiteral("12.34"));
    rechargeButton->click();
    const auto recharge = takeRequest(peer);
    QCOMPARE(recharge.action, QStringLiteral("wallet.recharge"));
    peer->abort();
    const QString uncertainText =
        QStringLiteral("结果未确认，请重新连接后刷新账户信息");
    QTRY_COMPARE(error->text(), uncertainText);
    QCOMPARE(status->text(), uncertainText);
    QVERIFY(!rechargeButton->isEnabled());
    QVERIFY(!nicknameButton->isEnabled());

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QTcpSocket *reconnectedPeer = server.nextPendingConnection();
    QVERIFY(reconnectedPeer != nullptr);
    const auto reconcile = takeRequest(reconnectedPeer);
    QCOMPARE(reconcile.action, QStringLiteral("user.get"));
    QCOMPARE(error->text(), uncertainText);
    QVERIFY(!rechargeButton->isEnabled());
    QJsonObject reconciledUser = userObject();
    reconciledUser.insert(QStringLiteral("balanceFen"), 24680);
    reply(reconnectedPeer, reconcile.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), reconciledUser}});
    QTRY_COMPARE(status->text(), QStringLiteral("账户信息已对账"));
    QCOMPARE(error->text(), QString());
    QVERIFY(rechargeButton->isEnabled());
    QVERIFY(nicknameButton->isEnabled());
}

QTEST_MAIN(UserApiTest)
#include "tst_userapi.moc"
