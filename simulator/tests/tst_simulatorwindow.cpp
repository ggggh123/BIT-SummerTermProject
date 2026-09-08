#include <QtTest>

#include <QLabel>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>

#include "core/TelemetryEngine.h"
#include "net/SimulatorClient.h"
#include "ui/SimulatorWindow.h"
#include "ui/PulseChart.h"

using namespace ev::simulator;

class FakeClient : public ISimulatorClient
{
    Q_OBJECT
public:
    using ISimulatorClient::ISimulatorClient;

    void start() override {}
    void stop() override {}
    bool isConnected() const override { return false; }
    void refresh() override { ++refreshes; }
    void sendTelemetry(const QList<TelemetrySample> &samples) override
    {
        latest = samples;
        for (const TelemetrySample &s : samples)
            emit logMessage(QStringLiteral("response telemetry-%1-%2 OK")
                .arg(s.chargerId).arg(s.recordedAt.toMSecsSinceEpoch()));
    }
    void sendFault(const FaultIntent &intent) override
    {
        faults.append(intent);
        emit logMessage(QStringLiteral("09:00:03  charger %1  simulator.fault_set  OK")
                            .arg(intent.chargerId));
    }

    void setRunning(bool running) override { lastRunning = running; }

    bool lastRunning = false;
    int refreshes = 0;
    QList<TelemetrySample> latest;
    QList<FaultIntent> faults;
};

class SimulatorWindowTest : public QObject
{
    Q_OBJECT
private slots:
    void runPauseTickAndLog();
    void faultDisabledWithoutSelection();
    void distinguishesTransportFromAuthenticatedSession();
    void actualSamplesSelectionAndFaultControls();
    void selectedIdentitySurvivesSnapshotRefresh();
    void boundedHistoryLogsAndPinnedCursor();
    void emptyStateAndDesktopCaptures();
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

    QCOMPARE(window.runButtonText(), QStringLiteral("启动模拟"));
    QCOMPARE(window.tickCount(), 0);
    QVERIFY(!client.lastRunning);  // R13: panel starts paused

    window.toggleRun();
    QCOMPARE(window.runButtonText(), QStringLiteral("暂停模拟"));
    QVERIFY(client.lastRunning);   // R13: run state propagated to the client

    window.doTick();
    window.doTick();
    QVERIFY(window.tickCount() >= 2);
    QVERIFY(window.logLines().join(QLatin1Char('\n'))
                .contains(QStringLiteral("1001")));

    window.toggleRun();
    QCOMPARE(window.runButtonText(), QStringLiteral("启动模拟"));
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

static void capture(QWidget &widget, const QString &name)
{
    const QString directory = qEnvironmentVariable("EV_SIM_UI_CAPTURE_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QCoreApplication::processEvents();
    QVERIFY(widget.grab().save(QDir(directory).filePath(name + QStringLiteral(".png"))));
}

void SimulatorWindowTest::actualSamplesSelectionAndFaultControls()
{
    TelemetryEngine engine(20260908, t0(), 3000);
    engine.replaceChargers({{1001, QStringLiteral("charging"), 60},
                            {1002, QStringLiteral("idle"), 30}});
    FakeClient client;
    SimulatorWindow window(&client, &engine);
    auto *table = window.findChild<QTableWidget *>();
    auto *chart = window.findChild<ev::ui::PulseChart *>();
    auto *power = window.findChild<QLabel *>(QStringLiteral("simPowerValue"));
    QCOMPARE(power->text(), QStringLiteral("—"));
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("60"));
    QCOMPARE(table->item(0, 3)->text(), QStringLiteral("—"));
    window.toggleRun();
    window.doTick();
    const double generated = client.latest.at(0).powerKw;
    QVERIFY(generated > 54 && generated < 66);
    QCOMPARE(chart->reading(0)->kw, generated);
    QCOMPARE(power->text(), QString::number(generated, 'f', 1));
    QCOMPARE(table->item(0, 3)->text(), power->text());
    QCOMPARE(table->item(1, 3)->text(), QStringLiteral("0.0"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("simSampleCount"))->text(), QStringLiteral("2"));

    table->selectRow(0);
    QVERIFY(window.faultEnabled());
    QVERIFY(!window.recoverEnabled());
    window.injectFault();
    QCOMPARE(client.faults.size(), 1);
    QVERIFY(client.faults.first().fault);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("故障"));
    QVERIFY(!window.faultEnabled());
    QVERIFY(window.recoverEnabled());
    window.injectRecovery();
    QCOMPARE(client.faults.size(), 2);
    QVERIFY(!client.faults.last().fault);
    QCOMPARE(engine.chargers().first().status, QStringLiteral("fault"));
    window.doTick();
    QCOMPARE(table->item(0, 3)->text(), QStringLiteral("0.0"));
    window.prepareReset();
    const int ticks = window.tickCount();
    window.doTick();
    QCOMPARE(window.tickCount(), ticks);
    QVERIFY(!client.lastRunning);
    QCOMPARE(window.runButtonText(), QStringLiteral("启动模拟"));
    QVERIFY(window.logLines().last().contains(QStringLiteral("不会修改数据库")));
    window.findChild<QPushButton *>(QStringLiteral("simRefreshButton"))->click();
    QCOMPARE(client.refreshes, 1);
    emit client.disconnected();
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("simSessionBadge"))->text(), QStringLiteral("未连接"));
    QVERIFY(chart->sampleCount() > 0); // 历史样本仍可回看，不伪装成服务端确认。
}

void SimulatorWindowTest::selectedIdentitySurvivesSnapshotRefresh()
{
    TelemetryEngine engine(1, t0(), 3000);
    engine.replaceChargers({{1001, QStringLiteral("idle"), 60},
                            {1002, QStringLiteral("fault"), 30}});
    FakeClient client;
    SimulatorWindow window(&client, &engine);
    auto *table = window.findChild<QTableWidget *>();
    table->selectRow(1);
    engine.replaceChargers({{999, QStringLiteral("idle"), 7},
                            {1001, QStringLiteral("idle"), 60},
                            {1002, QStringLiteral("fault"), 30}});
    emit client.chargersReceived(engine.chargers());
    QCOMPARE(table->currentRow(), 2);
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("simSelectedCharger"))->text(), QStringLiteral("1002"));
    QVERIFY(window.recoverEnabled());
    engine.replaceChargers({{1001, QStringLiteral("idle"), 60}});
    emit client.chargersReceived(engine.chargers());
    QVERIFY(!window.faultEnabled());
    QVERIFY(!window.recoverEnabled());
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("simSelectedCharger"))->text(), QStringLiteral("未选择"));
}

void SimulatorWindowTest::boundedHistoryLogsAndPinnedCursor()
{
    TelemetryEngine engine(7, t0(), 3000);
    engine.replaceChargers({{1001, QStringLiteral("charging"), 60}});
    FakeClient client;
    SimulatorWindow window(&client, &engine);
    auto *chart = window.findChild<ev::ui::PulseChart *>();
    window.toggleRun();
    for (int i = 0; i < 600; ++i) window.doTick();
    QCOMPARE(chart->sampleCount(), 600);
    chart->setCursorSeconds(300);
    const double read = chart->reading(0)->kw;
    window.doTick();
    QCOMPARE(chart->sampleCount(), 600);
    QCOMPARE(chart->cursorSeconds(), 297.0); // 窗口滚动仍锁定原采样时刻。
    QCOMPARE(chart->reading(0)->kw, read);
    window.prepareReset();
    for (int i = 0; i < 700; ++i) emit client.logMessage(QStringLiteral("test %1").arg(i));
    QCOMPARE(window.logLines().size(), 500);
    QCOMPARE(window.logLines().first(), QStringLiteral("test 200"));
    auto *logs = window.findChild<QListWidget *>();
    QCOMPARE(logs->count(), 500);
    QCOMPARE(logs->item(0)->toolTip(), QStringLiteral("test 699"));
}

void SimulatorWindowTest::emptyStateAndDesktopCaptures()
{
    TelemetryEngine engine(20260908, t0(), 3000);
    FakeClient client;
    SimulatorWindow window(&client, &engine);
    window.show();
    QTest::qWait(30);
    QCOMPARE(window.size(), QSize(1360, 860));
    auto *chart = window.findChild<ev::ui::PulseChart *>();
    auto *power = window.findChild<QLabel *>(QStringLiteral("simPowerValue"));
    capture(window, QStringLiteral("simulator-empty-1360x860"));
    window.toggleRun();
    window.doTick();
    QCOMPARE(chart->sampleCount(), 0);
    QCOMPARE(power->text(), QStringLiteral("—"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("simSampleCount"))->text(), QStringLiteral("0"));
    // 截图样本只由测试固定种子的真实 engine 生成，不向生产代码塞入波形。
    QList<ChargerSnapshot> fleet;
    for (int i = 0; i < 16; ++i)
        fleet.append({1001 + i, i == 3 ? QStringLiteral("fault") : i == 2 ? QStringLiteral("reserved") : QStringLiteral("idle"), i < 4 ? 60.0 : 30.0});
    engine.replaceChargers(fleet);
    emit client.chargersReceived(fleet);
    emit client.sessionReady();
    for (int i = 0; i < 85; ++i) {
        if (i == 4) { fleet[0].status = QStringLiteral("charging"); engine.replaceChargers(fleet); }
        if (i == 25) { fleet[5].status = QStringLiteral("charging"); engine.replaceChargers(fleet); }
        window.doTick();
    }
    window.findChild<QTimer *>()->stop(); // 保留运行外观，避免截屏期间时钟推进。
    QCoreApplication::processEvents();
    auto *viewport = window.findChild<QScrollArea *>();
    QCOMPARE(viewport->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(viewport->verticalScrollBar()->maximum(), 0);
    capture(window, QStringLiteral("simulator-running-1360x860"));
    auto *table = window.findChild<QTableWidget *>();
    table->selectRow(0);
    QCOMPARE(chart->reading(0)->kw, client.latest.at(0).powerKw);
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("simChartScope"))->text().contains(QStringLiteral("1001")));
    capture(window, QStringLiteral("simulator-selected-1360x860"));
    window.findChild<QPushButton *>(QStringLiteral("simAllDevicesButton"))->click();
    double sum = 0;
    for (const auto &sample : client.latest) sum += sample.powerKw;
    QCOMPARE(chart->reading(0)->kw, sum);
    QVERIFY(!window.faultEnabled());
    chart->setCursorSeconds(60);
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("simCursorLabel"))->text().contains(QStringLiteral("历史回看")));
    window.findChild<QPushButton *>(QStringLiteral("simLatestButton"))->click();
    QVERIFY(chart->followingLatest());
    window.toggleRun();
    table->selectRow(3);
    QVERIFY(window.recoverEnabled());
    capture(window, QStringLiteral("simulator-fault-1360x860"));
    window.resize(1280, 720);
    QCoreApplication::processEvents();
    QCOMPARE(window.size(), QSize(1280, 720));
    for (const char *name : {"simRunButton", "simFaultButton", "simRecoverButton", "simResetButton"}) {
        auto *button = window.findChild<QPushButton *>(QLatin1String(name));
        const QRect bounds(button->mapTo(&window, QPoint()), button->size());
        QVERIFY2(window.rect().contains(bounds), name);
        QVERIFY(button->height() >= 36);
    }
    QCOMPARE(viewport->horizontalScrollBar()->maximum(), 0);
    capture(window, QStringLiteral("simulator-paused-1280x720"));
    viewport->verticalScrollBar()->setValue(viewport->verticalScrollBar()->maximum());
    capture(window, QStringLiteral("simulator-compact-bottom-1280x720"));
    emit client.authenticationFailed(QStringLiteral("AUTH_REQUIRED"));
    capture(window, QStringLiteral("simulator-auth-error-1280x720"));
}

QTEST_MAIN(SimulatorWindowTest)
#include "tst_simulatorwindow.moc"
