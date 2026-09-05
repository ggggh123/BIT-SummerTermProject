#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/TelemetryEngine.h"
#include "net/SimulatorClient.h"
#include "protocol/FrameCodec.h"
#include "protocol/JsonEnvelope.h"

using namespace ev::simulator;
using namespace ev::protocol;

class FakeServer : public QObject
{
    Q_OBJECT
public:
    bool listen(quint16 port = 0)
    {
        connect(&server_, &QTcpServer::newConnection, this, &FakeServer::onNewConnection);
        return server_.listen(QHostAddress::LocalHost, port);
    }

    quint16 port() const { return server_.serverPort(); }
    const QList<RequestEnvelope> &requests() const { return requests_; }
    void disconnectOnNextEvent() { disconnectOnNextEvent_ = true; }
    void setAutoReplyStatus(bool enabled) { autoReplyStatus_ = enabled; }
    void setChargerStatus(const QString &status) { chargerStatus_ = status; }
    void setStatusCode(const QString &code) { statusCode_ = code; }
    void setNextEventCode(const QString &code) { nextEventCode_ = code; }
    int statusRequestCount() const
    {
        int count = 0;
        for (const RequestEnvelope &request : requests_) {
            if (request.action == QStringLiteral("simulator.status"))
                ++count;
        }
        return count;
    }
    void replyPendingStatus() { replyStatus(); }

private slots:
    void onNewConnection()
    {
        conn_ = server_.nextPendingConnection();
        decoder_.reset();
        connect(conn_, &QTcpSocket::readyRead, this, &FakeServer::onReadyRead);
    }

    void onReadyRead()
    {
        const QList<QByteArray> frames = decoder_.append(conn_->readAll());
        for (const QByteArray &frame : frames) {
            RequestEnvelope req = parseRequest(frame);
            requests_.append(req);
            if (req.action == QStringLiteral("simulator.status")) {
                pendingStatusId_ = req.requestId;
                if (autoReplyStatus_)
                    replyStatus();
            } else if (disconnectOnNextEvent_) {
                disconnectOnNextEvent_ = false;
                conn_->abort();
            } else {
                const QString code = nextEventCode_;
                nextEventCode_ = QStringLiteral("OK");
                reply(req.requestId, code);
            }
        }
    }

private:
    void replyStatus()
    {
        if (!conn_)
            return;
        ResponseEnvelope resp;
        resp.requestId = pendingStatusId_;
        resp.ok = statusCode_ == QLatin1String("OK");
        resp.code = statusCode_;
        resp.message = resp.ok ? QStringLiteral("ok") : QStringLiteral("rejected");

        if (!resp.ok) {
            resp.data = QJsonObject{};
            conn_->write(encodeFrame(toJson(resp)));
            return;
        }

        QJsonObject charger;
        charger[QStringLiteral("chargerId")] = 1001;
        charger[QStringLiteral("stationId")] = 1;
        charger[QStringLiteral("code")] = QStringLiteral("1001");
        charger[QStringLiteral("type")] = QStringLiteral("fast");
        charger[QStringLiteral("powerKw")] = 60.0;
        charger[QStringLiteral("status")] = chargerStatus_;
        charger[QStringLiteral("chargeCount")] = 0;
        charger[QStringLiteral("totalDurationSec")] = 0;
        charger[QStringLiteral("updatedAt")] = QStringLiteral("2026-09-01T09:00:00+08:00");

        QJsonArray chargers;
        chargers.append(charger);

        QJsonObject data;
        data[QStringLiteral("acceptedAt")] = QStringLiteral("2026-09-01T09:00:00+08:00");
        data[QStringLiteral("chargers")] = chargers;
        resp.data = data;

        conn_->write(encodeFrame(toJson(resp)));
    }

    void reply(const QString &requestId, const QString &code)
    {
        ResponseEnvelope resp;
        resp.requestId = requestId;
        resp.ok = code == QLatin1String("OK");
        resp.code = code;
        resp.message = resp.ok ? QStringLiteral("ok") : QStringLiteral("rejected");
        resp.data = QJsonObject{};
        conn_->write(encodeFrame(toJson(resp)));
    }

    QTcpServer server_;
    QTcpSocket *conn_ = nullptr;
    FrameDecoder decoder_;
    QList<RequestEnvelope> requests_;
    QString pendingStatusId_;
    bool disconnectOnNextEvent_ = false;
    bool autoReplyStatus_ = true;
    QString chargerStatus_ = QStringLiteral("idle");
    QString statusCode_ = QStringLiteral("OK");
    QString nextEventCode_ = QStringLiteral("OK");
};

class SimulatorClientTest : public QObject
{
    Q_OBJECT
private slots:
    void reconnectDelaySequence();
    void requestIdIsStableForSample();
    void sendsStatusAndTelemetryToFakeServer();
    void queuesTelemetryWhileDisconnected();
    void statusReportsActualRunState();
    void encodedEventTimestampsKeepMilliseconds();
    void queuedEventsFlushAfterConnecting();
    void unacknowledgedEventResendsWithSameRequestId();
    void periodicallyRefreshesAuthoritativeSnapshot();
    void statusRefreshIsSingleFlight();
    void conflictTriggersImmediateAuthoritativeRefresh();
    void emptyTokenReportsAuthenticationFailureWithoutReadySession();
    void doesNotFlushEventsBeforeAuthenticatedSnapshot();
};

void SimulatorClientTest::reconnectDelaySequence()
{
    QCOMPARE(SimulatorClient::reconnectDelayMs(0), 1000);
    QCOMPARE(SimulatorClient::reconnectDelayMs(1), 2000);
    QCOMPARE(SimulatorClient::reconnectDelayMs(2), 4000);
    QCOMPARE(SimulatorClient::reconnectDelayMs(5), 4000);
}

void SimulatorClientTest::requestIdIsStableForSample()
{
    TelemetrySample s;
    s.chargerId = 1001;
    s.recordedAt = QDateTime::fromMSecsSinceEpoch(
        1234567890000LL, QTimeZone::fromSecondsAheadOfUtc(8 * 3600));
    const QString a = SimulatorClient::requestIdForSample(s);
    const QString b = SimulatorClient::requestIdForSample(s);
    QCOMPARE(a, b);
    QVERIFY(a.contains(QStringLiteral("telemetry-1001")));
}

void SimulatorClientTest::sendsStatusAndTelemetryToFakeServer()
{
    FakeServer server;
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        3000);

    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");

    SimulatorClient client(config, &engine);
    QSignalSpy connectedSpy(&client, &SimulatorClient::connected);
    QSignalSpy chargersSpy(&client, &SimulatorClient::chargersReceived);

    client.start();
    QVERIFY(connectedSpy.wait(3000));

    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 1, 3000);
    QCOMPARE(server.requests().at(0).action, QStringLiteral("simulator.status"));
    QTRY_COMPARE_WITH_TIMEOUT(chargersSpy.count(), 1, 3000);

    TelemetrySample s;
    s.chargerId = 1001;
    s.recordedAt = engine.currentTime();
    s.powerKw = 60.0;
    s.energyIncrementKwh = 0.05;
    s.status = QStringLiteral("charging");
    client.sendTelemetry({s});

    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 2, 3000);
    const RequestEnvelope &req = server.requests().last();
    QCOMPARE(req.action, QStringLiteral("telemetry.push"));
    QCOMPARE(req.token, QStringLiteral("sim-token"));
    QCOMPARE(req.payload.value(QStringLiteral("chargerId")).toInt(), 1001);
    QCOMPARE(req.payload.value(QStringLiteral("energyIncrementKwh")).toDouble(), 0.05);
    QCOMPARE(req.payload.value(QStringLiteral("status")).toString(),
             QStringLiteral("charging"));

    client.stop();
}

void SimulatorClientTest::queuesTelemetryWhileDisconnected()
{
    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        3000);

    SimulatorConfig config;  // no server; stays disconnected
    SimulatorClient client(config, &engine);

    QList<TelemetrySample> samples;
    for (int i = 0; i < 250; ++i) {
        TelemetrySample s;
        s.chargerId = 1001;
        s.recordedAt = engine.currentTime();
        samples.append(s);
    }
    client.sendTelemetry(samples);

    QCOMPARE(client.queuedSamples(), 200);  // bounded queue, oldest dropped
}

// R13 regression: simulator.status must report the real run state instead of
// a hardcoded "running".
void SimulatorClientTest::statusReportsActualRunState()
{
    FakeServer server;
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        3000);

    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();

    SimulatorClient client(config, &engine);
    QSignalSpy connectedSpy(&client, &SimulatorClient::connected);
    client.start();
    QVERIFY(connectedSpy.wait(3000));

    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 1, 3000);
    QCOMPARE(server.requests().at(0).action, QStringLiteral("simulator.status"));
    // Telemetry is not flowing yet: the truthful initial state is "paused".
    QCOMPARE(server.requests().at(0).payload.value(QStringLiteral("state")).toString(),
             QStringLiteral("paused"));

    client.setRunning(true);
    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 2, 3000);
    QCOMPARE(server.requests().last().action, QStringLiteral("simulator.status"));
    QCOMPARE(server.requests().last().payload.value(QStringLiteral("state")).toString(),
             QStringLiteral("running"));

    client.setRunning(false);
    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 3, 3000);
    QCOMPARE(server.requests().last().action, QStringLiteral("simulator.status"));
    QCOMPARE(server.requests().last().payload.value(QStringLiteral("state")).toString(),
             QStringLiteral("paused"));

    client.stop();
}

void SimulatorClientTest::encodedEventTimestampsKeepMilliseconds()
{
    FakeServer server;
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        3000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");

    SimulatorClient client(config, &engine);
    QSignalSpy connectedSpy(&client, &SimulatorClient::connected);
    client.start();
    QVERIFY(connectedSpy.wait(3000));
    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 1, 3000);

    TelemetrySample telemetry;
    telemetry.chargerId = 1001;
    telemetry.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.001+08:00"), Qt::ISODateWithMs);
    telemetry.status = QStringLiteral("idle");
    client.sendTelemetry({telemetry});

    FaultIntent fault;
    fault.chargerId = 1001;
    fault.fault = true;
    fault.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.002+08:00"), Qt::ISODateWithMs);
    client.sendFault(fault);

    FaultIntent recovery = fault;
    recovery.fault = false;
    recovery.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.003+08:00"), Qt::ISODateWithMs);
    client.sendFault(recovery);

    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 4, 3000);
    QCOMPARE(server.requests().at(1).payload.value(QStringLiteral("recordedAt")).toString(),
             QStringLiteral("2026-09-01T09:00:00.001+08:00"));
    QCOMPARE(server.requests().at(2).payload.value(QStringLiteral("recordedAt")).toString(),
             QStringLiteral("2026-09-01T09:00:00.002+08:00"));
    QCOMPARE(server.requests().at(3).payload.value(QStringLiteral("recordedAt")).toString(),
             QStringLiteral("2026-09-01T09:00:00.003+08:00"));
    QVERIFY(server.requests().at(1).payload.value(QStringLiteral("recordedAt")).toString()
            < server.requests().at(2).payload.value(QStringLiteral("recordedAt")).toString());
    QVERIFY(server.requests().at(2).payload.value(QStringLiteral("recordedAt")).toString()
            < server.requests().at(3).payload.value(QStringLiteral("recordedAt")).toString());

    client.stop();
}

void SimulatorClientTest::queuedEventsFlushAfterConnecting()
{
    FakeServer server;
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        3000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");

    SimulatorClient client(config, &engine);
    TelemetrySample telemetry;
    telemetry.chargerId = 1001;
    telemetry.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.001+08:00"), Qt::ISODateWithMs);
    telemetry.status = QStringLiteral("idle");
    client.sendTelemetry({telemetry});

    FaultIntent fault;
    fault.chargerId = 1001;
    fault.fault = true;
    fault.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.002+08:00"), Qt::ISODateWithMs);
    client.sendFault(fault);

    FaultIntent recovery = fault;
    recovery.fault = false;
    recovery.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.003+08:00"), Qt::ISODateWithMs);
    client.sendFault(recovery);

    client.start();
    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 4, 3000);
    QCOMPARE(server.requests().at(1).action, QStringLiteral("telemetry.push"));
    QCOMPARE(server.requests().at(2).action, QStringLiteral("simulator.fault_set"));
    QCOMPARE(server.requests().at(3).action, QStringLiteral("simulator.fault_set"));
    QCOMPARE(server.requests().at(1).payload.value(QStringLiteral("recordedAt")).toString(),
             QStringLiteral("2026-09-01T09:00:00.001+08:00"));
    QCOMPARE(server.requests().at(2).payload.value(QStringLiteral("recordedAt")).toString(),
             QStringLiteral("2026-09-01T09:00:00.002+08:00"));
    QCOMPARE(server.requests().at(3).payload.value(QStringLiteral("recordedAt")).toString(),
             QStringLiteral("2026-09-01T09:00:00.003+08:00"));

    client.stop();
}

void SimulatorClientTest::unacknowledgedEventResendsWithSameRequestId()
{
    FakeServer server;
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        3000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");

    SimulatorClient client(config, &engine);
    QSignalSpy connectedSpy(&client, &SimulatorClient::connected);
    client.start();
    QVERIFY(connectedSpy.wait(3000));
    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 1, 3000);

    TelemetrySample telemetry;
    telemetry.chargerId = 1001;
    telemetry.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.001+08:00"), Qt::ISODateWithMs);
    telemetry.status = QStringLiteral("idle");
    server.disconnectOnNextEvent();
    client.sendTelemetry({telemetry});

    QTRY_VERIFY_WITH_TIMEOUT(server.requests().size() >= 4, 5000);
    QList<QString> telemetryIds;
    for (const RequestEnvelope &request : server.requests()) {
        if (request.action == QStringLiteral("telemetry.push"))
            telemetryIds.append(request.requestId);
    }
    QCOMPARE(telemetryIds.size(), 2);
    QCOMPARE(telemetryIds.at(0), telemetryIds.at(1));

    client.stop();
}

void SimulatorClientTest::periodicallyRefreshesAuthoritativeSnapshot()
{
    FakeServer server;
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        1000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");
    config.intervalMs = 1000;

    SimulatorClient client(config, &engine);
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(server.statusRequestCount(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(engine.chargers().first().status,
                              QStringLiteral("idle"), 3000);

    server.setChargerStatus(QStringLiteral("charging"));
    QTRY_VERIFY_WITH_TIMEOUT(server.statusRequestCount() >= 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(engine.chargers().first().status,
                              QStringLiteral("charging"), 3000);

    QList<QString> statusIds;
    for (const RequestEnvelope &request : server.requests()) {
        if (request.action == QStringLiteral("simulator.status"))
            statusIds.append(request.requestId);
    }
    QVERIFY(statusIds.size() >= 2);
    QVERIFY(statusIds.at(0) != statusIds.at(1));

    client.stop();
}

void SimulatorClientTest::statusRefreshIsSingleFlight()
{
    FakeServer server;
    server.setAutoReplyStatus(false);
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        10000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");
    config.intervalMs = 10000;

    SimulatorClient client(config, &engine);
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(server.statusRequestCount(), 1, 3000);
    client.setRunning(true);
    QTest::qWait(200);
    QCOMPARE(server.statusRequestCount(), 1);

    server.replyPendingStatus();
    QTRY_COMPARE_WITH_TIMEOUT(server.statusRequestCount(), 2, 1000);
    QCOMPARE(server.requests().last().payload.value(QStringLiteral("state")).toString(),
             QStringLiteral("running"));

    client.stop();
}

void SimulatorClientTest::conflictTriggersImmediateAuthoritativeRefresh()
{
    FakeServer server;
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        10000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");
    config.intervalMs = 10000;

    SimulatorClient client(config, &engine);
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(server.statusRequestCount(), 1, 3000);
    server.setChargerStatus(QStringLiteral("charging"));
    server.setNextEventCode(QStringLiteral("ORDER_STATE_CONFLICT"));

    TelemetrySample telemetry;
    telemetry.chargerId = 1001;
    telemetry.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.001+08:00"), Qt::ISODateWithMs);
    telemetry.status = QStringLiteral("idle");
    client.sendTelemetry({telemetry});

    QTRY_COMPARE_WITH_TIMEOUT(server.statusRequestCount(), 2, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(engine.chargers().first().status,
                              QStringLiteral("charging"), 1000);

    client.stop();
}

void SimulatorClientTest::emptyTokenReportsAuthenticationFailureWithoutReadySession()
{
    FakeServer server;
    server.setStatusCode(QStringLiteral("AUTH_REQUIRED"));
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        3000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();

    SimulatorClient client(config, &engine);
    QSignalSpy readySpy(&client, &SimulatorClient::sessionReady);
    QSignalSpy authFailureSpy(&client, &SimulatorClient::authenticationFailed);
    client.start();

    QTRY_COMPARE_WITH_TIMEOUT(authFailureSpy.count(), 1, 3000);
    QCOMPARE(authFailureSpy.at(0).at(0).toString(), QStringLiteral("AUTH_REQUIRED"));
    QCOMPARE(readySpy.count(), 0);
    QCOMPARE(engine.chargers().size(), 0);

    client.stop();
}

void SimulatorClientTest::doesNotFlushEventsBeforeAuthenticatedSnapshot()
{
    FakeServer server;
    server.setAutoReplyStatus(false);
    QVERIFY(server.listen());

    TelemetryEngine engine(20260901,
        QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"), Qt::ISODate),
        10000);
    SimulatorConfig config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = server.port();
    config.token = QStringLiteral("sim-token");
    config.intervalMs = 10000;

    SimulatorClient client(config, &engine);
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(server.statusRequestCount(), 1, 3000);

    TelemetrySample telemetry;
    telemetry.chargerId = 1001;
    telemetry.recordedAt = QDateTime::fromString(
        QStringLiteral("2026-09-01T09:00:00.001+08:00"), Qt::ISODateWithMs);
    telemetry.status = QStringLiteral("idle");
    client.sendTelemetry({telemetry});
    QTest::qWait(200);
    QCOMPARE(server.requests().size(), 1);

    server.replyPendingStatus();
    QTRY_COMPARE_WITH_TIMEOUT(server.requests().size(), 2, 1000);
    QCOMPARE(server.requests().last().action, QStringLiteral("telemetry.push"));

    client.stop();
}

QTEST_GUILESS_MAIN(SimulatorClientTest)
#include "tst_simulatorclient.moc"
