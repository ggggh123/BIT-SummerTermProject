#include "app/UserAppConfig.h"
#include "net/TcpJsonClient.h"
#include "net/TencentMapClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "services/UserApi.h"
#include "ui/ChargePage.h"
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

QJsonObject canonicalOrderObject(QString status, bool ended = false,
                                 qint64 chargerId = 7, qint64 stationId = 3)
{
    const bool started = status == QStringLiteral("charging")
        || status == QStringLiteral("completed");
    const bool terminal = ended || status == QStringLiteral("completed")
        || status == QStringLiteral("cancelled");
    QJsonObject order = orderObject(std::move(status));
    order.insert(QStringLiteral("chargerId"), chargerId);
    order.insert(QStringLiteral("stationId"), stationId);
    order.insert(QStringLiteral("chargerCode"), QStringLiteral("T-%1").arg(chargerId));
    order.insert(QStringLiteral("startedAt"), started
        ? QJsonValue(QString::fromLatin1(kTimestamp)) : QJsonValue(QJsonValue::Null));
    order.insert(QStringLiteral("endedAt"), terminal
        ? QJsonValue(QStringLiteral("2026-09-01T09:30:45.123+08:00"))
        : QJsonValue(QJsonValue::Null));
    return order;
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

ev::user::Station stationValue()
{
    ev::user::Station station;
    station.stationId = 3;
    station.name = QStringLiteral("测试充电站3");
    station.address = QStringLiteral("北京市海淀区测试路3号");
    station.latitude = 39.953;
    station.longitude = 116.313;
    station.priceFenPerKwh = 135;
    station.chargerCount = 4;
    station.idleCount = 2;
    return station;
}

ev::user::Charger chargerValue(QString status = QStringLiteral("idle"))
{
    ev::user::Charger charger;
    charger.chargerId = 7;
    charger.stationId = 3;
    charger.code = QStringLiteral("T-7");
    charger.type = QStringLiteral("fast");
    charger.powerKw = 60.0;
    charger.status = std::move(status);
    charger.updatedAt = QString::fromLatin1(kTimestamp);
    return charger;
}

ev::user::Order orderValue(QString status, bool ended = false,
                           qint64 chargerId = 7, qint64 stationId = 3)
{
    ev::user::Order order;
    order.orderId = 99;
    order.userId = 42;
    order.chargerId = chargerId;
    order.stationId = stationId;
    order.stationName = QStringLiteral("测试充电站%1").arg(stationId);
    order.chargerCode = QStringLiteral("T-%1").arg(chargerId);
    order.status = std::move(status);
    order.reservedAt = QString::fromLatin1(kTimestamp);
    if (order.status == QStringLiteral("charging") || order.status == QStringLiteral("completed")) {
        order.startedAt = QString::fromLatin1(kTimestamp);
    }
    if (ended || order.status == QStringLiteral("completed")
        || order.status == QStringLiteral("cancelled")) {
        order.endedAt = QStringLiteral("2026-09-01T09:30:45.123+08:00");
    }
    order.energyKwh = 12.5;
    order.amountFen = 2345;
    order.elapsedSec = 360;
    return order;
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
    void activeOrderGuardSurvivesProfileNavigation();
    void noOrderNavigationKeepsNearbyAvailable();
    void chargeMutationDisablesAuthenticatedNavigationUntilOutcome();
    void nearbyStationsUseOwnedSessionValidateDistanceAndSortTies();
    void stationDetailDecodesCompleteAuthoritativeObjects();
    void latestForecastDecodesCompleteRunAndExactNoPrediction();
    void latestForecastRejectsRecordsOutsideMatchingStationSnapshot();
    void task4ResultSignalsCarryIdsAcrossIdenticalArgumentRaces();
    void nearbyPageRejectsReversedSameOriginAndStationResults();
    void chargeRefreshRejectsStaleSelectionAndCannotOverwriteNewDetail();
    void nearbyReplacementInvalidatesRememberedChargeSelection();
    void uncertainReserveKeepsGlobalGateUntilCurrentAndFacts();
    void activeOrderExistsCannotBeBypassedBeforeReconciliation();
    void backgroundNearbyReplacementCannotInvalidatePendingReserve();
    void chargeRefreshPreservesIdleSelectionInBothResponseOrders();
    void chargeRefreshSuccessCannotEraseFactsFailure();
    void disconnectIgnoresChargeListReplayBeforeFreshReconciliation();
    void foregroundOriginReplacementRemovesOldStationControls();
    void activeOrderExistsChargingStatesWaitForMatchingFacts();
    void terminalBackWaitsForNearbyFactsToCommit();
    void terminalAndNullBackRemainReachableAcrossReconnect();
    void terminalWithoutMatchingNearbyContextDoesNotStrandBack();
    void exitRefreshResolvesOnceWhenContextChangesOrStationIsMissing();
    void chargeDerivedForecastIsNotReplayedAfterDisconnect();
    void differentAuthoritativeOrderClearsRememberedSelectionCopies();
    void externalActiveOrderAuthorityInvalidatesVisibleSelection();
    void exitRefreshFirstFailureWins_data();
    void exitRefreshFirstFailureWins();
    void externalActiveOrderSupersedesTerminalExit_data();
    void externalActiveOrderSupersedesTerminalExit();
    void deferredNearbyInvalidationPreventsSelectionResurrection_data();
    void deferredNearbyInvalidationPreventsSelectionResurrection();
    void nearbyPageScopesFailuresAndUsesChinesePendingEmptyStates();
    void profileActionsUseOwnedSessionAndAuthoritativeResponses();
    void profileValidationAndFailuresPreserveCachedUser();
    void profileRejectsInvalidFailureCodesAndPreservesLegalCodes();
    void profileCorrelationDropsUnknownResponseIds();
    void uncertainRechargeReconcilesAfterReconnectWithoutReplay();
    void authenticatedProfilePageIsReachableWithStableControls();
    void profilePageKeepsUncertainStateUntilAuthoritativeReconciliation();
    void profilePageLocalizesProtocolAndUnknownErrors();
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
    QSignalSpy failures(&api, &UserApi::chargeRequestFailed);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("owned-token")}, {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    (void)api.loadCurrentOrder();
    const auto badOrder = takeRequest(peer);
    QCOMPARE(badOrder.action, QStringLiteral("order.current"));
    QCOMPARE(badOrder.token, QStringLiteral("owned-token"));
    QCOMPARE(badOrder.payload, QJsonObject{});
    reply(peer, badOrder.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("completed"))}});
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().at(1)).code, QStringLiteral("INVALID_RESPONSE"));

    (void)api.loadCurrentOrder();
    const auto nullOrder = takeRequest(peer);
    reply(peer, nullOrder.requestId, true, QStringLiteral("OK"), QString(), QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_COMPARE(orders.size(), 1);
    QVERIFY(!qvariant_cast<ev::user::CurrentOrderResult>(orders.takeFirst().at(1)).order.has_value());
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
    QSignalSpy orderFailures(&api, &UserApi::chargeRequestFailed);

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
    (void)api.loadCurrentOrder();
    const auto maximumOrderRequest = takeRequest(peer);
    reply(peer, maximumOrderRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), maximumOrder}});
    QTRY_COMPARE(orders.size(), 1);
    const auto maximumResult = qvariant_cast<ev::user::CurrentOrderResult>(orders.takeFirst().at(1));
    QVERIFY(maximumResult.order.has_value());
    QCOMPARE(maximumResult.order->orderId, maxSafeInteger);
    QCOMPARE(maximumResult.order->amountFen, maxSafeInteger);
    QCOMPARE(maximumResult.order->elapsedSec, maxSafeInteger);

    QJsonObject oversizedAmountOrder = orderObject();
    oversizedAmountOrder.insert(QStringLiteral("amountFen"), twoToThe53);
    (void)api.loadCurrentOrder();
    const auto oversizedAmountRequest = takeRequest(peer);
    reply(peer, oversizedAmountRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), oversizedAmountOrder}});
    QTRY_COMPARE(orderFailures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(orderFailures.takeFirst().at(1)).code, QStringLiteral("INVALID_RESPONSE"));

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
    (void)api.loadCurrentOrder();
    const auto currentOrder = takeRequest(peer);
    reply(peer, currentOrder.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    QTRY_COMPARE(orders.size(), 1);
    const auto result = qvariant_cast<ev::user::CurrentOrderResult>(orders.takeFirst().at(1));
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

void UserApiTest::activeOrderGuardSurvivesProfileNavigation()
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
          QJsonObject{{QStringLiteral("token"), QStringLiteral("active-nav-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto current = takeRequest(peer);
    reply(peer, current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject()}});

    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    auto *nearbyNavigation =
        window.findChild<QPushButton *>(QStringLiteral("nearbyNavigationButton"));
    auto *profileNavigation =
        window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"));
    auto *currentOrderNavigation =
        window.findChild<QPushButton *>(QStringLiteral("currentOrderNavigationButton"));
    QVERIFY(pages != nullptr);
    QVERIFY(nearbyNavigation != nullptr);
    QVERIFY(profileNavigation != nullptr);
    QVERIFY(currentOrderNavigation != nullptr);
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("chargePage"));
    const auto associatedFacts = takeRequest(peer);
    QCOMPARE(associatedFacts.action, QStringLiteral("station.detail"));
    reply(peer, associatedFacts.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"), QJsonArray{
                           chargerObject(7, 3, QStringLiteral("charging")),
                           chargerObject(8, 3), chargerObject(9, 3),
                           chargerObject(10, 3, QStringLiteral("fault"))}}});
    QVERIFY(!nearbyNavigation->isEnabled());
    QVERIFY(currentOrderNavigation->isVisible());
    QTRY_VERIFY(currentOrderNavigation->isEnabled());

    profileNavigation->click();
    const auto profileGet = takeRequest(peer);
    QCOMPARE(profileGet.action, QStringLiteral("user.get"));
    reply(peer, profileGet.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("profilePage"));
    nearbyNavigation->click();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("profilePage"));
    currentOrderNavigation->click();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("chargePage"));
}

void UserApiTest::noOrderNavigationKeepsNearbyAvailable()
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
          QJsonObject{{QStringLiteral("token"), QStringLiteral("no-order-nav-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto current = takeRequest(peer);
    reply(peer, current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    auto *nearbyNavigation =
        window.findChild<QPushButton *>(QStringLiteral("nearbyNavigationButton"));
    auto *profileNavigation =
        window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"));
    auto *currentOrderNavigation =
        window.findChild<QPushButton *>(QStringLiteral("currentOrderNavigationButton"));
    QVERIFY(pages != nullptr);
    QVERIFY(nearbyNavigation != nullptr);
    QVERIFY(profileNavigation != nullptr);
    QVERIFY(currentOrderNavigation != nullptr);
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("nearbyPage"));
    QVERIFY(nearbyNavigation->isEnabled());
    QVERIFY(!currentOrderNavigation->isVisible());

    profileNavigation->click();
    const auto profileGet = takeRequest(peer);
    reply(peer, profileGet.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("profilePage"));
    nearbyNavigation->click();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("nearbyPage"));
}

void UserApiTest::chargeMutationDisablesAuthenticatedNavigationUntilOutcome()
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
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("pending-nav-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto current = takeRequest(peer);
    reply(peer, current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    const auto facts = takeRequest(peer);
    QCOMPARE(facts.action, QStringLiteral("station.detail"));
    reply(peer, facts.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3, QStringLiteral("reserved")),
                                  chargerObject(8, 3), chargerObject(9, 3),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});

    auto *start = window.findChild<QPushButton *>(QStringLiteral("chargeStartButton"));
    auto *nearby = window.findChild<QPushButton *>(QStringLiteral("nearbyNavigationButton"));
    auto *currentNavigation =
        window.findChild<QPushButton *>(QStringLiteral("currentOrderNavigationButton"));
    auto *profile = window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"));
    QVERIFY(start != nullptr);
    QVERIFY(nearby != nullptr);
    QVERIFY(currentNavigation != nullptr);
    QVERIFY(profile != nullptr);
    QTRY_VERIFY(start->isEnabled());
    start->click();
    const auto mutation = takeRequest(peer);
    QCOMPARE(mutation.action, QStringLiteral("charge.start"));
    QVERIFY(!nearby->isEnabled());
    QVERIFY(!currentNavigation->isEnabled());
    QVERIFY(!profile->isEnabled());

    reply(peer, mutation.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("charging"))}});
    QTRY_VERIFY(currentNavigation->isEnabled());
    QTRY_VERIFY(profile->isEnabled());
    QVERIFY(!nearby->isEnabled());
    const auto refreshedFacts = takeRequest(peer);
    QCOMPARE(refreshedFacts.action, QStringLiteral("station.detail"));
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

void UserApiTest::chargeRefreshRejectsStaleSelectionAndCannotOverwriteNewDetail()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QTcpSocket *peer = server.nextPendingConnection();
    UserApi api(&client);
    NearbyPage page(&api, nullptr);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("refresh-generation-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());

    const ev::user::GeoPoint origin{39.958, 116.317};
    ev::user::Station station;
    station.stationId = 3;
    station.name = QStringLiteral("测试充电站3");
    station.address = QStringLiteral("北京市海淀区测试路3号");
    station.latitude = 39.953;
    station.longitude = 116.313;
    station.priceFenPerKwh = 135;
    station.chargerCount = 4;
    station.idleCount = 2;
    page.origin_ = origin;
    page.stations_ = {station};
    page.selectionGeneration_ = 20;

    page.refreshAfterCharge(origin, 3, 19);
    QTest::qWait(30);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
    page.refreshAfterCharge(origin, 3, 20);
    const auto backgroundRefresh = takeRequest(peer);
    QCOMPARE(backgroundRefresh.action, QStringLiteral("station.list"));

    page.requestStationDetail(station);
    const auto newDetail = takeRequest(peer);
    QCOMPARE(newDetail.action, QStringLiteral("station.detail"));
    reply(peer, backgroundRefresh.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), QJsonArray{stationObject(8, 0.2)}}});
    QTest::qWait(50);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("stationName_8")) == nullptr);
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("nearbySearchButton"))->isEnabled());

    reply(peer, newDetail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                  chargerObject(9, 3, QStringLiteral("charging")),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    QTRY_VERIFY(page.findChild<QPushButton *>(QStringLiteral("chargerButton_7")) != nullptr);
}

void UserApiTest::nearbyReplacementInvalidatesRememberedChargeSelection()
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
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("selection-invalidation-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto current = takeRequest(peer);
    reply(peer, current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearby = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    QVERIFY(nearby != nullptr);
    ev::user::Station station;
    station.stationId = 3;
    station.name = QStringLiteral("测试充电站3");
    station.address = QStringLiteral("北京市海淀区测试路3号");
    station.latitude = 39.953;
    station.longitude = 116.313;
    station.priceFenPerKwh = 135;
    station.chargerCount = 1;
    station.idleCount = 1;
    ev::user::Charger charger;
    charger.chargerId = 7;
    charger.stationId = 3;
    charger.code = QStringLiteral("T-7");
    charger.type = QStringLiteral("fast");
    charger.powerKw = 60.0;
    charger.status = QStringLiteral("idle");
    charger.updatedAt = QString::fromLatin1(kTimestamp);
    const ev::user::GeoPoint origin{39.958, 116.317};
    nearby->displayStations({origin, {station}});
    nearby->displayStationDetail({station, {charger}});
    auto *chargerButton =
        nearby->findChild<QPushButton *>(QStringLiteral("chargerButton_7"));
    QVERIFY(chargerButton != nullptr);
    chargerButton->click();
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    auto *chargeStatus = window.findChild<QLabel *>(QStringLiteral("chargeStatus"));
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("chargePage"));
    QTRY_VERIFY(reserve->isEnabled());

    nearby->displayStations({origin, {station}});
    QTRY_VERIFY(!reserve->isEnabled());
    QVERIFY(!chargeStatus->text().contains(QStringLiteral("已选择")));
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
    reply(peer, stale.requestId, false, QStringLiteral("UNKNOWN_STALE_CODE"),
          QStringLiteral("stale response must stay scoped"), QJsonObject{});
    QTRY_VERIFY(!search->isEnabled());
    QVERIFY(!status->text().contains(QStringLiteral("connection"), Qt::CaseInsensitive));
    reply(peer, active.requestId, false, QStringLiteral("UNKNOWN_ACTIVE_CODE"),
          QStringLiteral("invalid response code"), QJsonObject{});
    QTRY_VERIFY(search->isEnabled());
    QCOMPARE(status->text(), QStringLiteral("服务器响应无效，请重试"));
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
    reply(peer, staleDetailRequest.requestId, false, QStringLiteral("UNKNOWN_STALE_CODE"),
          QStringLiteral("stale invalid response"), QJsonObject{});
    QTRY_VERIFY(!stationButton->isEnabled());
    QVERIFY(!retainedCharger->isEnabled());
    QCOMPARE(detailStatus->text(), QStringLiteral("正在加载充电桩…"));
    reply(peer, activeDetailRequest.requestId, false, QStringLiteral("UNKNOWN_ACTIVE_CODE"),
          QStringLiteral("invalid response code"), QJsonObject{});
    QTRY_VERIFY(stationButton->isEnabled());
    QVERIFY(retainedCharger->isEnabled());
    QCOMPARE(detailStatus->text(), QStringLiteral("服务器响应无效，请重试"));
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
    QSignalSpy profileChanged(&api, &UserApi::sessionUserApplied);
    QSignalSpy mutationPending(&api, &UserApi::profileMutationPendingChanged);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("profile-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    profileChanged.clear();
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

void UserApiTest::profileRejectsInvalidFailureCodesAndPreservesLegalCodes()
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
    QSignalSpy uncertain(&api, &UserApi::profileReconciliationRequired);
    QSignalSpy reconciled(&api, &UserApi::profileReconciled);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("failure-code-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    const ev::user::User original = *api.sessionUser();

    const QString legalFailureId = api.loadProfile();
    QVERIFY(!legalFailureId.isEmpty());
    const auto legalFailureRequest = takeRequest(peer);
    reply(peer, legalFailureRequest.requestId, false, QStringLiteral("AUTH_REQUIRED"),
          QString(), QJsonObject{});
    QTRY_COMPARE(failures.size(), 1);
    auto failure = qvariant_cast<ev::user::ApiError>(failures.takeFirst().first());
    QCOMPARE(failure.code, QStringLiteral("AUTH_REQUIRED"));
    QCOMPARE(failure.message, QString());
    QCOMPARE(uncertain.size(), 0);

    const QString invalidOkId = api.updateNickname(QStringLiteral("不会生效"));
    QVERIFY(!invalidOkId.isEmpty());
    const auto invalidOkRequest = takeRequest(peer);
    reply(peer, invalidOkRequest.requestId, false, QStringLiteral("OK"), QString(),
          QJsonObject{});
    QTRY_COMPARE(failures.size(), 1);
    failure = qvariant_cast<ev::user::ApiError>(failures.takeFirst().first());
    QCOMPARE(failure.code, QStringLiteral("INVALID_RESPONSE"));
    QTRY_COMPARE(uncertain.size(), 1);
    QCOMPARE(api.sessionUser()->nickname, original.nickname);
    QCOMPARE(api.sessionUser()->balanceFen, original.balanceFen);
    QCOMPARE(api.sessionUser()->mobile, original.mobile);
    QCOMPARE(api.sessionUser()->avatarPath, original.avatarPath);
    QCOMPARE(api.sessionUser()->status, original.status);
    QCOMPARE(api.sessionUser()->registeredAt, original.registeredAt);

    QVERIFY(api.rechargeWallet(QStringLiteral("1.00")).isEmpty());
    QTRY_COMPARE(failures.size(), 1);
    QCOMPARE(qvariant_cast<ev::user::ApiError>(failures.takeFirst().first()).code,
             QStringLiteral("RECONCILIATION_REQUIRED"));
    const QString reconcileId = api.loadProfile();
    QVERIFY(!reconcileId.isEmpty());
    const auto reconcileRequest = takeRequest(peer);
    reply(peer, reconcileRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(reconciled.size(), 1);

    const QString unknownFailureId = api.rechargeWallet(QStringLiteral("1.00"));
    QVERIFY(!unknownFailureId.isEmpty());
    const auto unknownFailureRequest = takeRequest(peer);
    reply(peer, unknownFailureRequest.requestId, false, QStringLiteral("NEW_SERVER_CODE"),
          QStringLiteral("raw english server failure"), QJsonObject{});
    QTRY_COMPARE(failures.size(), 1);
    failure = qvariant_cast<ev::user::ApiError>(failures.takeFirst().first());
    QCOMPARE(failure.code, QStringLiteral("INVALID_RESPONSE"));
    QTRY_COMPARE(uncertain.size(), 2);
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
    QSignalSpy changed(&api, &UserApi::sessionUserApplied);

    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer);
    reply(peer, login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("correlation-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    changed.clear();

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
    connect(&api, &UserApi::sessionUserApplied, &api,
            [&api, &rechargeAttemptBeforeReconciledSignal](const ev::user::User &,
                                                            quint64, quint64) {
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

void UserApiTest::profilePageLocalizesProtocolAndUnknownErrors()
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
          QJsonObject{{QStringLiteral("token"), QStringLiteral("localized-error-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_VERIFY(api.sessionUser().has_value());
    page.refresh();
    const auto initialGet = takeRequest(peer);
    reply(peer, initialGet.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), userObject()}});

    auto *error = page.findChild<QLabel *>(QStringLiteral("profileError"));
    auto *retry = page.findChild<QPushButton *>(QStringLiteral("profileRetryButton"));
    QVERIFY(error != nullptr);
    QVERIFY(retry != nullptr);
    QTRY_VERIFY(page.findChild<QPushButton *>(QStringLiteral("rechargeButton"))->isEnabled());

    page.refresh();
    const auto protocolRequest = takeRequest(peer);
    QVERIFY(!protocolRequest.requestId.isEmpty());
    const QByteArray malformedEnvelope = ev::protocol::encodeFrame(QByteArrayLiteral("{}"));
    QCOMPARE(peer->write(malformedEnvelope), qint64{malformedEnvelope.size()});
    QVERIFY(peer->flush());
    QTRY_COMPARE(error->text(), QStringLiteral("通信协议异常，请重试"));
    QVERIFY(!error->text().contains(QRegularExpression(QStringLiteral("[A-Za-z]"))));
    QVERIFY(retry->isVisible());

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QTcpSocket *reconnectedPeer = server.nextPendingConnection();
    QVERIFY(reconnectedPeer != nullptr);
    page.refresh();
    const auto unknownRequest = takeRequest(reconnectedPeer);
    QCOMPARE(unknownRequest.action, QStringLiteral("user.get"));
    reply(reconnectedPeer, unknownRequest.requestId, false,
          QStringLiteral("NEW_SERVER_CODE"),
          QStringLiteral("raw english external message"), QJsonObject{});
    QTRY_COMPARE(error->text(), QStringLiteral("服务器返回的账户信息无效"));
    QVERIFY(!error->text().contains(QStringLiteral("raw english external message")));
}

void UserApiTest::uncertainReserveKeepsGlobalGateUntilCurrentAndFacts()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> firstPeer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(firstPeer.data());
    reply(firstPeer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("uncertain-gate-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(firstPeer.data());
    reply(firstPeer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    auto *nearbyNavigation =
        window.findChild<QPushButton *>(QStringLiteral("nearbyNavigationButton"));
    auto *currentNavigation =
        window.findChild<QPushButton *>(QStringLiteral("currentOrderNavigationButton"));
    auto *profileNavigation =
        window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"));
    QTRY_VERIFY(reserve->isEnabled());
    reserve->click();
    const auto mutation = takeRequest(firstPeer.data());
    QCOMPARE(mutation.action, QStringLiteral("charge.reserve"));
    firstPeer->abort();
    QTRY_VERIFY(!nearbyNavigation->isEnabled());
    QVERIFY(!currentNavigation->isEnabled());
    QVERIFY(!profileNavigation->isEnabled());
    QVERIFY(!reserve->isEnabled());

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> secondPeer(server.nextPendingConnection());
    const auto current = takeRequest(secondPeer.data());
    QCOMPARE(current.action, QStringLiteral("order.current"));
    reserve->click();
    QTest::qWait(30);
    QCOMPARE(secondPeer->bytesAvailable(), qint64{0});
    reply(secondPeer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    const auto firstFacts = takeRequest(secondPeer.data());
    const auto secondFacts = takeRequest(secondPeer.data());
    const auto detail = firstFacts.action == QStringLiteral("station.detail")
        ? firstFacts : secondFacts;
    const auto list = firstFacts.action == QStringLiteral("station.list")
        ? firstFacts : secondFacts;
    QCOMPARE(detail.action, QStringLiteral("station.detail"));
    QCOMPARE(list.action, QStringLiteral("station.list"));
    QVERIFY(!profileNavigation->isEnabled());
    reply(secondPeer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"),
                       QJsonArray{stationObject(3, 0.1, true, false)}}});
    QVERIFY(!profileNavigation->isEnabled());
    reply(secondPeer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3, QStringLiteral("reserved")),
                                  chargerObject(8, 3), chargerObject(9, 3),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    QTRY_VERIFY(profileNavigation->isEnabled());
    QVERIFY(currentNavigation->isEnabled());
    QVERIFY(!nearbyNavigation->isEnabled());
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeStartButton"))->isEnabled());
}

void UserApiTest::activeOrderExistsCannotBeBypassedBeforeReconciliation()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("active-exists-gate-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    auto *profileNavigation =
        window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"));
    reserve->click();
    const auto mutation = takeRequest(peer.data());
    reply(peer.data(), mutation.requestId, false, QStringLiteral("ACTIVE_ORDER_EXISTS"),
          QStringLiteral("active"), QJsonObject{});
    const auto current = takeRequest(peer.data());
    QCOMPARE(current.action, QStringLiteral("order.current"));
    QVERIFY(!profileNavigation->isEnabled());
    emit nearbyPage->chargerSelected({origin, station, chargerValue(), 9'999});
    QVERIFY(!reserve->isEnabled());
    reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    const auto firstFacts = takeRequest(peer.data());
    const auto secondFacts = takeRequest(peer.data());
    const auto detail = firstFacts.action == QStringLiteral("station.detail")
        ? firstFacts : secondFacts;
    const auto list = firstFacts.action == QStringLiteral("station.list")
        ? firstFacts : secondFacts;
    reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3, QStringLiteral("reserved")),
                                  chargerObject(8, 3), chargerObject(9, 3),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"),
                       QJsonArray{stationObject(3, 0.1, true, false)}}});
    QTRY_VERIFY(profileNavigation->isEnabled());
}

void UserApiTest::backgroundNearbyReplacementCannotInvalidatePendingReserve()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("background-replace-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    reserve->click();
    const auto mutation = takeRequest(peer.data());
    ev::user::Station replacement = station;
    replacement.name = QStringLiteral("后台新站点快照");
    nearbyPage->displayStations({{40.0, 116.4}, {replacement}});
    QVERIFY(!reserve->isEnabled());

    reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    auto *status = window.findChild<QLabel *>(QStringLiteral("chargeStatus"));
    QTRY_COMPARE(status->text(), QStringLiteral("已预约"));
    const auto detail = takeRequest(peer.data());
    QCOMPARE(detail.action, QStringLiteral("station.detail"));
    reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3, QStringLiteral("reserved")),
                                  chargerObject(8, 3), chargerObject(9, 3),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeStartButton"))->isEnabled());
}

void UserApiTest::chargeRefreshPreservesIdleSelectionInBothResponseOrders()
{
    for (const bool listFirst : {true, false}) {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
        phone->setText(QString::fromLatin1(kMobile));
        loginButton->click();
        const auto login = takeRequest(peer.data());
        reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("refresh-order-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(peer.data());
        reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
        const ev::user::GeoPoint origin{39.958, 116.317};
        const auto station = stationValue();
        nearbyPage->displayStations({origin, {station}});
        nearbyPage->displayStationDetail({station, {chargerValue()}});
        nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
        auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
        reserve->click();
        const auto mutation = takeRequest(peer.data());
        reply(peer.data(), mutation.requestId, false, QStringLiteral("DB_BUSY"),
              QStringLiteral("busy"), QJsonObject{});
        const auto current = takeRequest(peer.data());
        reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        const auto firstFacts = takeRequest(peer.data());
        const auto secondFacts = takeRequest(peer.data());
        const auto detail = firstFacts.action == QStringLiteral("station.detail")
            ? firstFacts : secondFacts;
        const auto list = firstFacts.action == QStringLiteral("station.list")
            ? firstFacts : secondFacts;
        const auto replyList = [&] {
            reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("stations"),
                               QJsonArray{stationObject(3, 0.1, true, false)}}});
        };
        const auto replyDetail = [&] {
            reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                              {QStringLiteral("chargers"),
                               QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                          chargerObject(9, 3, QStringLiteral("charging")),
                                          chargerObject(10, 3, QStringLiteral("fault"))}}});
        };
        if (listFirst) {
            replyList();
            QVERIFY(!reserve->isEnabled());
            replyDetail();
        } else {
            replyDetail();
            QTRY_VERIFY(reserve->isEnabled());
            replyList();
        }
        QTRY_VERIFY(reserve->isEnabled());
        QTest::qWait(30);
        QVERIFY(reserve->isEnabled());
    }
}

void UserApiTest::chargeRefreshSuccessCannotEraseFactsFailure()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("facts-failure-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    reserve->click();
    const auto mutation = takeRequest(peer.data());
    reply(peer.data(), mutation.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    const auto current = takeRequest(peer.data());
    reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    const auto firstFacts = takeRequest(peer.data());
    const auto secondFacts = takeRequest(peer.data());
    const auto detail = firstFacts.action == QStringLiteral("station.detail")
        ? firstFacts : secondFacts;
    const auto list = firstFacts.action == QStringLiteral("station.list")
        ? firstFacts : secondFacts;
    reply(peer.data(), detail.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    auto *retry = window.findChild<QPushButton *>(QStringLiteral("chargeRetryButton"));
    auto *error = window.findChild<QLabel *>(QStringLiteral("chargeError"));
    QTRY_VERIFY(retry->isVisible());
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"),
                       QJsonArray{stationObject(3, 0.1, true, false)}}});
    QTest::qWait(50);
    QVERIFY(retry->isVisible());
    QVERIFY(error->text().contains(QStringLiteral("服务繁忙")));
    QVERIFY(!reserve->isEnabled());
    QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"))->isEnabled());
}

void UserApiTest::disconnectIgnoresChargeListReplayBeforeFreshReconciliation()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> firstPeer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    auto *loginButton = window.findChild<QPushButton *>(QStringLiteral("loginButton"));
    phone->setText(QString::fromLatin1(kMobile));
    loginButton->click();
    const auto login = takeRequest(firstPeer.data());
    reply(firstPeer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("list-replay-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(firstPeer.data());
    reply(firstPeer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    reserve->click();
    const auto mutation = takeRequest(firstPeer.data());
    reply(firstPeer.data(), mutation.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    const auto current = takeRequest(firstPeer.data());
    reply(firstPeer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    const auto firstFacts = takeRequest(firstPeer.data());
    const auto secondFacts = takeRequest(firstPeer.data());
    const auto oldDetail = firstFacts.action == QStringLiteral("station.detail")
        ? firstFacts : secondFacts;
    const auto oldList = firstFacts.action == QStringLiteral("station.list")
        ? firstFacts : secondFacts;
    firstPeer->abort();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> secondPeer(server.nextPendingConnection());
    const auto freshCurrent = takeRequest(secondPeer.data());
    QCOMPARE(freshCurrent.action, QStringLiteral("order.current"));
    QVERIFY(freshCurrent.requestId != current.requestId);
    QTest::qWait(30);
    QCOMPARE(secondPeer->bytesAvailable(), qint64{0});
    Q_UNUSED(oldDetail);
    Q_UNUSED(oldList);
    reply(secondPeer.data(), freshCurrent.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    const auto newFirstFacts = takeRequest(secondPeer.data());
    const auto newSecondFacts = takeRequest(secondPeer.data());
    const auto newDetail = newFirstFacts.action == QStringLiteral("station.detail")
        ? newFirstFacts : newSecondFacts;
    const auto newList = newFirstFacts.action == QStringLiteral("station.list")
        ? newFirstFacts : newSecondFacts;
    QCOMPARE(newDetail.action, QStringLiteral("station.detail"));
    QCOMPARE(newList.action, QStringLiteral("station.list"));
    QVERIFY(!reserve->isEnabled());
    reply(secondPeer.data(), newList.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"),
                       QJsonArray{stationObject(3, 0.1, true, false)}}});
    QVERIFY(!reserve->isEnabled());
    reply(secondPeer.data(), newDetail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                  chargerObject(9, 3, QStringLiteral("charging")),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    QTRY_VERIFY(reserve->isEnabled());
}

void UserApiTest::foregroundOriginReplacementRemovesOldStationControls()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    QSignalSpy loginSucceeded(&api, &UserApi::loginSucceeded);
    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("origin-replacement-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(loginSucceeded.count(), 1);

    TencentMapClient map(QStringLiteral("test-key"), nullptr,
                         QUrl(QStringLiteral("https://offline.invalid/geocode")), 20);
    NearbyPage page(&api, &map);
    page.show();
    const ev::user::GeoPoint originalOrigin{39.958, 116.317};
    const auto station = stationValue();
    page.displayStations({originalOrigin, {station}});
    page.displayStationDetail({station, {chargerValue()}});
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("stationButton_3")) != nullptr);
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("chargerButton_7")) != nullptr);

    page.pendingGeocodeId_ = QStringLiteral("foreground-geocode");
    emit map.geocodeSucceeded(QStringLiteral("foreground-geocode"), {40.0, 116.4});
    const auto refreshedList = takeRequest(peer.data());
    QCOMPARE(refreshedList.action, QStringLiteral("station.list"));

    auto *oldStation = page.findChild<QPushButton *>(QStringLiteral("stationButton_3"));
    auto *oldCharger = page.findChild<QPushButton *>(QStringLiteral("chargerButton_7"));
    QVERIFY(oldStation == nullptr || !oldStation->isEnabled());
    QVERIFY(oldCharger == nullptr || !oldCharger->isEnabled());
}

void UserApiTest::activeOrderExistsChargingStatesWaitForMatchingFacts()
{
    for (const bool stopped : {false, true}) {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        phone->setText(QString::fromLatin1(kMobile));
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        const auto login = takeRequest(peer.data());
        reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("active-state-gate-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(peer.data());
        reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

        auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
        const ev::user::GeoPoint origin{39.958, 116.317};
        const auto station = stationValue();
        nearbyPage->displayStations({origin, {station}});
        nearbyPage->displayStationDetail({station, {chargerValue()}});
        nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
        auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
        reserve->click();
        const auto mutation = takeRequest(peer.data());
        reply(peer.data(), mutation.requestId, false, QStringLiteral("ACTIVE_ORDER_EXISTS"),
              QStringLiteral("active"), QJsonObject{});
        const auto current = takeRequest(peer.data());
        reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("charging"), stopped)}});
        const auto firstFacts = takeRequest(peer.data());
        const auto secondFacts = takeRequest(peer.data());
        const auto detail = firstFacts.action == QStringLiteral("station.detail")
            ? firstFacts : secondFacts;
        const auto list = firstFacts.action == QStringLiteral("station.list")
            ? firstFacts : secondFacts;
        auto *action = window.findChild<QPushButton *>(stopped
            ? QStringLiteral("chargeSettleButton") : QStringLiteral("chargeStopButton"));
        auto *profile = window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"));
        QVERIFY(!profile->isEnabled());
        QVERIFY(!action->isEnabled());
        reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3, stopped
                               ? QStringLiteral("idle") : QStringLiteral("charging")),
                                      chargerObject(8, 3),
                                      chargerObject(9, 3, stopped
                                          ? QStringLiteral("charging") : QStringLiteral("idle")),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
        QTRY_VERIFY(profile->isEnabled());
        QTRY_VERIFY(action->isEnabled());
        reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
    }
}

void UserApiTest::terminalBackWaitsForNearbyFactsToCommit()
{
    for (const bool settle : {false, true}) {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        phone->setText(QString::fromLatin1(kMobile));
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        const auto login = takeRequest(peer.data());
        reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("terminal-refresh-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(peer.data());
        reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

        auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
        const ev::user::GeoPoint origin{39.958, 116.317};
        auto station = stationValue();
        const QString initialStatus = settle ? QStringLiteral("charging")
                                             : QStringLiteral("reserved");
        nearbyPage->displayStations({origin, {station}});
        nearbyPage->displayStationDetail({station, {chargerValue(initialStatus)}});
        nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
        auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
        ev::user::StationSelection remembered{origin, station, chargerValue(initialStatus), 3};
        chargePage->enterOrder(settle
            ? orderValue(QStringLiteral("charging"), true)
            : orderValue(QStringLiteral("reserved")), remembered);
        auto *mutationButton = window.findChild<QPushButton *>(settle
            ? QStringLiteral("chargeSettleButton") : QStringLiteral("chargeCancelButton"));
        QTRY_VERIFY(mutationButton->isEnabled());
        mutationButton->click();
        const auto mutation = takeRequest(peer.data());
        const QJsonObject responseData = settle
            ? QJsonObject{{QStringLiteral("order"), canonicalOrderObject(QStringLiteral("completed"))},
                          {QStringLiteral("balanceFen"), 10'000}}
            : QJsonObject{{QStringLiteral("order"), canonicalOrderObject(QStringLiteral("cancelled"))}};
        reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(), responseData);
        const auto firstRefresh = takeRequest(peer.data());
        const auto secondRefresh = takeRequest(peer.data());
        const auto detail = firstRefresh.action == QStringLiteral("station.detail")
            ? firstRefresh : secondRefresh;
        const auto list = firstRefresh.action == QStringLiteral("station.list")
            ? firstRefresh : secondRefresh;
        auto *back = window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"));
        auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
        QVERIFY(!back->isEnabled());
        back->click();
        QCOMPARE(pages->currentWidget(), static_cast<QWidget *>(chargePage));

        QJsonObject refreshedStation = stationObject(3, 0.1, true, false);
        refreshedStation.insert(QStringLiteral("idleCount"), 3);
        if (settle) {
            reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                              {QStringLiteral("chargers"),
                               QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                          chargerObject(9, 3, QStringLiteral("charging")),
                                          chargerObject(10, 3, QStringLiteral("fault"))}}});
            QVERIFY(!back->isEnabled());
            reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("stations"), QJsonArray{refreshedStation}}});
        } else {
            reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("stations"), QJsonArray{refreshedStation}}});
            QVERIFY(!back->isEnabled());
            reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                              {QStringLiteral("chargers"),
                               QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                          chargerObject(9, 3, QStringLiteral("charging")),
                                          chargerObject(10, 3, QStringLiteral("fault"))}}});
        }
        QTRY_VERIFY(back->isEnabled());
        back->click();
        QTRY_COMPARE(pages->currentWidget(), static_cast<QWidget *>(nearbyPage));
        QTRY_VERIFY(nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->text()
                        .contains(QStringLiteral("空闲")));
        bool foundCount = false;
        for (QLabel *label : nearbyPage->findChildren<QLabel *>()) {
            foundCount = foundCount || label->text().contains(QStringLiteral("共 4 桩 / 空闲 3"));
        }
        QVERIFY(foundCount);
    }
}

void UserApiTest::terminalAndNullBackRemainReachableAcrossReconnect()
{
    for (const bool settle : {false, true}) {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> firstPeer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        phone->setText(QString::fromLatin1(kMobile));
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        const auto login = takeRequest(firstPeer.data());
        reply(firstPeer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("terminal-reconnect-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(firstPeer.data());
        reply(firstPeer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
        const ev::user::GeoPoint origin{39.958, 116.317};
        const auto station = stationValue();
        const QString initialStatus = settle ? QStringLiteral("charging")
                                             : QStringLiteral("reserved");
        nearbyPage->displayStations({origin, {station}});
        nearbyPage->displayStationDetail({station, {chargerValue(initialStatus)}});
        nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
        auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
        ev::user::StationSelection remembered{
            origin, station, chargerValue(initialStatus), 3};
        chargePage->enterOrder(settle
            ? orderValue(QStringLiteral("charging"), true)
            : orderValue(QStringLiteral("reserved")),
            remembered);
        auto *mutationButton = window.findChild<QPushButton *>(settle
            ? QStringLiteral("chargeSettleButton") : QStringLiteral("chargeCancelButton"));
        mutationButton->click();
        const auto mutation = takeRequest(firstPeer.data());
        const QJsonObject responseData = settle
            ? QJsonObject{{QStringLiteral("order"), canonicalOrderObject(QStringLiteral("completed"))},
                          {QStringLiteral("balanceFen"), 10'000}}
            : QJsonObject{{QStringLiteral("order"), canonicalOrderObject(QStringLiteral("cancelled"))}};
        reply(firstPeer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(), responseData);
        const auto oldDetail = takeRequest(firstPeer.data());
        const auto oldList = takeRequest(firstPeer.data());
        QVERIFY(oldDetail.action == QStringLiteral("station.detail")
                || oldList.action == QStringLiteral("station.detail"));
        firstPeer->abort();
        auto *back = window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"));
        auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("mainPages"));
        QTRY_VERIFY(!back->isEnabled());
        back->click();
        QCOMPARE(pages->currentWidget(), static_cast<QWidget *>(chargePage));

        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> secondPeer(server.nextPendingConnection());
        const auto current = takeRequest(secondPeer.data());
        reply(secondPeer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        const auto firstRefresh = takeRequest(secondPeer.data());
        const auto secondRefresh = takeRequest(secondPeer.data());
        const auto detail = firstRefresh.action == QStringLiteral("station.detail")
            ? firstRefresh : secondRefresh;
        const auto list = firstRefresh.action == QStringLiteral("station.list")
            ? firstRefresh : secondRefresh;
        reply(secondPeer.data(), detail.requestId, false, QStringLiteral("DB_BUSY"),
              QStringLiteral("busy"), QJsonObject{});
        auto *retry = window.findChild<QPushButton *>(QStringLiteral("chargeRetryButton"));
        QTRY_VERIFY(retry->isVisible());
        QVERIFY(!back->isEnabled());
        reply(secondPeer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
        QTRY_VERIFY(retry->isEnabled());
        retry->click();
        const auto retryCurrent = takeRequest(secondPeer.data());
        reply(secondPeer.data(), retryCurrent.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        const auto retryFirst = takeRequest(secondPeer.data());
        const auto retrySecond = takeRequest(secondPeer.data());
        const auto retryDetail = retryFirst.action == QStringLiteral("station.detail")
            ? retryFirst : retrySecond;
        const auto retryList = retryFirst.action == QStringLiteral("station.list")
            ? retryFirst : retrySecond;
        QCOMPARE(retryDetail.action, QStringLiteral("station.detail"));
        QCOMPARE(retryList.action, QStringLiteral("station.list"));
        reply(secondPeer.data(), retryDetail.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                      chargerObject(9, 3, QStringLiteral("charging")),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
        reply(secondPeer.data(), retryList.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
        QTRY_VERIFY(back->isEnabled());
    }

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> firstPeer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(firstPeer.data());
    reply(firstPeer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("null-reconnect-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(firstPeer.data());
    reply(firstPeer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    nearbyPage->displayStations({{40.0, 116.4}, {station}});
    auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
    auto *back = window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"));
    QTRY_VERIFY(back->isEnabled());
    firstPeer->abort();
    QTRY_VERIFY(!back->isEnabled());
    back->click();
    QCOMPARE(window.findChild<QStackedWidget *>(QStringLiteral("mainPages"))->currentWidget(),
             static_cast<QWidget *>(chargePage));
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> secondPeer(server.nextPendingConnection());
    const auto current = takeRequest(secondPeer.data());
    reply(secondPeer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_VERIFY(back->isEnabled());
}

void UserApiTest::terminalWithoutMatchingNearbyContextDoesNotStrandBack()
{
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        phone->setText(QString::fromLatin1(kMobile));
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        const auto login = takeRequest(peer.data());
        reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("no-origin-terminal-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(peer.data());
        reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("reserved"))}});
        const auto guardFacts = takeRequest(peer.data());
        reply(peer.data(), guardFacts.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3, QStringLiteral("reserved")),
                                      chargerObject(8, 3), chargerObject(9, 3),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
        auto *cancel = window.findChild<QPushButton *>(QStringLiteral("chargeCancelButton"));
        QTRY_VERIFY(cancel->isEnabled());
        cancel->click();
        const auto mutation = takeRequest(peer.data());
        reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("cancelled"))}});
        const auto optionalFacts = takeRequest(peer.data());
        QCOMPARE(optionalFacts.action, QStringLiteral("station.detail"));
        reply(peer.data(), optionalFacts.requestId, false, QStringLiteral("DB_BUSY"),
              QStringLiteral("busy"), QJsonObject{});
        auto *back = window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"));
        QTRY_VERIFY(back->isEnabled());
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("profileNavigationButton"))->isEnabled());
    }

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("lost-nearby-terminal-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue(QStringLiteral("reserved"))}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
    chargePage->enterOrder(orderValue(QStringLiteral("reserved")),
                           ev::user::StationSelection{
                               origin, station, chargerValue(QStringLiteral("reserved")), 3});
    auto *cancel = window.findChild<QPushButton *>(QStringLiteral("chargeCancelButton"));
    cancel->click();
    const auto mutation = takeRequest(peer.data());
    nearbyPage->displayStations({{40.0, 116.4}, {station}});
    reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"),
                       canonicalOrderObject(QStringLiteral("cancelled"))}});
    const auto facts = takeRequest(peer.data());
    QCOMPARE(facts.action, QStringLiteral("station.detail"));
    reply(peer.data(), facts.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                  chargerObject(9, 3, QStringLiteral("charging")),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
}

void UserApiTest::exitRefreshResolvesOnceWhenContextChangesOrStationIsMissing()
{
    for (const bool detailFirst : {false, true}) {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        phone->setText(QString::fromLatin1(kMobile));
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        const auto login = takeRequest(peer.data());
        reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("missing-station-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(peer.data());
        reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

        auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
        const ev::user::GeoPoint origin{39.958, 116.317};
        const auto station = stationValue();
        nearbyPage->displayStations({origin, {station}});
        nearbyPage->displayStationDetail(
            {station, {chargerValue(QStringLiteral("reserved"))}});
        nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
        auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
        chargePage->enterOrder(
            orderValue(QStringLiteral("reserved")),
            ev::user::StationSelection{
                origin, station, chargerValue(QStringLiteral("reserved")), 3});

        QSignalSpy committed(nearbyPage, &NearbyPage::chargeRefreshCommitted);
        QSignalSpy failed(nearbyPage, &NearbyPage::chargeRefreshFailed);
        QSignalSpy unavailable(nearbyPage, &NearbyPage::chargeRefreshUnavailable);
        window.findChild<QPushButton *>(QStringLiteral("chargeCancelButton"))->click();
        const auto mutation = takeRequest(peer.data());
        reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("cancelled"))}});
        const auto firstRefresh = takeRequest(peer.data());
        const auto secondRefresh = takeRequest(peer.data());
        const auto detail = firstRefresh.action == QStringLiteral("station.detail")
            ? firstRefresh : secondRefresh;
        const auto list = firstRefresh.action == QStringLiteral("station.list")
            ? firstRefresh : secondRefresh;
        QCOMPARE(detail.action, QStringLiteral("station.detail"));
        QCOMPARE(list.action, QStringLiteral("station.list"));

        const auto replyDetail = [&] {
            reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                              {QStringLiteral("chargers"),
                               QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                          chargerObject(9, 3, QStringLiteral("charging")),
                                          chargerObject(10, 3, QStringLiteral("fault"))}}});
        };
        const auto replyListWithoutTarget = [&] {
            reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("stations"),
                               QJsonArray{stationObject(4, 0.2, true, false)}}});
        };
        if (detailFirst) {
            replyDetail();
            replyListWithoutTarget();
        } else {
            replyListWithoutTarget();
            replyDetail();
        }

        QTRY_COMPARE(unavailable.size(), 1);
        QCOMPARE(committed.size(), 0);
        QCOMPARE(failed.size(), 0);
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
        QVERIFY(nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7")) == nullptr);
        QVERIFY(nearbyPage->findChild<QPushButton *>(QStringLiteral("stationButton_3")) == nullptr);
        QVERIFY(nearbyPage->findChild<QPushButton *>(QStringLiteral("stationButton_4")) != nullptr);
        QTest::qWait(20);
        QCOMPARE(unavailable.size(), 1);
    }

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("late-geocode-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail(
        {station, {chargerValue(QStringLiteral("reserved"))}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
    chargePage->enterOrder(
        orderValue(QStringLiteral("reserved")),
        ev::user::StationSelection{
            origin, station, chargerValue(QStringLiteral("reserved")), 3});
    QSignalSpy committed(nearbyPage, &NearbyPage::chargeRefreshCommitted);
    QSignalSpy failed(nearbyPage, &NearbyPage::chargeRefreshFailed);
    QSignalSpy unavailable(nearbyPage, &NearbyPage::chargeRefreshUnavailable);
    window.findChild<QPushButton *>(QStringLiteral("chargeCancelButton"))->click();
    const auto mutation = takeRequest(peer.data());
    reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"),
                       canonicalOrderObject(QStringLiteral("cancelled"))}});
    const auto firstRefresh = takeRequest(peer.data());
    const auto secondRefresh = takeRequest(peer.data());
    const auto detail = firstRefresh.action == QStringLiteral("station.detail")
        ? firstRefresh : secondRefresh;
    nearbyPage->pendingGeocodeId_ = QStringLiteral("late-exit-geocode");
    emit nearbyPage->mapClient_->geocodeSucceeded(
        QStringLiteral("late-exit-geocode"), ev::user::GeoPoint{40.0, 116.4});
    const auto replacementList = takeRequest(peer.data());
    QCOMPARE(replacementList.action, QStringLiteral("station.list"));
    QTRY_COMPARE(unavailable.size(), 1);
    QCOMPARE(committed.size(), 0);
    QCOMPARE(failed.size(), 0);
    reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                  chargerObject(9, 3, QStringLiteral("charging")),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    reply(peer.data(), replacementList.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"),
                       QJsonArray{stationObject(4, 0.2, true, false)}}});
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
    QVERIFY(nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7")) == nullptr);
    QCOMPARE(unavailable.size(), 1);
    QCOMPARE(committed.size(), 0);
    QCOMPARE(failed.size(), 0);
}

void UserApiTest::chargeDerivedForecastIsNotReplayedAfterDisconnect()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> firstPeer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(firstPeer.data());
    reply(firstPeer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("forecast-replay-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(firstPeer.data());
    reply(firstPeer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    QVector<ev::user::Station> stations;
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        auto station = stationValue();
        station.stationId = stationId;
        station.name = QStringLiteral("测试充电站%1").arg(stationId);
        station.forecastEnabled = true;
        stations.push_back(station);
    }
    const auto selectedStation = stations.at(2);
    nearbyPage->displayStations({origin, stations});
    nearbyPage->displayStationDetail({selectedStation, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    reserve->click();
    const auto mutation = takeRequest(firstPeer.data());
    reply(firstPeer.data(), mutation.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    const auto current = takeRequest(firstPeer.data());
    reply(firstPeer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    const auto firstFacts = takeRequest(firstPeer.data());
    const auto secondFacts = takeRequest(firstPeer.data());
    const auto detail = firstFacts.action == QStringLiteral("station.detail")
        ? firstFacts : secondFacts;
    const auto list = firstFacts.action == QStringLiteral("station.list")
        ? firstFacts : secondFacts;
    reply(firstPeer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                  chargerObject(9, 3, QStringLiteral("charging")),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    QJsonArray stationArray;
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        stationArray.append(stationObject(stationId, stationId / 10.0, true, true));
    }
    reply(firstPeer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), stationArray}});
    const auto forecast = takeRequest(firstPeer.data());
    QCOMPARE(forecast.action, QStringLiteral("forecast.latest"));

    firstPeer->abort();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> secondPeer(server.nextPendingConnection());
    const auto freshCurrent = takeRequest(secondPeer.data());
    QCOMPARE(freshCurrent.action, QStringLiteral("order.current"));
    QTest::qWait(30);
    QCOMPARE(secondPeer->bytesAvailable(), qint64{0});
}

void UserApiTest::differentAuthoritativeOrderClearsRememberedSelectionCopies()
{
    const auto establishDifferentOrder = [](MainWindow &window, QTcpSocket *peer) {
        auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
        const ev::user::GeoPoint origin{39.958, 116.317};
        const auto station = stationValue();
        nearbyPage->displayStations({origin, {station}});
        nearbyPage->displayStationDetail({station, {chargerValue()}});
        nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
        window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"))->click();
        const auto mutation = takeRequest(peer);
        reply(peer, mutation.requestId, false, QStringLiteral("ACTIVE_ORDER_EXISTS"),
              QStringLiteral("active"), QJsonObject{});
        const auto current = takeRequest(peer);
        reply(peer, current.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("reserved"), false, 8)}});
        const auto detail = takeRequest(peer);
        QCOMPARE(detail.action, QStringLiteral("station.detail"));
        reply(peer, detail.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3),
                                      chargerObject(8, 3, QStringLiteral("reserved")),
                                      chargerObject(9, 3),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeStartButton"))->isEnabled());
    };

    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> firstPeer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        phone->setText(QString::fromLatin1(kMobile));
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        const auto login = takeRequest(firstPeer.data());
        reply(firstPeer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("mismatch-page-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(firstPeer.data());
        reply(firstPeer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        establishDifferentOrder(window, firstPeer.data());
        firstPeer->abort();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> secondPeer(server.nextPendingConnection());
        const auto current = takeRequest(secondPeer.data());
        reply(secondPeer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        QTest::qWait(30);
        QCOMPARE(secondPeer->bytesAvailable(), qint64{0});
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
    }

    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        MainWindow window(usableConfig(server.serverPort()));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
        phone->setText(QString::fromLatin1(kMobile));
        window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
        const auto login = takeRequest(peer.data());
        reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("token"), QStringLiteral("mismatch-main-token")},
                          {QStringLiteral("user"), userObject()}});
        const auto guard = takeRequest(peer.data());
        reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        establishDifferentOrder(window, peer.data());
        auto *api = window.findChild<UserApi *>();
        (void)api->loadCurrentOrder(77, 77, ev::user::ChargeOperation::Guard);
        const auto externalCurrent = takeRequest(peer.data());
        reply(peer.data(), externalCurrent.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("reserved"))}});
        auto *currentNavigation =
            window.findChild<QPushButton *>(QStringLiteral("currentOrderNavigationButton"));
        QTRY_VERIFY(currentNavigation->isEnabled());
        currentNavigation->click();
        const auto pageCurrent = takeRequest(peer.data());
        reply(peer.data(), pageCurrent.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("reserved"))}});
        const auto detail = takeRequest(peer.data());
        QCOMPARE(detail.action, QStringLiteral("station.detail"));
        QTest::qWait(30);
        QCOMPARE(peer->bytesAvailable(), qint64{0});
    }
}

void UserApiTest::externalActiveOrderAuthorityInvalidatesVisibleSelection()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("external-authority-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    QTRY_VERIFY(reserve->isEnabled());

    auto *api = window.findChild<UserApi *>();
    (void)api->loadCurrentOrder(76, 76, ev::user::ChargeOperation::Guard);
    const auto externalNull = takeRequest(peer.data());
    reply(peer.data(), externalNull.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_VERIFY(reserve->isEnabled());

    (void)api->loadCurrentOrder(77, 77, ev::user::ChargeOperation::Guard);
    const auto externalCurrent = takeRequest(peer.data());
    reply(peer.data(), externalCurrent.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"),
                       canonicalOrderObject(QStringLiteral("reserved"), false, 8)}});
    QTRY_VERIFY(!reserve->isEnabled());
    auto *currentNavigation =
        window.findChild<QPushButton *>(QStringLiteral("currentOrderNavigationButton"));
    QVERIFY(!currentNavigation->isEnabled());
    const auto authorityFacts = takeRequest(peer.data());
    QCOMPARE(authorityFacts.action, QStringLiteral("station.detail"));
    QJsonObject authorityStation = stationObject(3, 0.0, false);
    authorityStation.insert(QStringLiteral("idleCount"), 1);
    reply(peer.data(), authorityFacts.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), authorityStation},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(7, 3),
                                  chargerObject(8, 3, QStringLiteral("reserved")),
                                  chargerObject(9, 3, QStringLiteral("charging")),
                                  chargerObject(10, 3, QStringLiteral("fault"))}}});
    QTRY_VERIFY(currentNavigation->isEnabled());
    QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeStartButton"))
                    ->isEnabled());

    (void)api->loadCurrentOrder(78, 78, ev::user::ChargeOperation::Guard);
    const auto laterNull = takeRequest(peer.data());
    reply(peer.data(), laterNull.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_VERIFY(!reserve->isEnabled());
    QTest::qWait(20);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
}

void UserApiTest::exitRefreshFirstFailureWins_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("detail-failure-then-late-list-success-and-retry") << 0;
    QTest::newRow("missing-charger-then-late-list-failure-and-geocode") << 1;
    QTest::newRow("list-failure-then-late-detail-success") << 2;
    QTest::newRow("list-failure-then-late-detail-failure") << 3;
    QTest::newRow("settle-detail-failure-then-late-list-success") << 4;
    QTest::newRow("settle-list-failure-then-late-detail-success") << 5;
}

void UserApiTest::exitRefreshFirstFailureWins()
{
    QFETCH(int, scenario);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("first-failure-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    const bool settle = scenario >= 4;
    const QString selectedStatus = settle ? QStringLiteral("charging")
                                          : QStringLiteral("reserved");
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail(
        {station, {chargerValue(selectedStatus)}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
    chargePage->enterOrder(
        settle ? orderValue(QStringLiteral("charging"), true)
               : orderValue(QStringLiteral("reserved")),
        ev::user::StationSelection{
            origin, station, chargerValue(selectedStatus), 3});
    QSignalSpy committed(nearbyPage, &NearbyPage::chargeRefreshCommitted);
    QSignalSpy failed(nearbyPage, &NearbyPage::chargeRefreshFailed);
    QSignalSpy unavailable(nearbyPage, &NearbyPage::chargeRefreshUnavailable);
    window.findChild<QPushButton *>(settle ? QStringLiteral("chargeSettleButton")
                                           : QStringLiteral("chargeCancelButton"))->click();
    const auto mutation = takeRequest(peer.data());
    QJsonObject mutationData{{QStringLiteral("order"),
                              canonicalOrderObject(settle ? QStringLiteral("completed")
                                                          : QStringLiteral("cancelled"))}};
    if (settle) {
        mutationData.insert(QStringLiteral("balanceFen"), 10'000);
    }
    reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(), mutationData);
    const auto firstRefresh = takeRequest(peer.data());
    const auto secondRefresh = takeRequest(peer.data());
    const auto detail = firstRefresh.action == QStringLiteral("station.detail")
        ? firstRefresh : secondRefresh;
    const auto list = firstRefresh.action == QStringLiteral("station.list")
        ? firstRefresh : secondRefresh;

    const auto validDetail = [&] {
        reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                      chargerObject(9, 3, QStringLiteral("charging")),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
    };
    if (scenario == 0 || scenario == 4) {
        reply(peer.data(), detail.requestId, false, QStringLiteral("DB_BUSY"),
              QStringLiteral("busy"), QJsonObject{});
    } else if (scenario == 1) {
        reply(peer.data(), detail.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(8, 3), chargerObject(9, 3),
                                      chargerObject(10, 3, QStringLiteral("charging")),
                                      chargerObject(11, 3, QStringLiteral("fault"))}}});
    } else {
        reply(peer.data(), list.requestId, false, QStringLiteral("DB_BUSY"),
              QStringLiteral("busy"), QJsonObject{});
    }

    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(committed.size(), 0);
    QCOMPARE(unavailable.size(), 0);
    const quint64 failedAttempt = failed.at(0).at(0).toULongLong();
    const quint64 failedGeneration = failed.at(0).at(1).toULongLong();
    QCOMPARE(failed.at(0).at(2).toLongLong(), qint64{3});
    QVERIFY(failedAttempt != 0);
    QVERIFY(failedGeneration != 0);
    auto *retry = window.findChild<QPushButton *>(QStringLiteral("chargeRetryButton"));
    QTRY_VERIFY(retry->isVisible());
    QTRY_VERIFY(retry->isEnabled());
    QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());

    if (scenario == 0) {
        retry->click();
        const auto current = takeRequest(peer.data());
        reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        const auto retryFirst = takeRequest(peer.data());
        const auto retrySecond = takeRequest(peer.data());
        const auto retryDetail = retryFirst.action == QStringLiteral("station.detail")
            ? retryFirst : retrySecond;
        const auto retryList = retryFirst.action == QStringLiteral("station.list")
            ? retryFirst : retrySecond;
        reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
        QTest::qWait(20);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(committed.size(), 0);
        QCOMPARE(unavailable.size(), 0);
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
        chargePage->nearbyRefreshCommitted(failedAttempt, failedGeneration, 3);
        chargePage->nearbyRefreshFailed(
            failedAttempt, failedGeneration, 3,
            {QStringLiteral("old"), QStringLiteral("DB_BUSY"), QStringLiteral("old")});
        chargePage->nearbyRefreshUnavailable(failedAttempt, failedGeneration, 3);
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
        reply(peer.data(), retryDetail.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                      chargerObject(9, 3, QStringLiteral("charging")),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
        reply(peer.data(), retryList.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
        QTRY_COMPARE(committed.size(), 1);
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
        QCOMPARE(failed.size(), 1);
        QCOMPARE(unavailable.size(), 0);
    } else if (scenario == 1) {
        reply(peer.data(), list.requestId, false, QStringLiteral("DB_BUSY"),
              QStringLiteral("late"), QJsonObject{});
        nearbyPage->pendingGeocodeId_ = QStringLiteral("late-failed-geocode");
        emit nearbyPage->mapClient_->geocodeSucceeded(
            QStringLiteral("late-failed-geocode"), ev::user::GeoPoint{40.0, 116.4});
        const auto replacementList = takeRequest(peer.data());
        reply(peer.data(), replacementList.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(4, 0.2, true, false)}}});
        QTest::qWait(20);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(committed.size(), 0);
        QCOMPARE(unavailable.size(), 0);
        QVERIFY(retry->isVisible());
        QVERIFY(retry->isEnabled());
    } else if (scenario == 4) {
        reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
        QTest::qWait(20);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(committed.size(), 0);
        QCOMPARE(unavailable.size(), 0);
        QVERIFY(retry->isVisible());
        QVERIFY(retry->isEnabled());
    } else {
        if (scenario == 2 || scenario == 5) {
            validDetail();
        } else {
            reply(peer.data(), detail.requestId, false, QStringLiteral("DB_BUSY"),
                  QStringLiteral("late"), QJsonObject{});
        }
        QTest::qWait(20);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(committed.size(), 0);
        QCOMPARE(unavailable.size(), 0);
        QVERIFY(retry->isVisible());
        QVERIFY(retry->isEnabled());
    }
}

void UserApiTest::externalActiveOrderSupersedesTerminalExit_data()
{
    QTest::addColumn<bool>("sameSelection");
    QTest::newRow("cancel-exit-same-selection-active-reservation") << true;
    QTest::newRow("settle-exit-different-charger-active-charge") << false;
}

void UserApiTest::externalActiveOrderSupersedesTerminalExit()
{
    QFETCH(bool, sameSelection);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("authority-exit-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    const QString oldChargerStatus = sameSelection ? QStringLiteral("reserved")
                                                   : QStringLiteral("charging");
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue(oldChargerStatus)}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *chargePage = window.findChild<ChargePage *>(QStringLiteral("chargePage"));
    chargePage->enterOrder(
        sameSelection ? orderValue(QStringLiteral("reserved"))
                      : orderValue(QStringLiteral("charging"), true),
        ev::user::StationSelection{origin, station, chargerValue(oldChargerStatus), 3});
    window.findChild<QPushButton *>(sameSelection
        ? QStringLiteral("chargeCancelButton") : QStringLiteral("chargeSettleButton"))->click();
    const auto mutation = takeRequest(peer.data());
    if (sameSelection) {
        reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("cancelled"))}});
    } else {
        reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("completed"))},
                          {QStringLiteral("balanceFen"), 10'000}});
    }
    const auto oldFirst = takeRequest(peer.data());
    const auto oldSecond = takeRequest(peer.data());
    const auto oldDetail = oldFirst.action == QStringLiteral("station.detail")
        ? oldFirst : oldSecond;
    const auto oldList = oldFirst.action == QStringLiteral("station.list")
        ? oldFirst : oldSecond;

    auto *api = window.findChild<UserApi *>();
    (void)api->loadCurrentOrder(91, 91, ev::user::ChargeOperation::Guard);
    const auto authorityRequest = takeRequest(peer.data());
    QJsonObject authority = canonicalOrderObject(
        sameSelection ? QStringLiteral("reserved") : QStringLiteral("charging"),
        false, sameSelection ? 7 : 8, sameSelection ? 3 : 4);
    authority.insert(QStringLiteral("orderId"), sameSelection ? 100 : 101);
    authority.insert(QStringLiteral("stationName"),
                     sameSelection ? QStringLiteral("测试充电站3")
                                   : QStringLiteral("测试充电站4"));
    reply(peer.data(), authorityRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), authority}});

    auto *status = window.findChild<QLabel *>(QStringLiteral("chargeStatus"));
    QTRY_COMPARE(status->text(), sameSelection ? QStringLiteral("已预约")
                                               : QStringLiteral("充电中"));
    auto *back = window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"));
    QVERIFY(!back->isEnabled());
    QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"))->isEnabled());

    const auto freshFirst = takeRequest(peer.data());
    std::optional<ev::protocol::RequestEnvelope> freshList;
    ev::protocol::RequestEnvelope freshDetail = freshFirst;
    if (sameSelection) {
        const auto freshSecond = takeRequest(peer.data());
        freshDetail = freshFirst.action == QStringLiteral("station.detail")
            ? freshFirst : freshSecond;
        freshList = freshFirst.action == QStringLiteral("station.list")
            ? freshFirst : freshSecond;
    }
    QCOMPARE(freshDetail.action, QStringLiteral("station.detail"));

    const auto replyOldDetail = [&] {
        reply(peer.data(), oldDetail.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                      chargerObject(9, 3, QStringLiteral("charging")),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
    };
    const auto replyOldList = [&] {
        reply(peer.data(), oldList.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
    };
    if (sameSelection) {
        replyOldDetail();
        replyOldList();
    } else {
        replyOldList();
        replyOldDetail();
    }
    QTest::qWait(20);
    QCOMPARE(status->text(), sameSelection ? QStringLiteral("已预约")
                                           : QStringLiteral("充电中"));
    QVERIFY(!back->isEnabled());

    const qint64 stationId = sameSelection ? 3 : 4;
    const qint64 chargerId = sameSelection ? 7 : 8;
    reply(peer.data(), freshDetail.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(stationId, 0.0, false)},
                      {QStringLiteral("chargers"),
                       QJsonArray{chargerObject(chargerId, stationId,
                                                   sameSelection
                                                       ? QStringLiteral("reserved")
                                                       : QStringLiteral("charging")),
                                  chargerObject(chargerId + 1, stationId),
                                  chargerObject(chargerId + 2, stationId),
                                  chargerObject(chargerId + 3, stationId,
                                                QStringLiteral("fault"))}}});
    auto *action = window.findChild<QPushButton *>(sameSelection
        ? QStringLiteral("chargeStartButton") : QStringLiteral("chargeStopButton"));
    QTRY_VERIFY(action->isEnabled());
    QVERIFY(!back->isEnabled());
    emit chargePage->backRequested(42);
    QCOMPARE(window.findChild<QStackedWidget *>(QStringLiteral("mainPages"))->currentWidget(),
             static_cast<QWidget *>(chargePage));
    const auto backReconcile = takeRequest(peer.data());
    QCOMPARE(backReconcile.action, QStringLiteral("order.current"));
    QCOMPARE(status->text(), sameSelection ? QStringLiteral("已预约")
                                           : QStringLiteral("充电中"));
    back->click();
    QCOMPARE(window.findChild<QStackedWidget *>(QStringLiteral("mainPages"))->currentWidget(),
             static_cast<QWidget *>(chargePage));
    if (freshList.has_value()) {
        reply(peer.data(), freshList->requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(3, 0.1, true, false)}}});
    }
}

void UserApiTest::deferredNearbyInvalidationPreventsSelectionResurrection_data()
{
    QTest::addColumn<bool>("reserveSucceeds");
    QTest::newRow("active-order-exists-reconciles-null") << false;
    QTest::newRow("reserve-success-retains-order-not-selection") << true;
}

void UserApiTest::deferredNearbyInvalidationPreventsSelectionResurrection()
{
    QFETCH(bool, reserveSucceeds);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    MainWindow window(usableConfig(server.serverPort()));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    auto *phone = window.findChild<QLineEdit *>(QStringLiteral("phoneEdit"));
    phone->setText(QString::fromLatin1(kMobile));
    window.findChild<QPushButton *>(QStringLiteral("loginButton"))->click();
    const auto login = takeRequest(peer.data());
    reply(peer.data(), login.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("deferred-generation-token")},
                      {QStringLiteral("user"), userObject()}});
    const auto guard = takeRequest(peer.data());
    reply(peer.data(), guard.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    auto *nearbyPage = window.findChild<NearbyPage *>(QStringLiteral("nearbyPage"));
    const ev::user::GeoPoint origin{39.958, 116.317};
    const auto station = stationValue();
    nearbyPage->displayStations({origin, {station}});
    nearbyPage->displayStationDetail({station, {chargerValue()}});
    nearbyPage->findChild<QPushButton *>(QStringLiteral("chargerButton_7"))->click();
    auto *reserve = window.findChild<QPushButton *>(QStringLiteral("chargeReserveButton"));
    reserve->click();
    const auto mutation = takeRequest(peer.data());

    nearbyPage->pendingGeocodeId_ = QStringLiteral("hidden-pending-geocode");
    emit nearbyPage->mapClient_->geocodeSucceeded(
        QStringLiteral("hidden-pending-geocode"), ev::user::GeoPoint{40.0, 116.4});
    const auto replacementList = takeRequest(peer.data());
    QCOMPARE(replacementList.action, QStringLiteral("station.list"));
    QSignalSpy unavailable(nearbyPage, &NearbyPage::chargeRefreshUnavailable);

    if (!reserveSucceeds) {
        reply(peer.data(), mutation.requestId, false, QStringLiteral("ACTIVE_ORDER_EXISTS"),
              QStringLiteral("refresh"), QJsonObject{});
        const auto current = takeRequest(peer.data());
        reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        const auto oldFacts = takeRequest(peer.data());
        QCOMPARE(oldFacts.action, QStringLiteral("station.detail"));
        reply(peer.data(), oldFacts.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3), chargerObject(8, 3),
                                      chargerObject(9, 3, QStringLiteral("charging")),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
        reply(peer.data(), replacementList.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(4, 0.2, true, false)}}});
        QTRY_VERIFY(!reserve->isEnabled());
        reserve->click();
        QTest::qWait(20);
        QCOMPARE(peer->bytesAvailable(), qint64{0});
        QCOMPARE(unavailable.size(), 0);
    } else {
        reply(peer.data(), mutation.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("reserved"))}});
        const auto facts = takeRequest(peer.data());
        QCOMPARE(facts.action, QStringLiteral("station.detail"));
        reply(peer.data(), facts.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("station"), stationObject(3, 0.0, false)},
                          {QStringLiteral("chargers"),
                           QJsonArray{chargerObject(7, 3, QStringLiteral("reserved")),
                                      chargerObject(8, 3), chargerObject(9, 3),
                                      chargerObject(10, 3, QStringLiteral("fault"))}}});
        reply(peer.data(), replacementList.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("stations"),
                           QJsonArray{stationObject(4, 0.2, true, false)}}});
        auto *cancel = window.findChild<QPushButton *>(QStringLiteral("chargeCancelButton"));
        QTRY_VERIFY(cancel->isEnabled());
        cancel->click();
        const auto cancelMutation = takeRequest(peer.data());
        reply(peer.data(), cancelMutation.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"),
                           canonicalOrderObject(QStringLiteral("cancelled"))}});
        const auto optionalFacts = takeRequest(peer.data());
        QCOMPARE(optionalFacts.action, QStringLiteral("station.detail"));
        QTest::qWait(20);
        QCOMPARE(peer->bytesAvailable(), qint64{0});
        QCOMPARE(unavailable.size(), 0);
        QTRY_VERIFY(window.findChild<QPushButton *>(QStringLiteral("chargeBackButton"))->isEnabled());
    }
}

QTEST_MAIN(UserApiTest)
#include "tst_userapi.moc"
