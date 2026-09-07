#include <QtTest>

#include <QLabel>

#include "core/TelemetryEngine.h"
#include "net/SimulatorClient.h"
#include "ui/SimulatorWindow.h"

using namespace ev::simulator;

class FakeClient : public ISimulatorClient
{
    Q_OBJECT
public:
    using ISimulatorClient::ISimulatorClient;

    void start() override {}
    void stop() override {}
    bool isConnected() const override { return false; }
    void refresh() override {}
    void sendTelemetry(const QList<TelemetrySample> &samples) override
    {
        for (const TelemetrySample &s : samples)
            emit logMessage(QStringLiteral("09:00:03  charger %1  telemetry.push  OK")
                                .arg(s.chargerId));
    }
    void sendFault(const FaultIntent &intent) override
    {
        emit logMessage(QStringLiteral("09:00:03  charger %1  simulator.fault_set  OK")
                            .arg(intent.chargerId));
    }

    void setRunning(bool running) override { lastRunning = running; }

    bool lastRunning = false;
};

class SimulatorWindowTest : public QObject
{
    Q_OBJECT
private slots:
    void runPauseTickAndLog();
    void faultDisabledWithoutSelection();
    void distinguishesTransportFromAuthenticatedSession();
};

static QDateTime t0()
{
    return QDateTime::fromString(QStringLiteral("2026-09-01T09:00:00+08:00"),
                                 Qt::ISODate);
}

void SimulatorWindowTest::runPauseTickAndLog()
{
    TelemetryEngine engine(20260901, t0(), 3000);
    ChargerSnapshot c;
    c.chargerId = 1001;
    c.status = QStringLiteral("idle");
    c.powerKw = 60.0;
    engine.replaceChargers({c});

    FakeClient client;
    SimulatorWindow window(&client, &engine);

    QCOMPARE(window.runButtonText(), QStringLiteral("Run"));
    QCOMPARE(window.tickCount(), 0);
    QVERIFY(!client.lastRunning);  // R13: panel starts paused

    window.toggleRun();
    QCOMPARE(window.runButtonText(), QStringLiteral("Pause"));
    QVERIFY(client.lastRunning);   // R13: run state propagated to the client

    window.doTick();
    window.doTick();
    QVERIFY(window.tickCount() >= 2);
    QVERIFY(window.logLines().join(QLatin1Char('\n'))
                .contains(QStringLiteral("1001")));

    window.toggleRun();
    QCOMPARE(window.runButtonText(), QStringLiteral("Run"));
    QVERIFY(!client.lastRunning);  // R13: pause propagated to the client
}

void SimulatorWindowTest::faultDisabledWithoutSelection()
{
    TelemetryEngine engine(20260901, t0(), 3000);
    FakeClient client;
    SimulatorWindow window(&client, &engine);

    QVERIFY(!window.faultEnabled());
    QVERIFY(!window.recoverEnabled());
}

void SimulatorWindowTest::distinguishesTransportFromAuthenticatedSession()
{
    TelemetryEngine engine(20260901, t0(), 3000);
    FakeClient client;
    SimulatorWindow window(&client, &engine);

    auto hasBadge = [&window](const QString &text) {
        const QList<QLabel *> labels = window.findChildren<QLabel *>();
        for (const QLabel *label : labels) {
            if (label->text() == text)
                return true;
        }
        return false;
    };

    emit client.connected();
    QVERIFY(hasBadge(QStringLiteral("等待鉴权")));

    emit client.authenticationFailed(QStringLiteral("AUTH_REQUIRED"));
    QVERIFY(hasBadge(QStringLiteral("鉴权失败")));

    emit client.sessionReady();
    QVERIFY(hasBadge(QStringLiteral("已接入")));
}

QTEST_MAIN(SimulatorWindowTest)
#include "tst_simulatorwindow.moc"
