#include "app/UserAppConfig.h"
#include "net/TcpJsonClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "services/UserApi.h"
#include "ui/LoginPage.h"
#include "ui/MainWindow.h"

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
    api.loadNearbyStations(origin);
    const auto list = takeRequest(peer);
    QCOMPARE(list.action, QStringLiteral("station.list"));
    QCOMPARE(list.token, QStringLiteral("station-token"));
    QCOMPARE(list.payload, QJsonObject({{QStringLiteral("latitude"), origin.latitude},
                                       {QStringLiteral("longitude"), origin.longitude}}));
    reply(peer, list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{stationObject(9, 2.0), stationObject(3, 1.0), stationObject(2, 1.0)}}});
    QTRY_COMPARE(loaded.size(), 1);
    const auto result = qvariant_cast<ev::user::StationListResult>(loaded.takeFirst().at(0));
    QCOMPARE(result.origin.latitude, origin.latitude);
    QCOMPARE(result.stations.size(), 3);
    QCOMPARE(result.stations.at(0).stationId, qint64{2});
    QCOMPARE(result.stations.at(1).stationId, qint64{3});
    QCOMPARE(result.stations.at(2).stationId, qint64{9});
    QVERIFY(result.stations.at(0).distanceKm.has_value());
    QCOMPARE(*result.stations.at(0).distanceKm, 1.0);

    api.loadNearbyStations(origin);
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

    api.loadStationDetail(3);
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
    const auto result = qvariant_cast<ev::user::StationDetailResult>(loaded.takeFirst().at(0));
    QCOMPARE(result.station.stationId, qint64{3});
    QCOMPARE(result.station.chargerCount, qint64{4});
    QCOMPARE(result.station.idleCount, qint64{2});
    QCOMPARE(result.chargers.size(), 4);
    QCOMPARE(result.chargers.first().chargerId, qint64{30});

    api.loadStationDetail(3);
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
    QSignalSpy failures(&api, &UserApi::requestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("forecast-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    QJsonArray records;
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        for (int horizonH = 1; horizonH <= 24; ++horizonH) {
            records.append(forecastRecordObject(stationId, horizonH));
        }
    }
    api.loadLatestForecast();
    const auto latest = takeRequest(peer);
    QCOMPARE(latest.action, QStringLiteral("forecast.latest"));
    QCOMPARE(latest.token, QStringLiteral("forecast-token"));
    QCOMPARE(latest.payload, QJsonObject{});
    reply(peer, latest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject(true)}, {QStringLiteral("records"), records}});
    QTRY_COMPARE(loaded.size(), 1);
    auto result = qvariant_cast<ev::user::ForecastLatestResult>(loaded.takeFirst().at(0));
    QVERIFY(result.forecastRun.has_value());
    QVERIFY(result.forecastRun->stale);
    QCOMPARE(result.forecastRun->payloadHash, QString(64, QLatin1Char('a')));
    QCOMPARE(result.records.size(), 144);
    QCOMPARE(result.records.first().horizonH, 1);

    api.loadLatestForecast();
    const auto empty = takeRequest(peer);
    reply(peer, empty.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)}, {QStringLiteral("records"), QJsonArray{}}});
    QTRY_COMPARE(loaded.size(), 1);
    result = qvariant_cast<ev::user::ForecastLatestResult>(loaded.takeFirst().at(0));
    QVERIFY(!result.forecastRun.has_value());
    QVERIFY(result.records.isEmpty());

    api.loadLatestForecast();
    const auto contradictory = takeRequest(peer);
    reply(peer, contradictory.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)},
                      {QStringLiteral("records"), QJsonArray{forecastRecordObject(1, 1)}}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(0)).code, QStringLiteral("INVALID_RESPONSE"));
}

QTEST_MAIN(UserApiTest)
#include "tst_userapi.moc"
