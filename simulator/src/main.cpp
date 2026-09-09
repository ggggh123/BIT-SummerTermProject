#include <QApplication>
#include <QDateTime>
#include <QDebug>

#include <cstdlib>

#include "app/RuntimeStatusWriter.h"
#include "app/SimulatorConfig.h"
#include "core/TelemetryEngine.h"
#include "net/SimulatorClient.h"
#include "ui/SimulatorWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ev_charger_simulator"));

    const ev::simulator::SimulatorConfig config =
        ev::simulator::configFromCommandLine(app);   // 解析 --host/--port/--seed/--token 等
    const bool useRealtimeClock = config.startTime.trimmed().isEmpty();
    const QDateTime initialTime = ev::simulator::resolvedStartTime(
        config, QDateTime::currentDateTimeUtc());
    ev::simulator::TelemetryEngine::Clock wallClock;
    if (useRealtimeClock) {
        wallClock = []() {   // 生产启动锚定实时 +08:00；测试传固定时间保持确定性
            return QDateTime::currentDateTimeUtc().toOffsetFromUtc(8 * 3600);
        };
    }

    auto *engine = new ev::simulator::TelemetryEngine(
        config.seed, initialTime, config.intervalMs, wallClock);   // 遥测引擎（内存态）

    auto *client = new ev::simulator::SimulatorClient(config, engine);   // 网络客户端
    bool statusWriteFailed = false;
    ev::simulator::RuntimeStatusWriter *statusWriter = nullptr;
    const QString statusFile = qEnvironmentVariable("EV_SIMULATOR_STATUS_FILE");
    if (!statusFile.isEmpty()) {
        statusWriter = new ev::simulator::RuntimeStatusWriter(
            statusFile, client, &app);
        QObject::connect(
            statusWriter, &ev::simulator::RuntimeStatusWriter::writeFailed,
            &app, [&app, &statusWriteFailed](const QString &message) {
                statusWriteFailed = true;
                qCritical().noquote() << message;
                app.exit(EXIT_FAILURE);
            });

        QString error;
        if (!statusWriter->start(&error)) {
            qCritical().noquote() << error;
            return EXIT_FAILURE;
        }
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                         statusWriter,
                         [statusWriter, &statusWriteFailed]() {
            if (!statusWriteFailed)
                statusWriter->stop();
        });
    }

    auto *window = new ev::simulator::SimulatorWindow(client, engine);   // UI 只读 engine/client 信号
    window->show();

    client->start();   // 发起 TCP 连接，鉴权通过并取得权威快照后界面才显示"已接入"

    const int exitCode = app.exec();
    return statusWriteFailed ? EXIT_FAILURE : exitCode;
}
