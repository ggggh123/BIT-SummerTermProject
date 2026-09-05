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
    bool listen()
    {
        connect(&server_, &QTcpServer::newConnection, this, &FakeServer::onNewConnection);
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return server_.serverPort(); }
    QList<RequestEnvelope> requests() const { return requests_; }

private slots:
    void onNewConnection()
    {
        conn_ = server_.nextPendingConnection();
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
                replyStatus();
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
        resp.ok = true;
        resp.code = QStringLiteral("OK");
        resp.message = QStringLiteral("ok");

        QJsonObject charger;
        charger[QStringLiteral("chargerId")] = 1001;
        charger[QStringLiteral("stationId")] = 1;
        charger[QStringLiteral("code")] = QStringLiteral("1001");
        charger[QStringLiteral("type")] = QStringLiteral("fast");
        charger[QStringLiteral("powerKw")] = 60.0;
        charger[QStringLiteral("status")] = QStringLiteral("idle");
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

    QTcpServer server_;
    QTcpSocket *conn_ = nullptr;
    FrameDecoder decoder_;
    QList<RequestEnvelope> requests_;
    QString pendingStatusId_;
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
    s.recordedAt = QDateTime::fromMSecsSinceEpoch(1234567890000LL,
                                                  Qt::OffsetFromUTC, 8 * 3600);
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

    client.stop();
}

QTEST_GUILESS_MAIN(SimulatorClientTest)
#include "tst_simulatorclient.moc"
