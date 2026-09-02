#include "net/TcpJsonClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "services/UserApi.h"
#include "ui/ChargePage.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <QtTest>

namespace {

constexpr auto kMobile = "13800138000";
constexpr auto kTimestamp = "2026-09-01T08:30:45.123+08:00";
constexpr auto kEndedTimestamp = "2026-09-01T09:30:45.123+08:00";

QJsonObject userObject(QString status = QStringLiteral("active"), qint64 balanceFen = 12'345)
{
    return {
        {QStringLiteral("userId"), 42},
        {QStringLiteral("mobile"), QString::fromLatin1(kMobile)},
        {QStringLiteral("nickname"), QStringLiteral("测试用户")},
        {QStringLiteral("avatarPath"), QString()},
        {QStringLiteral("balanceFen"), balanceFen},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("registeredAt"), QString::fromLatin1(kTimestamp)},
    };
}

QJsonObject orderObject(QString status, bool ended = false, double energyKwh = 12.5,
                        qint64 amountFen = 2345, qint64 elapsedSec = 360)
{
    const bool started = status == QStringLiteral("charging")
        || status == QStringLiteral("completed");
    const bool mustEnd = ended || status == QStringLiteral("completed")
        || status == QStringLiteral("cancelled");
    return {
        {QStringLiteral("orderId"), 99},
        {QStringLiteral("userId"), 42},
        {QStringLiteral("chargerId"), 7},
        {QStringLiteral("stationId"), 3},
        {QStringLiteral("stationName"), QStringLiteral("星火充电站")},
        {QStringLiteral("chargerCode"), QStringLiteral("A-07")},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("reservedAt"), QString::fromLatin1(kTimestamp)},
        {QStringLiteral("startedAt"), started
             ? QJsonValue(QString::fromLatin1(kTimestamp)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("endedAt"), mustEnd
             ? QJsonValue(QString::fromLatin1(kEndedTimestamp)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("energyKwh"), energyKwh},
        {QStringLiteral("amountFen"), amountFen},
        {QStringLiteral("elapsedSec"), elapsedSec},
    };
}

QJsonObject stationObject(qint64 idleCount)
{
    return {
        {QStringLiteral("stationId"), 3},
        {QStringLiteral("name"), QStringLiteral("星火充电站")},
        {QStringLiteral("address"), QStringLiteral("北京市海淀区测试路3号")},
        {QStringLiteral("latitude"), 39.95},
        {QStringLiteral("longitude"), 116.31},
        {QStringLiteral("priceFenPerKwh"), 135},
        {QStringLiteral("forecastEnabled"), false},
        {QStringLiteral("chargerCount"), 1},
        {QStringLiteral("idleCount"), idleCount},
    };
}

QJsonObject chargerObject(QString status)
{
    return {
        {QStringLiteral("chargerId"), 7},
        {QStringLiteral("stationId"), 3},
        {QStringLiteral("code"), QStringLiteral("A-07")},
        {QStringLiteral("type"), QStringLiteral("fast")},
        {QStringLiteral("powerKw"), 60.0},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("chargeCount"), 12},
        {QStringLiteral("totalDurationSec"), 3600},
        {QStringLiteral("updatedAt"), QString::fromLatin1(kTimestamp)},
    };
}

QByteArray responseFrame(const QString &requestId, bool ok, QString code, QString message,
                         QJsonValue data)
{
    return ev::protocol::encodeFrame(ev::protocol::toJson(
        {requestId, ok, std::move(code), std::move(message), std::move(data)}));
}

void reply(QTcpSocket *peer, const QString &requestId, bool ok, const QString &code,
           const QString &message, const QJsonValue &data)
{
    const QByteArray frame = responseFrame(requestId, ok, code, message, data);
    QCOMPARE(peer->write(frame), qint64{frame.size()});
    QVERIFY(peer->flush());
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
    QObject::connect(&client, &TcpJsonClient::connectionChanged, &loop, [&](bool now) {
        connected = now;
        quitWhenReady();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    client.connectToServer();
    timeout.start(5'000);
    loop.exec();
    return accepted && connected;
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
    while (peer->bytesAvailable() < static_cast<qint64>(length) && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < static_cast<qint64>(length)) {
        return {};
    }
    return ev::protocol::parseRequest(peer->read(length));
}

void login(UserApi &api, QTcpSocket *peer, QString userStatus = QStringLiteral("active"))
{
    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("charge-token")},
                      {QStringLiteral("user"), userObject(std::move(userStatus))}});
    QTRY_VERIFY(api.sessionUser().has_value());
}

ev::user::StationSelection selection(quint64 generation = 11,
                                     QString chargerStatus = QStringLiteral("idle"))
{
    ev::user::StationSelection result;
    result.origin = {39.9, 116.3};
    result.station.stationId = 3;
    result.station.name = QStringLiteral("星火充电站");
    result.station.address = QStringLiteral("北京市海淀区测试路3号");
    result.station.latitude = 39.95;
    result.station.longitude = 116.31;
    result.station.priceFenPerKwh = 135;
    result.station.chargerCount = 1;
    result.station.idleCount = chargerStatus == QStringLiteral("idle") ? 1 : 0;
    result.charger.chargerId = 7;
    result.charger.stationId = 3;
    result.charger.code = QStringLiteral("A-07");
    result.charger.type = QStringLiteral("fast");
    result.charger.powerKw = 60.0;
    result.charger.status = std::move(chargerStatus);
    result.charger.updatedAt = QString::fromLatin1(kTimestamp);
    result.selectionGeneration = generation;
    return result;
}

ev::user::Order decodedOrder(QString status, bool ended = false, double energyKwh = 12.5,
                             qint64 amountFen = 2345, qint64 elapsedSec = 360)
{
    ev::user::Order order;
    order.orderId = 99;
    order.userId = 42;
    order.chargerId = 7;
    order.stationId = 3;
    order.stationName = QStringLiteral("星火充电站");
    order.chargerCode = QStringLiteral("A-07");
    order.status = std::move(status);
    order.reservedAt = QString::fromLatin1(kTimestamp);
    if (order.status == QStringLiteral("charging") || order.status == QStringLiteral("completed")) {
        order.startedAt = QString::fromLatin1(kTimestamp);
    }
    if (ended || order.status == QStringLiteral("completed")
        || order.status == QStringLiteral("cancelled")) {
        order.endedAt = QString::fromLatin1(kEndedTimestamp);
    }
    order.energyKwh = energyKwh;
    order.amountFen = amountFen;
    order.elapsedSec = elapsedSec;
    return order;
}

QPushButton *button(ChargePage &page, const char *name)
{
    auto *result = page.findChild<QPushButton *>(QString::fromLatin1(name));
    if (result == nullptr) {
        qFatal("missing button %s", name);
    }
    return result;
}

QLabel *label(ChargePage &page, const char *name)
{
    auto *result = page.findChild<QLabel *>(QString::fromLatin1(name));
    if (result == nullptr) {
        qFatal("missing label %s", name);
    }
    return result;
}

} // namespace

class ChargePageTest final : public QObject
{
    Q_OBJECT

private slots:
    void exactStateTableUsesOnlyAuthoritativeFields();
    void lifecycleUsesExactPayloadsAndUpdatesBalanceByRevision();
    void canonicalDecoderRejectsBadTimestampNullAndExtraFields();
    void frozenUserCanCancelStopAndSettle();
    void pollIsTwoSecondsSingleFlightAndStopsOnTerminalOrLeave();
    void uncertainMutationNeverReplaysAndReconcilesCurrentFirst();
    void businessErrorsUseFixedChineseMappingsAndSafeRefresh();
    void stalePageMutationCannotApplyAndLateProfileCannotOverwriteSettle();
    void notConnectedBeforeWriteIsDefiniteAndMalformedMutationIsUncertain();
    void terminalMutationRejectsOlderOutstandingPoll();
};

void ChargePageTest::exactStateTableUsesOnlyAuthoritativeFields()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    ChargePage page(&api);
    page.setConnectionAvailable(true);

    page.enterSelection(selection());
    QVERIFY(button(page, "chargeReserveButton")->isEnabled());
    QVERIFY(!button(page, "chargeStartButton")->isEnabled());

    page.enterOrder(decodedOrder(QStringLiteral("reserved")), selection(12, QStringLiteral("reserved")));
    QVERIFY(button(page, "chargeStartButton")->isEnabled());
    QVERIFY(button(page, "chargeCancelButton")->isEnabled());

    page.enterOrder(decodedOrder(QStringLiteral("charging"), false, 7.125, 876, 543));
    QVERIFY(button(page, "chargeStopButton")->isEnabled());
    QVERIFY(label(page, "chargeMeter")->isVisibleTo(&page));
    QVERIFY(label(page, "chargeMeter")->text().contains(QStringLiteral("543")));
    QVERIFY(label(page, "chargeMeter")->text().contains(QStringLiteral("7.125")));
    QVERIFY(label(page, "chargeMeter")->text().contains(QStringLiteral("8.76")));

    page.enterOrder(decodedOrder(QStringLiteral("charging"), true, 8.5, 999, 600),
                    selection(13, QStringLiteral("fault")));
    QVERIFY(button(page, "chargeSettleButton")->isEnabled());
    QVERIFY(label(page, "chargeSummary")->isVisibleTo(&page));
    QVERIFY(label(page, "chargeSummary")->text().contains(QStringLiteral("8.500")));
    QVERIFY(label(page, "chargeSummary")->text().contains(QStringLiteral("9.99")));

    page.enterOrder(decodedOrder(QStringLiteral("completed"), true));
    QVERIFY(button(page, "chargeBackButton")->isEnabled());
    QVERIFY(!button(page, "chargeSettleButton")->isEnabled());

    page.enterOrder(decodedOrder(QStringLiteral("cancelled"), true));
    QCOMPARE(label(page, "chargeStatus")->text(), QStringLiteral("预约已取消"));
    QVERIFY(button(page, "chargeBackButton")->isEnabled());
    QVERIFY(!button(page, "chargeReserveButton")->isEnabled());
}

void ChargePageTest::lifecycleUsesExactPayloadsAndUpdatesBalanceByRevision()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    QSignalSpy userApplied(&api, &UserApi::sessionUserApplied);
    ChargePage page(&api);
    QSignalSpy nearbyRefresh(&page, &ChargePage::nearbyRefreshRequested);
    page.setConnectionAvailable(true);
    page.enterSelection(selection(21));

    button(page, "chargeReserveButton")->click();
    const auto reserve = takeRequest(peer.data());
    QCOMPARE(reserve.action, QStringLiteral("charge.reserve"));
    const QJsonObject reservePayload{{QStringLiteral("chargerId"), 7}};
    QCOMPARE(reserve.payload, reservePayload);
    QVERIFY(!button(page, "chargeReserveButton")->isEnabled());
    reply(peer.data(), reserve.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    QTRY_COMPARE(label(page, "chargeStatus")->text(), QStringLiteral("已预约"));
    QTRY_COMPARE(nearbyRefresh.size(), 1);

    const auto detailAfterReserve = takeRequest(peer.data());
    QCOMPARE(detailAfterReserve.action, QStringLiteral("station.detail"));
    const QJsonObject stationPayload{{QStringLiteral("stationId"), 3}};
    QCOMPARE(detailAfterReserve.payload, stationPayload);
    reply(peer.data(), detailAfterReserve.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(0)},
                      {QStringLiteral("chargers"), QJsonArray{chargerObject(QStringLiteral("reserved"))}}});
    QTRY_VERIFY(button(page, "chargeStartButton")->isEnabled());

    button(page, "chargeStartButton")->click();
    const auto start = takeRequest(peer.data());
    QCOMPARE(start.action, QStringLiteral("charge.start"));
    const QJsonObject orderPayload{{QStringLiteral("orderId"), 99}};
    QCOMPARE(start.payload, orderPayload);
    reply(peer.data(), start.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("charging"), false, 1.25, 169, 80)}});
    QTRY_VERIFY(button(page, "chargeStopButton")->isEnabled());
    const auto detailAfterStart = takeRequest(peer.data());
    QCOMPARE(detailAfterStart.action, QStringLiteral("station.detail"));
    reply(peer.data(), detailAfterStart.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(0)},
                      {QStringLiteral("chargers"), QJsonArray{chargerObject(QStringLiteral("charging"))}}});

    button(page, "chargeStopButton")->click();
    const auto stop = takeRequest(peer.data());
    QCOMPARE(stop.action, QStringLiteral("charge.stop"));
    QCOMPARE(stop.payload, orderPayload);
    reply(peer.data(), stop.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("charging"), true, 2.5, 338, 160)}});
    QTRY_VERIFY(button(page, "chargeSettleButton")->isEnabled());
    const auto detailAfterStop = takeRequest(peer.data());
    QCOMPARE(detailAfterStop.action, QStringLiteral("station.detail"));
    reply(peer.data(), detailAfterStop.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("station"), stationObject(0)},
                      {QStringLiteral("chargers"), QJsonArray{chargerObject(QStringLiteral("fault"))}}});

    button(page, "chargeSettleButton")->click();
    const auto settle = takeRequest(peer.data());
    QCOMPARE(settle.action, QStringLiteral("charge.settle"));
    QCOMPARE(settle.payload, orderPayload);
    reply(peer.data(), settle.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("completed"), true, 2.5, 338, 160)},
                      {QStringLiteral("balanceFen"), 12007}});
    QTRY_COMPARE(api.sessionUser()->balanceFen, qint64{12007});
    QTRY_COMPARE(userApplied.size(), 1);
    QCOMPARE(userApplied.at(0).at(0).value<ev::user::User>().balanceFen, qint64{12007});
    QVERIFY(userApplied.at(0).at(1).toULongLong() > 0);
    QVERIFY(userApplied.at(0).at(2).toULongLong() > 0);
    QVERIFY(button(page, "chargeBackButton")->isEnabled());
}

void ChargePageTest::canonicalDecoderRejectsBadTimestampNullAndExtraFields()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    QSignalSpy failures(&api, &UserApi::chargeRequestFailed);
    QSignalSpy orders(&api, &UserApi::currentOrderLoaded);

    const auto exerciseInvalid = [&](QJsonObject malformed) {
        const auto context = api.loadCurrentOrder(5, 7, ev::user::ChargeOperation::Reconcile);
        const auto request = takeRequest(peer.data());
        QCOMPARE(request.requestId, context.requestId);
        QVERIFY(context.sessionGeneration > 0);
        QCOMPARE(context.pageGeneration, quint64{5});
        QCOMPARE(context.selectionGeneration, quint64{7});
        QCOMPARE(context.operation, ev::user::ChargeOperation::Reconcile);
        QVERIFY(context.readEpoch > 0);
        reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), malformed}});
        QTRY_VERIFY(!failures.isEmpty());
        const auto arguments = failures.takeFirst();
        QCOMPARE(arguments.at(0).value<ev::user::RequestContext>().requestId, request.requestId);
        QCOMPARE(arguments.at(1).value<ev::user::ApiError>().code, QStringLiteral("INVALID_RESPONSE"));
    };

    QJsonObject utc = orderObject(QStringLiteral("charging"));
    utc.insert(QStringLiteral("reservedAt"), QStringLiteral("2026-09-01T08:30:45Z"));
    exerciseInvalid(utc);
    QJsonObject trailingTimestamp = orderObject(QStringLiteral("charging"));
    trailingTimestamp.insert(QStringLiteral("reservedAt"),
                             QStringLiteral("2026-09-01T08:30:45+08:00\n"));
    exerciseInvalid(trailingTimestamp);
    QJsonObject cancelledWithoutEnd = orderObject(QStringLiteral("cancelled"));
    cancelledWithoutEnd.insert(QStringLiteral("endedAt"), QJsonValue(QJsonValue::Null));
    exerciseInvalid(cancelledWithoutEnd);
    QJsonObject reservedWithStart = orderObject(QStringLiteral("reserved"));
    reservedWithStart.insert(QStringLiteral("startedAt"), QString::fromLatin1(kTimestamp));
    exerciseInvalid(reservedWithStart);
    QJsonObject extra = orderObject(QStringLiteral("charging"));
    extra.insert(QStringLiteral("clientAmount"), 1);
    exerciseInvalid(extra);
    QJsonObject unsafe = orderObject(QStringLiteral("charging"));
    unsafe.insert(QStringLiteral("elapsedSec"), 9'007'199'254'740'992.0);
    exerciseInvalid(unsafe);

    QJsonObject fractional = orderObject(QStringLiteral("charging"));
    const QString precise = QStringLiteral("2026-09-01T08:30:45.123456+08:00");
    fractional.insert(QStringLiteral("reservedAt"), precise);
    fractional.insert(QStringLiteral("startedAt"), precise);
    const auto fractionalContext = api.loadCurrentOrder(
        5, 7, ev::user::ChargeOperation::Reconcile);
    const auto fractionalRequest = takeRequest(peer.data());
    QCOMPARE(fractionalRequest.requestId, fractionalContext.requestId);
    reply(peer.data(), fractionalRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), fractional}});
    QTRY_COMPARE(orders.size(), 1);
}

void ChargePageTest::frozenUserCanCancelStopAndSettle()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data(), QStringLiteral("frozen"));
    ChargePage page(&api);
    page.setConnectionAvailable(true);

    page.enterOrder(decodedOrder(QStringLiteral("reserved")), selection(31, QStringLiteral("reserved")));
    QVERIFY(button(page, "chargeCancelButton")->isEnabled());
    button(page, "chargeCancelButton")->click();
    const auto cancel = takeRequest(peer.data());
    QCOMPARE(cancel.action, QStringLiteral("order.cancel"));
    reply(peer.data(), cancel.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("cancelled"))}});
    QTRY_COMPARE(label(page, "chargeStatus")->text(), QStringLiteral("预约已取消"));
    QVERIFY(button(page, "chargeBackButton")->isEnabled());
    const auto cancelledFacts = takeRequest(peer.data());
    QCOMPARE(cancelledFacts.action, QStringLiteral("station.detail"));

    page.enterOrder(decodedOrder(QStringLiteral("charging")));
    QVERIFY(button(page, "chargeStopButton")->isEnabled());
    button(page, "chargeStopButton")->click();
    QCOMPARE(takeRequest(peer.data()).action, QStringLiteral("charge.stop"));

    page.enterOrder(decodedOrder(QStringLiteral("charging"), true), selection(32, QStringLiteral("idle")));
    QVERIFY(button(page, "chargeSettleButton")->isEnabled());
    button(page, "chargeSettleButton")->click();
    QCOMPARE(takeRequest(peer.data()).action, QStringLiteral("charge.settle"));
}

void ChargePageTest::pollIsTwoSecondsSingleFlightAndStopsOnTerminalOrLeave()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    ChargePage page(&api);
    page.setConnectionAvailable(true);
    page.enterOrder(decodedOrder(QStringLiteral("charging"), false, 1.0, 135, 60));

    QTest::qWait(1'850);
    QVERIFY(peer->bytesAvailable() == 0);
    QTest::qWait(250);
    const auto firstPoll = takeRequest(peer.data());
    QCOMPARE(firstPoll.action, QStringLiteral("order.current"));
    QTest::qWait(2'150);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
    reply(peer.data(), firstPoll.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("charging"), false, 3.25, 439, 180)}});
    QTRY_VERIFY(label(page, "chargeMeter")->text().contains(QStringLiteral("3.250")));

    QTest::qWait(2'100);
    const auto stoppedPoll = takeRequest(peer.data());
    QCOMPARE(stoppedPoll.action, QStringLiteral("order.current"));
    reply(peer.data(), stoppedPoll.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("charging"), true, 4.0, 540, 240)}});
    QTRY_VERIFY(button(page, "chargeSettleButton")->isEnabled());
    QTest::qWait(2'150);
    QCOMPARE(peer->bytesAvailable(), qint64{0});

    page.enterOrder(decodedOrder(QStringLiteral("charging")));
    page.leavePage();
    QTest::qWait(2'150);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
}

void ChargePageTest::uncertainMutationNeverReplaysAndReconcilesCurrentFirst()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> firstPeer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, firstPeer.data());
    ChargePage page(&api);
    page.setConnectionAvailable(true);
    page.enterSelection(selection(41));
    button(page, "chargeReserveButton")->click();
    const auto mutation = takeRequest(firstPeer.data());
    QCOMPARE(mutation.action, QStringLiteral("charge.reserve"));
    firstPeer->abort();
    QTRY_COMPARE(label(page, "chargeError")->text(), QStringLiteral("结果未确认，需要刷新"));
    QVERIFY(!button(page, "chargeReserveButton")->isEnabled());
    QVERIFY(button(page, "chargeRetryButton")->isVisibleTo(&page));

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5'000);
    QScopedPointer<QTcpSocket> secondPeer(server.nextPendingConnection());
    const auto reconcile = takeRequest(secondPeer.data(), 5'000);
    QCOMPARE(reconcile.action, QStringLiteral("order.current"));
    QVERIFY(reconcile.requestId != mutation.requestId);
    reply(secondPeer.data(), reconcile.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    QTRY_COMPARE(label(page, "chargeStatus")->text(), QStringLiteral("已预约"));
    const auto detail = takeRequest(secondPeer.data());
    QCOMPARE(detail.action, QStringLiteral("station.detail"));
}

void ChargePageTest::businessErrorsUseFixedChineseMappingsAndSafeRefresh()
{
    const QList<QPair<QString, QString>> mappings{
        {QStringLiteral("USER_FROZEN"), QStringLiteral("账户已冻结，无法预约或开始充电")},
        {QStringLiteral("ACTIVE_ORDER_EXISTS"), QStringLiteral("已有未完成订单，请先处理当前订单")},
        {QStringLiteral("CHARGER_NOT_AVAILABLE"), QStringLiteral("充电桩当前不可用，请刷新后重试")},
        {QStringLiteral("ORDER_STATE_CONFLICT"), QStringLiteral("订单状态已变化，正在刷新")},
        {QStringLiteral("INSUFFICIENT_BALANCE"), QStringLiteral("余额不足，请充值后再结算")},
        {QStringLiteral("DB_BUSY"), QStringLiteral("服务繁忙，请稍后刷新重试")},
    };
    for (const auto &mapping : mappings) {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        TcpJsonClient client;
        client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
        QVERIFY(connectToFakeServer(client, server));
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        UserApi api(&client);
        login(api, peer.data());
        ChargePage page(&api);
        page.setConnectionAvailable(true);
        page.enterSelection(selection(50));
        button(page, "chargeReserveButton")->click();
        const auto mutation = takeRequest(peer.data());
        reply(peer.data(), mutation.requestId, false, mapping.first,
              QStringLiteral("server detail must not replace fixed copy"), QJsonObject{});
        QTRY_COMPARE(label(page, "chargeError")->text(), mapping.second);
        const auto refresh = takeRequest(peer.data());
        QCOMPARE(refresh.action, QStringLiteral("order.current"));
        QCOMPARE(refresh.payload, QJsonObject{});
        reply(peer.data(), refresh.requestId, true, QStringLiteral("OK"), QString(),
              QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
        const auto facts = takeRequest(peer.data());
        QCOMPARE(facts.action, QStringLiteral("station.detail"));
    }
}

void ChargePageTest::stalePageMutationCannotApplyAndLateProfileCannotOverwriteSettle()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    ChargePage page(&api);
    page.setConnectionAvailable(true);

    page.enterSelection(selection(61));
    button(page, "chargeReserveButton")->click();
    const auto staleSelectionReserve = takeRequest(peer.data());
    page.enterSelection(selection(62));
    reply(peer.data(), staleSelectionReserve.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("reserved"))}});
    QTest::qWait(50);
    QVERIFY(button(page, "chargeReserveButton")->isEnabled());
    QVERIFY(label(page, "chargeStatus")->text().contains(QStringLiteral("已选择")));

    const qint64 originalBalance = api.sessionUser()->balanceFen;
    page.enterOrder(decodedOrder(QStringLiteral("charging"), true));
    button(page, "chargeSettleButton")->click();
    const auto staleSettle = takeRequest(peer.data());
    page.leavePage();
    reply(peer.data(), staleSettle.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("completed"))},
                      {QStringLiteral("balanceFen"), 10'000}});
    QTest::qWait(50);
    QCOMPARE(api.sessionUser()->balanceFen, originalBalance);

    const QString oldProfileId = api.loadProfile();
    const auto oldProfile = takeRequest(peer.data());
    QCOMPARE(oldProfile.requestId, oldProfileId);
    page.enterOrder(decodedOrder(QStringLiteral("charging"), true));
    QSignalSpy userApplied(&api, &UserApi::sessionUserApplied);
    button(page, "chargeSettleButton")->click();
    const auto settle = takeRequest(peer.data());
    reply(peer.data(), settle.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("completed"))},
                      {QStringLiteral("balanceFen"), 9'000}});
    QTRY_COMPARE(api.sessionUser()->balanceFen, qint64{9'000});
    QTRY_COMPARE(userApplied.size(), 1);

    QJsonObject staleProfile = userObject(QStringLiteral("active"), originalBalance);
    staleProfile.insert(QStringLiteral("nickname"), QStringLiteral("旧响应昵称"));
    reply(peer.data(), oldProfile.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("user"), staleProfile}});
    QTest::qWait(50);
    QCOMPARE(api.sessionUser()->balanceFen, qint64{9'000});
    QCOMPARE(api.sessionUser()->nickname, QStringLiteral("测试用户"));
    QCOMPARE(userApplied.size(), 1);
}

void ChargePageTest::notConnectedBeforeWriteIsDefiniteAndMalformedMutationIsUncertain()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    QSignalSpy failures(&api, &UserApi::chargeRequestFailed);

    client.disconnectFromServer();
    QTRY_VERIFY(peer->state() == QAbstractSocket::UnconnectedState
                || client.findChild<QTcpSocket *>()->state() == QAbstractSocket::UnconnectedState);
    const auto beforeWrite = api.reserveCharger(7, 1, 2);
    QVERIFY(!beforeWrite.requestId.isEmpty());
    QTRY_COMPARE(failures.size(), 1);
    const auto definite = failures.takeFirst();
    QCOMPARE(definite.at(1).value<ev::user::ApiError>().code, QStringLiteral("NOT_CONNECTED"));
    QCOMPARE(definite.at(2).toBool(), false);

    QTcpServer secondServer;
    QVERIFY(secondServer.listen(QHostAddress::LocalHost));
    TcpJsonClient connectedClient;
    connectedClient.configure(QStringLiteral("127.0.0.1"), secondServer.serverPort());
    QVERIFY(connectToFakeServer(connectedClient, secondServer));
    QScopedPointer<QTcpSocket> connectedPeer(secondServer.nextPendingConnection());
    UserApi connectedApi(&connectedClient);
    login(connectedApi, connectedPeer.data());
    ChargePage page(&connectedApi);
    page.setConnectionAvailable(true);
    page.enterOrder(decodedOrder(QStringLiteral("charging"), true));
    const qint64 balance = connectedApi.sessionUser()->balanceFen;
    button(page, "chargeSettleButton")->click();
    const auto settle = takeRequest(connectedPeer.data());
    reply(connectedPeer.data(), settle.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("completed"))},
                      {QStringLiteral("balanceFen"), QStringLiteral("9000")}});
    QTRY_COMPARE(label(page, "chargeError")->text(), QStringLiteral("结果未确认，需要刷新"));
    QCOMPARE(connectedApi.sessionUser()->balanceFen, balance);
    QVERIFY(!button(page, "chargeSettleButton")->isEnabled());
    button(page, "chargeRetryButton")->click();
    const auto reconcile = takeRequest(connectedPeer.data());
    QCOMPARE(reconcile.action, QStringLiteral("order.current"));
    reply(connectedPeer.data(), reconcile.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_COMPARE(label(page, "chargeStatus")->text(), QStringLiteral("当前无未完成订单"));
    QVERIFY(button(page, "chargeBackButton")->isEnabled());
}

void ChargePageTest::terminalMutationRejectsOlderOutstandingPoll()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    ChargePage page(&api);
    page.setConnectionAvailable(true);
    page.enterOrder(decodedOrder(QStringLiteral("charging"), false, 1.0, 135, 60));

    QTest::qWait(2'100);
    const auto poll = takeRequest(peer.data());
    QCOMPARE(poll.action, QStringLiteral("order.current"));
    button(page, "chargeStopButton")->click();
    const auto stop = takeRequest(peer.data());
    QCOMPARE(stop.action, QStringLiteral("charge.stop"));
    reply(peer.data(), stop.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("charging"), true,
                                                            2.5, 338, 160)}});
    QTRY_VERIFY(button(page, "chargeSettleButton")->isEnabled());
    const QString frozenSummary = label(page, "chargeSummary")->text();
    reply(peer.data(), poll.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), orderObject(QStringLiteral("charging"), false,
                                                            9.0, 1'215, 999)}});
    QTest::qWait(50);
    QCOMPARE(label(page, "chargeSummary")->text(), frozenSummary);
    QVERIFY(button(page, "chargeSettleButton")->isEnabled());
}

QTEST_MAIN(ChargePageTest)
#include "tst_chargepage.moc"
