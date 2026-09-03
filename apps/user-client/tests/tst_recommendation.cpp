#include "app/UserAppConfig.h"
#include "domain/Models.h"
#include "net/TcpJsonClient.h"
#include "net/TencentMapClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"
#include "services/UserApi.h"
#include "ui/HistoryPage.h"
#include "ui/MainWindow.h"
#include "ui/NearbyPage.h"

#include <QElapsedTimer>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <QtTest>

#include <algorithm>

namespace {

constexpr auto kMobile = "13800138000";
constexpr auto kRegisteredAt = "2026-09-01T07:00:00+08:00";

QJsonObject userObject(qint64 userId = 42)
{
    return {
        {QStringLiteral("userId"), userId},
        {QStringLiteral("mobile"), QString::fromLatin1(kMobile)},
        {QStringLiteral("nickname"), QStringLiteral("历史测试用户")},
        {QStringLiteral("avatarPath"), QString()},
        {QStringLiteral("balanceFen"), 12'345},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("registeredAt"), QString::fromLatin1(kRegisteredAt)},
    };
}

QJsonObject orderObject(qint64 orderId, QString status, QString reservedAt,
                        QString endedAt = {}, qint64 userId = 42)
{
    const bool started = status == QStringLiteral("charging")
        || status == QStringLiteral("completed");
    return {
        {QStringLiteral("orderId"), orderId},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("chargerId"), 1000 + orderId},
        {QStringLiteral("stationId"), 10 + orderId},
        {QStringLiteral("stationName"), QStringLiteral("测试站点%1").arg(orderId)},
        {QStringLiteral("chargerCode"), QStringLiteral("C-%1").arg(orderId)},
        {QStringLiteral("status"), std::move(status)},
        {QStringLiteral("reservedAt"), std::move(reservedAt)},
        {QStringLiteral("startedAt"), started
             ? QJsonValue(QStringLiteral("2026-09-01T08:10:00+08:00"))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("endedAt"), endedAt.isEmpty()
             ? QJsonValue(QJsonValue::Null) : QJsonValue(std::move(endedAt))},
        {QStringLiteral("energyKwh"), 10.125},
        {QStringLiteral("amountFen"), 2345},
        {QStringLiteral("elapsedSec"), 3600},
    };
}

QJsonObject stationObject(qint64 stationId, double distanceKm,
                          bool forecastEnabled = true)
{
    return {
        {QStringLiteral("stationId"), stationId},
        {QStringLiteral("name"), QStringLiteral("预测站点%1").arg(stationId)},
        {QStringLiteral("address"), QStringLiteral("北京市测试路%1号").arg(stationId)},
        {QStringLiteral("latitude"), 39.90 + stationId / 1000.0},
        {QStringLiteral("longitude"), 116.30 + stationId / 1000.0},
        {QStringLiteral("priceFenPerKwh"), 135},
        {QStringLiteral("forecastEnabled"), forecastEnabled},
        {QStringLiteral("chargerCount"), 4},
        {QStringLiteral("idleCount"), 2},
        {QStringLiteral("distanceKm"), distanceKm},
    };
}

QJsonObject forecastRunObject(bool stale)
{
    return {
        {QStringLiteral("runId"), QStringLiteral("run-task7")},
        {QStringLiteral("generatedAt"), QStringLiteral("2026-09-01T08:00:00+08:00")},
        {QStringLiteral("dataCutoff"), QStringLiteral("2026-09-01T07:00:00+08:00")},
        {QStringLiteral("activatedAt"), QStringLiteral("2026-09-01T08:01:00+08:00")},
        {QStringLiteral("modelVersion"), QStringLiteral("ridge-v1")},
        {QStringLiteral("payloadHash"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("stale"), stale},
    };
}

QJsonArray forecastRecords(const QHash<qint64, qint64> &horizonOneBusy,
                           const QHash<qint64, qint64> &chargerCounts = {})
{
    QJsonArray records;
    const QDateTime cutoff = QDateTime::fromString(
        QStringLiteral("2026-09-01T07:00:00+08:00"), Qt::ISODate);
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        for (qint64 horizon = 1; horizon <= 24; ++horizon) {
            const qint64 busy = horizon == 1 ? horizonOneBusy.value(stationId, 2) : 2;
            const qint64 chargerCount = chargerCounts.value(stationId, 4);
            const qint64 idle = chargerCount - busy;
            const long double ratio = static_cast<long double>(busy)
                / static_cast<long double>(chargerCount);
            const QString congestion = ratio < 0.5L ? QStringLiteral("low")
                : (ratio < 0.8L ? QStringLiteral("medium") : QStringLiteral("high"));
            records.append(QJsonObject{
                {QStringLiteral("stationId"), stationId},
                {QStringLiteral("forecastAt"), cutoff.addSecs(horizon * 3600).toString(Qt::ISODate)},
                {QStringLiteral("horizonH"), horizon},
                {QStringLiteral("predictedLoadKw"), 60.0 + stationId},
                {QStringLiteral("predictedBusyCount"), busy},
                {QStringLiteral("predictedIdleCount"), idle},
                {QStringLiteral("congestionLevel"), congestion},
                {QStringLiteral("isPeak"), busy == 4},
            });
        }
    }
    return records;
}

QByteArray responseFrame(const QString &requestId, bool ok, QString code,
                         QString message, QJsonValue data)
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
    while (peer->bytesAvailable() < static_cast<qint64>(length)
           && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    if (peer->bytesAvailable() < static_cast<qint64>(length)) {
        return {};
    }
    return ev::protocol::parseRequest(peer->read(length));
}

void login(UserApi &api, QTcpSocket *peer, qint64 userId = 42)
{
    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("task7-token")},
                      {QStringLiteral("user"), userObject(userId)}});
    QTRY_VERIFY(api.sessionUser().has_value());
}

template<typename T>
T *required(QObject *owner, const char *name)
{
    auto *value = owner->findChild<T *>(QString::fromLatin1(name));
    if (value == nullptr) {
        qFatal("missing child %s", name);
    }
    return value;
}

QList<qint64> visibleStationOrder(NearbyPage &page)
{
    page.resize(900, 900);
    page.show();
    QTest::qWait(20);
    QList<QPair<int, qint64>> positions;
    for (qint64 stationId = 1; stationId <= 7; ++stationId) {
        if (auto *label = page.findChild<QLabel *>(
                QStringLiteral("stationName_%1").arg(stationId))) {
            positions.append({label->mapTo(&page, QPoint{}).y(), stationId});
        }
    }
    std::sort(positions.begin(), positions.end());
    QList<qint64> result;
    for (const auto &[position, stationId] : positions) {
        Q_UNUSED(position);
        result.append(stationId);
    }
    return result;
}

ev::user::StationListResult decodedStations()
{
    ev::user::StationListResult result;
    result.origin = {39.962, 116.318};
    const QList<double> distances{0.0, 5.0, 2.0, 8.0, 1.0, 4.0, 3.0, 0.5};
    for (qint64 id = 1; id <= 7; ++id) {
        ev::user::Station station;
        station.stationId = id;
        station.name = QStringLiteral("预测站点%1").arg(id);
        station.address = QStringLiteral("北京市测试路%1号").arg(id);
        station.latitude = 39.90 + id / 1000.0;
        station.longitude = 116.30 + id / 1000.0;
        station.priceFenPerKwh = 135;
        station.forecastEnabled = id <= 6;
        station.chargerCount = 4;
        station.idleCount = 2;
        station.distanceKm = distances.at(id);
        result.stations.append(station);
    }
    return result;
}

QJsonArray encodedStations()
{
    QJsonArray result;
    const QList<double> distances{0.0, 5.0, 2.0, 8.0, 1.0, 4.0, 3.0, 0.5};
    for (qint64 id = 1; id <= 7; ++id) {
        result.append(stationObject(id, distances.at(id), id <= 6));
    }
    return result;
}

QJsonArray encodedStationsWithFirstCount(qint64 chargerCount)
{
    QJsonArray result = encodedStations();
    QJsonObject first = result.at(0).toObject();
    first.insert(QStringLiteral("chargerCount"), chargerCount);
    first.insert(QStringLiteral("idleCount"), qMin<qint64>(2, chargerCount));
    result.replace(0, first);
    return result;
}

QJsonArray encodedStationsWithSwappedForecastEnablement()
{
    QJsonArray result = encodedStations();
    QJsonObject first = result.at(0).toObject();
    first.insert(QStringLiteral("forecastEnabled"), false);
    result.replace(0, first);
    QJsonObject seventh = result.at(6).toObject();
    seventh.insert(QStringLiteral("forecastEnabled"), true);
    result.replace(6, seventh);
    return result;
}

ev::user::StationDetailResult selectableDetail(
    const ev::user::Station &source, qint64 chargerId = 1001)
{
    ev::user::StationDetailResult detail;
    detail.station = source;
    detail.station.chargerCount = 1;
    detail.station.idleCount = 1;
    ev::user::Charger charger;
    charger.chargerId = chargerId;
    charger.stationId = detail.station.stationId;
    charger.code = QStringLiteral("C-%1").arg(chargerId);
    charger.type = QStringLiteral("fast");
    charger.powerKw = 60.0;
    charger.status = QStringLiteral("idle");
    charger.updatedAt = QStringLiteral("2026-09-01T08:00:00+08:00");
    detail.chargers.append(charger);
    return detail;
}

QJsonObject stationDetailData(qint64 stationId = 1, qint64 chargerId = 1001)
{
    QJsonObject station = stationObject(stationId, 0.5);
    station.remove(QStringLiteral("distanceKm"));
    station.insert(QStringLiteral("chargerCount"), 1);
    station.insert(QStringLiteral("idleCount"), 1);
    return {
        {QStringLiteral("station"), station},
        {QStringLiteral("chargers"), QJsonArray{QJsonObject{
             {QStringLiteral("chargerId"), chargerId},
             {QStringLiteral("stationId"), stationId},
             {QStringLiteral("code"), QStringLiteral("C-%1").arg(chargerId)},
             {QStringLiteral("type"), QStringLiteral("fast")},
             {QStringLiteral("powerKw"), 60.0},
             {QStringLiteral("status"), QStringLiteral("idle")},
             {QStringLiteral("chargeCount"), 0},
             {QStringLiteral("totalDurationSec"), 0},
             {QStringLiteral("updatedAt"), QStringLiteral("2026-09-01T08:00:00+08:00")},
         }}},
    };
}

void completeMainLoginWithoutOrder(MainWindow &window, QTcpSocket *peer)
{
    required<QLineEdit>(&window, "phoneEdit")->setText(QString::fromLatin1(kMobile));
    required<QPushButton>(&window, "loginButton")->click();
    auto request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("auth.user_login"));
    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("main-task7-token")},
                      {QStringLiteral("user"), userObject()}});
    request = takeRequest(peer);
    QCOMPARE(request.action, QStringLiteral("order.current"));
    reply(peer, request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTRY_VERIFY(!required<QWidget>(&window, "authenticatedNavigation")->isHidden());
}

QTcpSocket *waitForPeer(QTcpServer &server, int timeoutMs = 3'000)
{
    QElapsedTimer timer;
    timer.start();
    while (!server.hasPendingConnections() && timer.elapsed() < timeoutMs) {
        QTest::qWait(10);
    }
    return server.nextPendingConnection();
}

} // namespace

class RecommendationTest final : public QObject
{
    Q_OBJECT

private slots:
    void orderListUsesExactPayloadAndCanonicalDecoder();
    void orderListRejectsInvalidRequestPagination_data();
    void orderListRejectsInvalidRequestPagination();
    void orderListRejectsInvalidPages_data();
    void orderListRejectsInvalidPages();
    void historyRendersOnlyReceivedPageWithExactPagination();
    void historyDropsSupersededAndSessionForeignResponses();
    void historyFailureAndDisconnectPreserveCommittedCache();
    void historyLabelsStoppedChargingFromEndedAt();
    void historyUsesFallbackKeysForEquivalentTimestampInstants();
    void historyFormatsLargeFenExactly();
    void historyNeverExposesRawFailureMessages();
    void freshForecastUsesRealTransportAndDeterministicRanking();
    void staleNullDisabledAndNoMatchLoseRecommendationPriority();
    void nearbyDisconnectKeepsSelectableCacheAndRedBanner();
    void chargeRefreshPreservesForecastUntilReplacement();
    void forecastCacheRequiresCompatibleStationFacts();
    void retryWaitsForRealConnectionAndEmitsOnlySafeReads();
    void globalCurrentSurvivesInactiveChargeSelectionInvalidation();
    void globalCurrentFailureRequiresVisibleRetryBeforeQueuedReads();
    void pendingGlobalCurrentReplaysOnceAcrossSecondReconnect();
    void mutationDisconnectNeverReplaysWritesAndRefreshesCurrentFirst();
    void historyNavigationObeysTask6MutationGate();
};

void RecommendationTest::orderListUsesExactPayloadAndCanonicalDecoder()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    QSignalSpy loaded(&api, &UserApi::orderHistoryLoaded);

    const auto context = api.loadOrderHistory(20, 40, 7, 9);
    const auto request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("order.list"));
    QCOMPARE(request.token, QStringLiteral("task7-token"));
    QCOMPARE(request.payload,
             QJsonObject({{QStringLiteral("limit"), 20}, {QStringLiteral("offset"), 40}}));
    QCOMPARE(context.requestId, request.requestId);
    QCOMPARE(context.sessionGeneration, quint64{1});
    QCOMPARE(context.pageGeneration, quint64{7});
    QCOMPARE(context.readEpoch, quint64{9});
    QCOMPARE(context.limit, qint64{20});
    QCOMPARE(context.offset, qint64{40});

    const QJsonArray items{
        orderObject(3, QStringLiteral("completed"),
                    QStringLiteral("2026-09-01T10:00:00+08:00"),
                    QStringLiteral("2026-09-01T11:00:00+08:00")),
        orderObject(2, QStringLiteral("cancelled"),
                    QStringLiteral("2026-09-01T09:00:00+08:00"),
                    QStringLiteral("2026-09-01T09:30:00+08:00")),
    };
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), items}, {QStringLiteral("total"), 42}});
    QTRY_COMPARE(loaded.size(), 1);
    const auto emittedContext = loaded.at(0).at(0).value<ev::user::HistoryRequestContext>();
    const auto result = loaded.at(0).at(1).value<ev::user::OrderListResult>();
    QCOMPARE(emittedContext, context);
    QCOMPARE(result.total, qint64{42});
    QCOMPARE(result.items.size(), 2);
    QCOMPARE(result.items.constFirst().orderId, qint64{3});
    QCOMPARE(result.items.constLast().status, QStringLiteral("cancelled"));
}

void RecommendationTest::orderListRejectsInvalidRequestPagination_data()
{
    QTest::addColumn<qint64>("limit");
    QTest::addColumn<qint64>("offset");
    QTest::newRow("zero limit") << qint64{0} << qint64{0};
    QTest::newRow("limit above 100") << qint64{101} << qint64{0};
    QTest::newRow("negative offset") << qint64{20} << qint64{-1};
}

void RecommendationTest::orderListRejectsInvalidRequestPagination()
{
    QFETCH(qint64, limit);
    QFETCH(qint64, offset);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    QSignalSpy failed(&api, &UserApi::orderHistoryRequestFailed);
    const auto context = api.loadOrderHistory(limit, offset, 3, 4);
    QVERIFY(context.requestId.isEmpty());
    QCOMPARE(context.limit, limit);
    QCOMPARE(context.offset, offset);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.at(0).at(1).value<ev::user::ApiError>().code,
             QStringLiteral("INVALID_REQUEST"));
    QTest::qWait(20);
    QCOMPARE(peer->bytesAvailable(), qint64{0});
}

void RecommendationTest::orderListRejectsInvalidPages_data()
{
    QTest::addColumn<QString>("kind");
    QTest::newRow("extra top-level field") << QStringLiteral("extra");
    QTest::newRow("negative total") << QStringLiteral("negative-total");
    QTest::newRow("unsafe total") << QStringLiteral("unsafe-total");
    QTest::newRow("too many items") << QStringLiteral("too-many");
    QTest::newRow("foreign user") << QStringLiteral("foreign-user");
    QTest::newRow("reservedAt ascending") << QStringLiteral("ascending");
    QTest::newRow("malformed canonical order") << QStringLiteral("bad-order");
}

void RecommendationTest::orderListRejectsInvalidPages()
{
    QFETCH(QString, kind);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    QSignalSpy failed(&api, &UserApi::orderHistoryRequestFailed);
    QSignalSpy loaded(&api, &UserApi::orderHistoryLoaded);

    const qint64 limit = kind == QStringLiteral("too-many") ? 1 : 20;
    (void)api.loadOrderHistory(limit, 0, 1, 1);
    const auto request = takeRequest(peer.data());
    QJsonArray items{
        orderObject(2, QStringLiteral("completed"),
                    QStringLiteral("2026-09-01T10:00:00+08:00"),
                    QStringLiteral("2026-09-01T11:00:00+08:00")),
        orderObject(1, QStringLiteral("cancelled"),
                    QStringLiteral("2026-09-01T09:00:00+08:00"),
                    QStringLiteral("2026-09-01T09:30:00+08:00")),
    };
    QJsonObject data{{QStringLiteral("items"), items}, {QStringLiteral("total"), 2}};
    if (kind == QStringLiteral("extra")) {
        data.insert(QStringLiteral("cursor"), 1);
    } else if (kind == QStringLiteral("negative-total")) {
        data.insert(QStringLiteral("total"), -1);
    } else if (kind == QStringLiteral("unsafe-total")) {
        data.insert(QStringLiteral("total"), 9'007'199'254'740'992.0);
    } else if (kind == QStringLiteral("foreign-user")) {
        QJsonObject foreign = items.at(0).toObject();
        foreign.insert(QStringLiteral("userId"), 77);
        items.replace(0, foreign);
        data.insert(QStringLiteral("items"), items);
    } else if (kind == QStringLiteral("ascending")) {
        const QJsonValue first = items.at(0);
        items.replace(0, items.at(1));
        items.replace(1, first);
        data.insert(QStringLiteral("items"), items);
    } else if (kind == QStringLiteral("bad-order")) {
        QJsonObject bad = items.at(0).toObject();
        bad.insert(QStringLiteral("unexpected"), true);
        items.replace(0, bad);
        data.insert(QStringLiteral("items"), items);
    }
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(), data);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(loaded.size(), 0);
    const auto error = failed.at(0).at(1).value<ev::user::ApiError>();
    QCOMPARE(error.code, QStringLiteral("INVALID_RESPONSE"));
}

void RecommendationTest::historyRendersOnlyReceivedPageWithExactPagination()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage page(&api);
    page.setConnectionAvailable(true);
    page.activate();
    const auto first = takeRequest(peer.data());
    QCOMPARE(first.action, QStringLiteral("order.list"));
    QCOMPARE(first.payload.value(QStringLiteral("limit")).toInt(), 20);
    QCOMPARE(first.payload.value(QStringLiteral("offset")).toInt(), 0);
    const QJsonArray items{
        orderObject(40, QStringLiteral("completed"),
                    QStringLiteral("2026-09-01T10:00:00+08:00"),
                    QStringLiteral("2026-09-01T10:30:00+08:00")),
        orderObject(30, QStringLiteral("cancelled"),
                    QStringLiteral("2026-09-01T09:00:00+08:00"),
                    QStringLiteral("2026-09-01T11:30:00+08:00")),
        orderObject(20, QStringLiteral("charging"),
                    QStringLiteral("2026-09-01T08:00:00+08:00")),
        orderObject(10, QStringLiteral("reserved"),
                    QStringLiteral("2026-09-01T07:30:00+08:00")),
    };
    reply(peer.data(), first.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), items}, {QStringLiteral("total"), 25}});
    auto *list = required<QListWidget>(&page, "historyList");
    QTRY_COMPARE(list->count(), 4);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("订单 #30")));
    QVERIFY(list->item(1)->text().contains(QStringLiteral("订单 #40")));
    QVERIFY(list->item(2)->text().contains(QStringLiteral("订单 #20")));
    QVERIFY(list->item(3)->text().contains(QStringLiteral("订单 #10")));
    const QString completedText = list->item(1)->text();
    QVERIFY(completedText.contains(QStringLiteral("测试站点40")));
    QVERIFY(completedText.contains(QStringLiteral("C-40")));
    QVERIFY(completedText.contains(QStringLiteral("10.125 kWh")));
    QVERIFY(completedText.contains(QStringLiteral("23.45 元")));
    QVERIFY(completedText.contains(QStringLiteral("已完成")));
    QVERIFY(required<QLabel>(&page, "historyStatus")->text().contains(QStringLiteral("第 1 页")));
    QVERIFY(!required<QPushButton>(&page, "historyPrevButton")->isEnabled());
    QVERIFY(required<QPushButton>(&page, "historyNextButton")->isEnabled());

    required<QPushButton>(&page, "historyNextButton")->click();
    const auto second = takeRequest(peer.data());
    QCOMPARE(second.payload.value(QStringLiteral("limit")).toInt(), 20);
    QCOMPARE(second.payload.value(QStringLiteral("offset")).toInt(), 20);
    reply(peer.data(), second.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{
                           orderObject(9, QStringLiteral("cancelled"),
                                       QStringLiteral("2026-08-31T09:00:00+08:00"),
                                       QStringLiteral("2026-08-31T09:30:00+08:00")),
                           orderObject(8, QStringLiteral("reserved"),
                                       QStringLiteral("2026-08-31T08:00:00+08:00"))}},
                      {QStringLiteral("total"), 22}});
    QTRY_COMPARE(list->count(), 2);
    QVERIFY(required<QPushButton>(&page, "historyPrevButton")->isEnabled());
    QVERIFY(!required<QPushButton>(&page, "historyNextButton")->isEnabled());
}

void RecommendationTest::historyDropsSupersededAndSessionForeignResponses()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage page(&api);
    page.setConnectionAvailable(true);
    page.activate();
    const auto stale = takeRequest(peer.data());
    page.refresh();
    const auto fresh = takeRequest(peer.data());
    QVERIFY(stale.requestId != fresh.requestId);

    ev::user::Order ghost;
    ghost.orderId = 999;
    ghost.status = QStringLiteral("reserved");
    ev::user::OrderListResult ignoredResult;
    ignoredResult.items.append(ghost);
    ignoredResult.total = 1;
    const ev::user::HistoryRequestContext expectedContext{
        fresh.requestId, 1, 2, 2, 20, 0};
    auto wrongPage = expectedContext;
    --wrongPage.pageGeneration;
    api.orderHistoryLoaded(wrongPage, ignoredResult);
    auto wrongEpoch = expectedContext;
    --wrongEpoch.readEpoch;
    api.orderHistoryLoaded(wrongEpoch, ignoredResult);
    auto wrongSession = expectedContext;
    ++wrongSession.sessionGeneration;
    api.orderHistoryLoaded(wrongSession, ignoredResult);
    auto *list = required<QListWidget>(&page, "historyList");
    QCOMPARE(list->count(), 0);

    reply(peer.data(), stale.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{
                           orderObject(90, QStringLiteral("reserved"),
                                       QStringLiteral("2026-09-01T12:00:00+08:00"))}},
                      {QStringLiteral("total"), 1}});
    reply(peer.data(), fresh.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{
                           orderObject(80, QStringLiteral("reserved"),
                                       QStringLiteral("2026-09-01T11:00:00+08:00"))}},
                      {QStringLiteral("total"), 1}});
    QTRY_COMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("#80")));

    page.refresh();
    const auto oldSession = takeRequest(peer.data());
    api.loginByPhone(QString::fromLatin1(kMobile));
    const auto secondLogin = takeRequest(peer.data());
    reply(peer.data(), secondLogin.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("new-session-token")},
                      {QStringLiteral("user"), userObject()}});
    QTRY_COMPARE(list->count(), 0);
    reply(peer.data(), oldSession.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{
                           orderObject(70, QStringLiteral("reserved"),
                                       QStringLiteral("2026-09-01T10:00:00+08:00"))}},
                      {QStringLiteral("total"), 1}});
    QTest::qWait(30);
    QCOMPARE(list->count(), 0);
}

void RecommendationTest::historyFailureAndDisconnectPreserveCommittedCache()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage page(&api);
    page.setConnectionAvailable(true);
    page.activate();
    auto request = takeRequest(peer.data());
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{
                           orderObject(6, QStringLiteral("completed"),
                                       QStringLiteral("2026-09-01T09:00:00+08:00"),
                                       QStringLiteral("2026-09-01T10:00:00+08:00"))}},
                      {QStringLiteral("total"), 1}});
    auto *list = required<QListWidget>(&page, "historyList");
    QTRY_COMPARE(list->count(), 1);
    page.refresh();
    request = takeRequest(peer.data());
    reply(peer.data(), request.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    QTRY_VERIFY(!required<QLabel>(&page, "historyError")->text().isEmpty());
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("#6")));

    peer->disconnectFromHost();
    QTRY_VERIFY(!required<QLabel>(&page, "historyConnectionBanner")->text()
                     .contains(QStringLiteral("已连接")));
    QCOMPARE(list->count(), 1);
    auto *banner = required<QLabel>(&page, "historyConnectionBanner");
    QVERIFY(banner->styleSheet().contains(QStringLiteral("red"), Qt::CaseInsensitive)
            || banner->styleSheet().contains(QStringLiteral("#")));
    QVERIFY(required<QLabel>(&page, "historyStatus")->text().contains(QStringLiteral("缓存")));
}

void RecommendationTest::historyLabelsStoppedChargingFromEndedAt()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage page(&api);
    page.setConnectionAvailable(true);
    page.activate();
    const auto request = takeRequest(peer.data());
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{
                           orderObject(61, QStringLiteral("charging"),
                                       QStringLiteral("2026-09-01T09:00:00+08:00"),
                                       QStringLiteral("2026-09-01T10:00:00+08:00"))}},
                      {QStringLiteral("total"), 1}});
    auto *list = required<QListWidget>(&page, "historyList");
    QTRY_COMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("已停止待结算")));
    QVERIFY(!list->item(0)->text().contains(QStringLiteral("· 充电中")));
}

void RecommendationTest::historyUsesFallbackKeysForEquivalentTimestampInstants()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage page(&api);
    page.setConnectionAvailable(true);
    page.activate();
    const auto request = takeRequest(peer.data());
    const QJsonArray items{
        orderObject(91, QStringLiteral("completed"),
                    QStringLiteral("2026-09-01T09:00:00.0+08:00"),
                    QStringLiteral("2026-09-01T10:00:00.0+08:00")),
        orderObject(92, QStringLiteral("completed"),
                    QStringLiteral("2026-09-01T09:00:00.00+08:00"),
                    QStringLiteral("2026-09-01T10:00:00.00+08:00")),
    };
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), items}, {QStringLiteral("total"), 2}});
    auto *list = required<QListWidget>(&page, "historyList");
    QTRY_COMPARE(list->count(), 2);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("订单 #92")));
    QVERIFY(list->item(1)->text().contains(QStringLiteral("订单 #91")));
}

void RecommendationTest::historyFormatsLargeFenExactly()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage page(&api);
    page.setConnectionAvailable(true);
    page.activate();
    const auto request = takeRequest(peer.data());
    QJsonObject order = orderObject(
        62, QStringLiteral("completed"),
        QStringLiteral("2026-09-01T09:00:00+08:00"),
        QStringLiteral("2026-09-01T10:00:00+08:00"));
    order.insert(QStringLiteral("amountFen"), 9'007'199'254'740'901.0);
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{order}},
                      {QStringLiteral("total"), 1}});
    auto *list = required<QListWidget>(&page, "historyList");
    QTRY_COMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("90071992547409.01 元")));
}

void RecommendationTest::historyNeverExposesRawFailureMessages()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage page(&api);
    page.setConnectionAvailable(true);
    page.activate();
    const auto request = takeRequest(peer.data());
    reply(peer.data(), request.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("raw english database failure"), QJsonObject{});
    auto *error = required<QLabel>(&page, "historyError");
    QTRY_VERIFY(!error->text().isEmpty());
    QVERIFY(!error->text().contains(QStringLiteral("raw english")));
    QVERIFY(error->text().contains(QStringLiteral("服务繁忙")));
}

void RecommendationTest::freshForecastUsesRealTransportAndDeterministicRanking()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    NearbyPage page(&api, nullptr);
    page.setConnectionAvailable(true);
    page.displayStations(decodedStations());
    QSignalSpy invalidated(&page, &NearbyPage::selectionInvalidated);
    page.refreshAfterReconnect();
    const auto listRequest = takeRequest(peer.data());
    QCOMPARE(listRequest.action, QStringLiteral("station.list"));
    reply(peer.data(), listRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), encodedStations()}});
    const auto forecastRequest = takeRequest(peer.data());
    QCOMPARE(forecastRequest.action, QStringLiteral("forecast.latest"));
    const QHash<qint64, qint64> busy{{1, 1}, {2, 3}, {3, 4}, {4, 1}, {5, 2}, {6, 2}};
    reply(peer.data(), forecastRequest.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject(false)},
                      {QStringLiteral("records"), forecastRecords(busy)}});
    QTRY_VERIFY(required<QLabel>(&page, "forecastSource")->text()
                    .contains(QStringLiteral("activatedAt")));
    const QList<qint64> order = visibleStationOrder(page);
    QCOMPARE(order, QList<qint64>({4, 1, 6, 5, 2, 3, 7}));
    QCOMPARE(invalidated.size(), 0);
    QVERIFY(required<QLabel>(&page, "forecastLabel_4")->text().contains(QStringLiteral("空闲 3")));
    QCOMPARE(required<QLabel>(&page, "forecastLabel_7")->text(), QStringLiteral("暂无预测"));
}

void RecommendationTest::staleNullDisabledAndNoMatchLoseRecommendationPriority()
{
    TcpJsonClient client;
    UserApi api(&client);
    NearbyPage page(&api, nullptr);
    page.displayStations(decodedStations());

    ev::user::ForecastLatestResult stale;
    ev::user::ForecastRun run;
    run.runId = QStringLiteral("run-task7");
    run.generatedAt = QStringLiteral("2026-09-01T08:00:00+08:00");
    run.dataCutoff = QStringLiteral("2026-09-01T07:00:00+08:00");
    run.activatedAt = QStringLiteral("2026-09-01T08:01:00+08:00");
    run.modelVersion = QStringLiteral("ridge-v1");
    run.payloadHash = QString(64, QLatin1Char('a'));
    run.stale = true;
    stale.forecastRun = run;
    for (qint64 stationId = 1; stationId <= 6; ++stationId) {
        ev::user::ForecastRecord record;
        record.stationId = stationId;
        record.horizonH = stationId == 6 ? 2 : 1; // station 6 deliberately has no h=1 match.
        record.predictedBusyCount = stationId == 1 ? 1 : 2;
        record.predictedIdleCount = 4 - record.predictedBusyCount;
        record.congestionLevel = stationId == 1 ? QStringLiteral("low") : QStringLiteral("medium");
        stale.records.append(record);
    }
    page.displayForecast(stale);
    QCOMPARE(visibleStationOrder(page), QList<qint64>({7, 4, 2, 6, 5, 1, 3}));
    QVERIFY(required<QLabel>(&page, "forecastLabel_1")->text().contains(QStringLiteral("过期")));
    QCOMPARE(required<QLabel>(&page, "forecastLabel_6")->text(), QStringLiteral("暂无预测"));
    QCOMPARE(required<QLabel>(&page, "forecastLabel_7")->text(), QStringLiteral("暂无预测"));

    page.displayForecast({});
    QCOMPARE(visibleStationOrder(page), QList<qint64>({7, 4, 2, 6, 5, 1, 3}));
    QCOMPARE(required<QLabel>(&page, "forecastLabel_1")->text(), QStringLiteral("暂无预测"));
}

void RecommendationTest::nearbyDisconnectKeepsSelectableCacheAndRedBanner()
{
    TcpJsonClient client;
    UserApi api(&client);
    NearbyPage page(&api, nullptr);
    ev::user::StationListResult stations = decodedStations();
    page.displayStations(stations);
    ev::user::ForecastLatestResult forecast;
    ev::user::ForecastRun run;
    run.runId = QStringLiteral("cached-run");
    run.generatedAt = QStringLiteral("2026-09-01T08:00:00+08:00");
    run.dataCutoff = QStringLiteral("2026-09-01T07:00:00+08:00");
    run.activatedAt = QStringLiteral("2026-09-01T08:01:00+08:00");
    run.modelVersion = QStringLiteral("ridge-v1");
    run.payloadHash = QString(64, QLatin1Char('a'));
    forecast.forecastRun = run;
    ev::user::ForecastRecord record;
    record.stationId = 1;
    record.horizonH = 1;
    record.predictedBusyCount = 1;
    record.predictedIdleCount = 3;
    record.congestionLevel = QStringLiteral("low");
    forecast.records.append(record);
    page.displayForecast(forecast);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ev::user::StationDetailResult detail;
    detail.station = stations.stations.constFirst();
    detail.station.chargerCount = 1;
    detail.station.idleCount = 1;
    ev::user::Charger charger;
    charger.chargerId = 1001;
    charger.stationId = detail.station.stationId;
    charger.code = QStringLiteral("C-1001");
    charger.type = QStringLiteral("fast");
    charger.powerKw = 60.0;
    charger.status = QStringLiteral("idle");
    charger.updatedAt = QStringLiteral("2026-09-01T08:00:00+08:00");
    detail.chargers.append(charger);
    page.displayStationDetail(detail);
    QSignalSpy invalidated(&page, &NearbyPage::selectionInvalidated);
    page.setConnectionAvailable(false);
    for (qint64 stationId = 1; stationId <= 7; ++stationId) {
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("stationName_%1").arg(stationId)) != nullptr);
    }
    auto *banner = required<QLabel>(&page, "nearbyConnectionBanner");
    QVERIFY(banner->text().contains(QStringLiteral("离线"))
            || banner->text().contains(QStringLiteral("不可用")));
    QVERIFY(banner->styleSheet().contains(QStringLiteral("red"), Qt::CaseInsensitive)
            || banner->styleSheet().contains(QStringLiteral("#")));
    QVERIFY(required<QLabel>(&page, "nearbyStatus")->text().contains(QStringLiteral("缓存")));
    QVERIFY(required<QPushButton>(&page, "stationButton_1")->isEnabled());
    QVERIFY(required<QPushButton>(&page, "chargerButton_1001")->isEnabled());
    QVERIFY(required<QLabel>(&page, "forecastLabel_1")->text().contains(QStringLiteral("空闲 3")));
    QCOMPARE(invalidated.size(), 0);
    QVERIFY(required<QPushButton>(&page, "nearbyRetryButton")->isEnabled());
}

void RecommendationTest::chargeRefreshPreservesForecastUntilReplacement()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    NearbyPage page(&api, nullptr);
    page.setConnectionAvailable(true);
    const auto stations = decodedStations();
    page.displayStations(stations);

    ev::user::ForecastLatestResult forecast;
    ev::user::ForecastRun run;
    run.runId = QStringLiteral("charge-cache");
    run.generatedAt = QStringLiteral("2026-09-01T08:00:00+08:00");
    run.dataCutoff = QStringLiteral("2026-09-01T07:00:00+08:00");
    run.activatedAt = QStringLiteral("2026-09-01T08:01:00+08:00");
    run.modelVersion = QStringLiteral("ridge-v1");
    run.payloadHash = QString(64, QLatin1Char('a'));
    forecast.forecastRun = run;
    ev::user::ForecastRecord record;
    record.stationId = 1;
    record.horizonH = 1;
    record.predictedBusyCount = 1;
    record.predictedIdleCount = 3;
    record.congestionLevel = QStringLiteral("low");
    forecast.records.append(record);
    page.displayForecast(forecast);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                .contains(QStringLiteral("空闲 3")));

    page.displayStationDetail(selectableDetail(stations.stations.constFirst()));
    QVERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                .contains(QStringLiteral("空闲 3")));
    QSignalSpy selected(&page, &NearbyPage::chargerSelected);
    required<QPushButton>(&page, "chargerButton_1001")->click();
    QCOMPARE(selected.size(), 1);
    QVERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                .contains(QStringLiteral("空闲 3")));
    const auto selection = selected.constFirst().constFirst()
                               .value<ev::user::StationSelection>();
    page.refreshAfterCharge(selection.origin, selection.station.stationId,
                            selection.selectionGeneration, 77);
    QVERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                .contains(QStringLiteral("空闲 3")));
    const auto list = takeRequest(peer.data());
    QCOMPARE(list.action, QStringLiteral("station.list"));
    reply(peer.data(), list.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    QTRY_VERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                    .contains(QStringLiteral("空闲 3")));
    QCOMPARE(visibleStationOrder(page).constFirst(), qint64{1});
}

void RecommendationTest::forecastCacheRequiresCompatibleStationFacts()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    NearbyPage page(&api, nullptr);
    page.setConnectionAvailable(true);
    page.displayStations(decodedStations());

    ev::user::ForecastLatestResult cached;
    ev::user::ForecastRun run;
    run.runId = QStringLiteral("compatible-cache");
    run.generatedAt = QStringLiteral("2026-09-01T08:00:00+08:00");
    run.dataCutoff = QStringLiteral("2026-09-01T07:00:00+08:00");
    run.activatedAt = QStringLiteral("2026-09-01T08:01:00+08:00");
    run.modelVersion = QStringLiteral("ridge-v1");
    run.payloadHash = QString(64, QLatin1Char('a'));
    cached.forecastRun = run;
    ev::user::ForecastRecord first;
    first.stationId = 1;
    first.horizonH = 1;
    first.predictedBusyCount = 1;
    first.predictedIdleCount = 3;
    first.congestionLevel = QStringLiteral("low");
    cached.records.append(first);
    page.displayForecast(cached);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    page.refreshAfterReconnect();
    auto list = takeRequest(peer.data());
    reply(peer.data(), list.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    QTRY_VERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                    .contains(QStringLiteral("空闲 3")));

    page.refreshAfterReconnect();
    list = takeRequest(peer.data());
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), encodedStations()}});
    auto forecast = takeRequest(peer.data());
    QCOMPARE(forecast.action, QStringLiteral("forecast.latest"));
    reply(peer.data(), forecast.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                    .contains(QStringLiteral("空闲 3")));
    QCOMPARE(visibleStationOrder(page).constFirst(), qint64{1});

    page.refreshAfterReconnect();
    list = takeRequest(peer.data());
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"),
                       encodedStationsWithSwappedForecastEnablement()}});
    forecast = takeRequest(peer.data());
    QCOMPARE(forecast.action, QStringLiteral("forecast.latest"));
    reply(peer.data(), forecast.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_COMPARE(required<QLabel>(&page, "forecastLabel_1")->text(),
                 QStringLiteral("暂无预测"));
    QCOMPARE(required<QLabel>(&page, "forecastLabel_7")->text(),
             QStringLiteral("暂无预测"));
    QVERIFY(visibleStationOrder(page).indexOf(1)
            > visibleStationOrder(page).indexOf(7));

    page.refreshAfterReconnect();
    list = takeRequest(peer.data());
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), encodedStationsWithFirstCount(5)}});
    forecast = takeRequest(peer.data());
    reply(peer.data(), forecast.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("busy"), QJsonObject{});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_COMPARE(required<QLabel>(&page, "forecastLabel_1")->text(),
                 QStringLiteral("暂无预测"));
    const auto fallbackOrder = visibleStationOrder(page);
    QVERIFY(fallbackOrder.indexOf(1) > fallbackOrder.indexOf(7));

    page.refreshAfterReconnect();
    list = takeRequest(peer.data());
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), encodedStationsWithFirstCount(5)}});
    forecast = takeRequest(peer.data());
    const QHash<qint64, qint64> busy{{1, 1}, {2, 3}, {3, 4}, {4, 1}, {5, 2}, {6, 2}};
    reply(peer.data(), forecast.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), forecastRunObject(false)},
                      {QStringLiteral("records"),
                       forecastRecords(busy, QHash<qint64, qint64>{{1, 5}})}});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTRY_VERIFY(required<QLabel>(&page, "forecastLabel_1")->text()
                    .contains(QStringLiteral("空闲 4")));
    QCOMPARE(visibleStationOrder(page).constFirst(), qint64{1});
}

void RecommendationTest::retryWaitsForRealConnectionAndEmitsOnlySafeReads()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    TcpJsonClient client;
    client.configure(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY(connectToFakeServer(client, server));
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    UserApi api(&client);
    login(api, peer.data());
    HistoryPage history(&api);
    NearbyPage nearby(&api, nullptr);
    QObject::connect(&api, &UserApi::connectionChanged,
                     &nearby, &NearbyPage::setConnectionAvailable);
    QObject::connect(&api, &UserApi::connectionChanged, &nearby,
                     [&nearby, &history](bool connected) {
        if (connected) {
            nearby.refreshAfterReconnect();
            history.refreshAfterReconnect();
        }
    });
    history.setConnectionAvailable(true);
    nearby.setConnectionAvailable(true);
    nearby.displayStations(decodedStations());
    history.activate();
    auto historySeed = takeRequest(peer.data());
    reply(peer.data(), historySeed.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{}}, {QStringLiteral("total"), 0}});

    peer->disconnectFromHost();
    QTRY_VERIFY(peer->state() == QAbstractSocket::UnconnectedState);
    required<QPushButton>(&history, "historyRetryButton")->click();
    QTest::qWait(50);
    QVERIFY(!server.hasPendingConnections() || takeRequest(peer.data(), 20).action.isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 2'000);
    peer.reset(server.nextPendingConnection());
    QStringList actions;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1'000 && actions.size() < 3) {
        const auto request = takeRequest(peer.data(), 100);
        if (!request.action.isEmpty()) {
            actions.append(request.action);
            if (request.action == QStringLiteral("station.list")) {
                reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
                      QJsonObject{{QStringLiteral("stations"), encodedStations()}});
            } else if (request.action == QStringLiteral("order.list")) {
                reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
                      QJsonObject{{QStringLiteral("items"), QJsonArray{}},
                                  {QStringLiteral("total"), 0}});
            } else if (request.action == QStringLiteral("forecast.latest")) {
                reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
                      QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)},
                                  {QStringLiteral("records"), QJsonArray{}}});
            }
        }
    }
    QVERIFY(actions.contains(QStringLiteral("order.list")));
    QVERIFY(actions.contains(QStringLiteral("station.list")));
    QVERIFY(actions.contains(QStringLiteral("forecast.latest")));
    for (const QString &action : std::as_const(actions)) {
        QVERIFY(action == QStringLiteral("order.list") || action == QStringLiteral("station.list")
                || action == QStringLiteral("forecast.latest")
                || action == QStringLiteral("order.current")
                || action == QStringLiteral("station.detail"));
        QVERIFY(!action.startsWith(QStringLiteral("charge.")));
        QVERIFY(action != QStringLiteral("order.cancel"));
        QVERIFY(action != QStringLiteral("wallet.recharge"));
        QVERIFY(action != QStringLiteral("user.update"));
    }
}

void RecommendationTest::globalCurrentSurvivesInactiveChargeSelectionInvalidation()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.tencentMapKey = QStringLiteral("test-map-key");
    MainWindow window(config);
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer != nullptr);
    completeMainLoginWithoutOrder(window, peer.data());

    auto *nearby = required<NearbyPage>(&window, "nearbyPage");
    const auto stations = decodedStations();
    nearby->displayStations(stations);
    nearby->displayStationDetail(selectableDetail(stations.stations.constFirst()));
    required<QPushButton>(nearby, "chargerButton_1001")->click();
    auto *pages = required<QStackedWidget>(&window, "mainPages");
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("chargePage"));
    required<QPushButton>(&window, "nearbyNavigationButton")->click();
    QTRY_COMPARE(pages->currentWidget()->objectName(), QStringLiteral("nearbyPage"));

    peer->disconnectFromHost();
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    peer.reset(waitForPeer(server));
    QVERIFY(peer != nullptr);
    const auto current = takeRequest(peer.data());
    QCOMPARE(current.action, QStringLiteral("order.current"));

    nearby->displayStations(decodedStations());
    reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    QTest::qWait(30);
    nearby->displayStationDetail(selectableDetail(stations.stations.constFirst(), 1002));
    required<QPushButton>(nearby, "chargerButton_1002")->click();
    QTRY_COMPARE_WITH_TIMEOUT(pages->currentWidget()->objectName(),
                              QStringLiteral("chargePage"), 1'000);
}

void RecommendationTest::globalCurrentFailureRequiresVisibleRetryBeforeQueuedReads()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.tencentMapKey = QStringLiteral("test-map-key");
    MainWindow window(config);
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer != nullptr);
    completeMainLoginWithoutOrder(window, peer.data());
    auto *nearby = required<NearbyPage>(&window, "nearbyPage");
    const auto stations = decodedStations();
    nearby->displayStations(stations);

    peer->disconnectFromHost();
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    peer.reset(waitForPeer(server));
    QVERIFY(peer != nullptr);
    auto current = takeRequest(peer.data());
    QCOMPARE(current.action, QStringLiteral("order.current"));
    QCOMPARE(takeRequest(peer.data(), 100).action, QString());
    reply(peer.data(), current.requestId, false, QStringLiteral("DB_BUSY"),
          QStringLiteral("raw current failure"), QJsonObject{});

    auto *status = window.findChild<QLabel *>(QStringLiteral("currentAuthorityStatus"));
    auto *retry = window.findChild<QPushButton *>(QStringLiteral("currentAuthorityRetryButton"));
    QVERIFY(status != nullptr);
    QVERIFY(retry != nullptr);
    QTRY_VERIFY(!status->isHidden());
    QVERIFY(status->text().contains(QStringLiteral("当前订单")));
    QVERIFY(!status->text().contains(QStringLiteral("raw current failure")));
    QTRY_VERIFY(retry->isEnabled());
    auto *pages = required<QStackedWidget>(&window, "mainPages");
    QVERIFY(!pages->isEnabled());

    const auto detail = selectableDetail(stations.stations.constFirst());
    nearby->chargerSelected({stations.origin, detail.station,
                             detail.chargers.constFirst(), 999});
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("nearbyPage"));

    retry->click();
    current = takeRequest(peer.data());
    QCOMPARE(current.action, QStringLiteral("order.current"));
    QCOMPARE(takeRequest(peer.data(), 100).action, QString());
    reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    const auto list = takeRequest(peer.data());
    QCOMPARE(list.action, QStringLiteral("station.list"));
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), encodedStations()}});
    const auto forecast = takeRequest(peer.data());
    QCOMPARE(forecast.action, QStringLiteral("forecast.latest"));
    reply(peer.data(), forecast.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)},
                      {QStringLiteral("records"), QJsonArray{}}});
    QTRY_VERIFY(status->isHidden());
    QVERIFY(pages->isEnabled());
}

void RecommendationTest::pendingGlobalCurrentReplaysOnceAcrossSecondReconnect()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.tencentMapKey = QStringLiteral("test-map-key");
    MainWindow window(config);
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer != nullptr);
    completeMainLoginWithoutOrder(window, peer.data());
    required<NearbyPage>(&window, "nearbyPage")->displayStations(decodedStations());
    required<QPushButton>(&window, "historyNavigationButton")->click();
    const auto initialHistory = takeRequest(peer.data());
    QCOMPARE(initialHistory.action, QStringLiteral("order.list"));
    reply(peer.data(), initialHistory.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{}},
                      {QStringLiteral("total"), 0}});
    QTRY_COMPARE(required<QStackedWidget>(&window, "mainPages")
                     ->currentWidget()->objectName(),
                 QStringLiteral("historyPage"));

    peer->disconnectFromHost();
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    peer.reset(waitForPeer(server));
    QVERIFY(peer != nullptr);
    const auto firstCurrent = takeRequest(peer.data());
    QCOMPARE(firstCurrent.action, QStringLiteral("order.current"));
    QCOMPARE(takeRequest(peer.data(), 100).action, QString());

    peer->disconnectFromHost();
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    peer.reset(waitForPeer(server));
    QVERIFY(peer != nullptr);
    const auto replayedCurrent = takeRequest(peer.data());
    QCOMPARE(replayedCurrent.action, QStringLiteral("order.current"));
    QCOMPARE(replayedCurrent.requestId, firstCurrent.requestId);
    QCOMPARE(takeRequest(peer.data(), 100).action, QString());
    reply(peer.data(), replayedCurrent.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    const auto list = takeRequest(peer.data());
    QCOMPARE(list.action, QStringLiteral("station.list"));
    const auto history = takeRequest(peer.data());
    QCOMPARE(history.action, QStringLiteral("order.list"));
    QCOMPARE(history.payload.value(QStringLiteral("limit")).toInt(), 20);
    QCOMPARE(history.payload.value(QStringLiteral("offset")).toInt(), 0);
    reply(peer.data(), list.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("stations"), encodedStations()}});
    reply(peer.data(), history.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("items"), QJsonArray{}},
                      {QStringLiteral("total"), 0}});
    const auto forecast = takeRequest(peer.data());
    QCOMPARE(forecast.action, QStringLiteral("forecast.latest"));
    reply(peer.data(), forecast.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)},
                      {QStringLiteral("records"), QJsonArray{}}});
    auto *status = required<QLabel>(&window, "currentAuthorityStatus");
    QTRY_VERIFY(status->isHidden());
    QVERIFY(required<QStackedWidget>(&window, "mainPages")->isEnabled());
}

void RecommendationTest::mutationDisconnectNeverReplaysWritesAndRefreshesCurrentFirst()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.tencentMapKey = QStringLiteral("test-map-key");
    MainWindow window(config);
    QScopedPointer<QTcpSocket> peer(waitForPeer(server));
    QVERIFY(peer != nullptr);
    completeMainLoginWithoutOrder(window, peer.data());

    auto *nearby = required<NearbyPage>(&window, "nearbyPage");
    const auto stations = decodedStations();
    nearby->displayStations(stations);
    nearby->displayStationDetail(selectableDetail(stations.stations.constFirst()));
    required<QPushButton>(nearby, "chargerButton_1001")->click();
    auto *reserve = required<QPushButton>(&window, "chargeReserveButton");
    QTRY_VERIFY(reserve->isEnabled());
    reserve->click();
    const auto mutation = takeRequest(peer.data());
    QCOMPARE(mutation.action, QStringLiteral("charge.reserve"));

    peer->disconnectFromHost();
    QTRY_COMPARE(peer->state(), QAbstractSocket::UnconnectedState);
    peer.reset(waitForPeer(server));
    QVERIFY(peer != nullptr);
    const auto current = takeRequest(peer.data());
    QCOMPARE(current.action, QStringLiteral("order.current"));
    QVERIFY(current.requestId != mutation.requestId);
    reply(peer.data(), current.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});

    QStringList replayActions;
    QStringList replayRequestIds;
    QElapsedTimer quiet;
    QElapsedTimer total;
    quiet.start();
    total.start();
    while (quiet.elapsed() < 350 && total.elapsed() < 3'000) {
        const auto request = takeRequest(peer.data(), 75);
        if (request.action.isEmpty()) {
            continue;
        }
        quiet.restart();
        replayActions.append(request.action);
        replayRequestIds.append(request.requestId);
        if (request.action == QStringLiteral("station.detail")) {
            reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
                  stationDetailData());
        } else if (request.action == QStringLiteral("station.list")) {
            reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("stations"), encodedStations()}});
        } else if (request.action == QStringLiteral("forecast.latest")) {
            reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
                  QJsonObject{{QStringLiteral("forecastRun"), QJsonValue(QJsonValue::Null)},
                              {QStringLiteral("records"), QJsonArray{}}});
        }
    }
    QVERIFY(replayActions.contains(QStringLiteral("station.detail")));
    QVERIFY(replayActions.contains(QStringLiteral("station.list")));
    QVERIFY(replayActions.contains(QStringLiteral("forecast.latest")));
    QCOMPARE(replayActions.size(), 3);
    QCOMPARE(replayActions.count(QStringLiteral("station.detail")), 1);
    QCOMPARE(replayActions.count(QStringLiteral("station.list")), 1);
    QCOMPARE(replayActions.count(QStringLiteral("forecast.latest")), 1);
    QVERIFY(!replayRequestIds.contains(mutation.requestId));
    for (const QString &action : std::as_const(replayActions)) {
        QVERIFY(action == QStringLiteral("station.detail")
                || action == QStringLiteral("station.list")
                || action == QStringLiteral("forecast.latest"));
        QVERIFY(!action.startsWith(QStringLiteral("charge.")));
        QVERIFY(action != QStringLiteral("order.cancel"));
        QVERIFY(action != QStringLiteral("wallet.recharge"));
        QVERIFY(action != QStringLiteral("user.update"));
    }
}

void RecommendationTest::historyNavigationObeysTask6MutationGate()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    UserAppConfig config;
    config.serverHost = QStringLiteral("127.0.0.1");
    config.serverPort = server.serverPort();
    config.tencentMapKey = QStringLiteral("test-map-key");
    MainWindow window(config);
    QTRY_VERIFY(server.hasPendingConnections());
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    required<QLineEdit>(&window, "phoneEdit")->setText(QString::fromLatin1(kMobile));
    required<QPushButton>(&window, "loginButton")->click();
    auto request = takeRequest(peer.data());
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("token"), QStringLiteral("main-token")},
                      {QStringLiteral("user"), userObject()}});
    request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("order.current"));
    reply(peer.data(), request.requestId, true, QStringLiteral("OK"), QString(),
          QJsonObject{{QStringLiteral("order"), QJsonValue(QJsonValue::Null)}});
    auto *historyButton = required<QPushButton>(&window, "historyNavigationButton");
    QTRY_VERIFY(historyButton->isEnabled());

    auto *nearby = required<NearbyPage>(&window, "nearbyPage");
    ev::user::StationListResult stations = decodedStations();
    stations.stations.resize(1);
    nearby->displayStations(stations);
    ev::user::StationDetailResult detail;
    detail.station = stations.stations.constFirst();
    detail.station.chargerCount = 1;
    detail.station.idleCount = 1;
    ev::user::Charger charger;
    charger.chargerId = 1001;
    charger.stationId = detail.station.stationId;
    charger.code = QStringLiteral("C-1001");
    charger.type = QStringLiteral("fast");
    charger.powerKw = 60.0;
    charger.status = QStringLiteral("idle");
    charger.updatedAt = QStringLiteral("2026-09-01T08:00:00+08:00");
    detail.chargers.append(charger);
    nearby->displayStationDetail(detail);
    required<QPushButton>(nearby, "chargerButton_1001")->click();
    auto *reserve = required<QPushButton>(&window, "chargeReserveButton");
    QTRY_VERIFY(reserve->isEnabled());
    reserve->click();
    request = takeRequest(peer.data());
    QCOMPARE(request.action, QStringLiteral("charge.reserve"));
    QTRY_VERIFY(!historyButton->isEnabled());
}

QTEST_MAIN(RecommendationTest)

#include "tst_recommendation.moc"
