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
        ev::simulator::configFromCommandLine(app);
    const bool useRealtimeClock = config.startTime.trimmed().isEmpty();
    const QDateTime initialTime = ev::simulator::resolvedStartTime(
        config, QDateTime::currentDateTimeUtc());
    ev::simulator::TelemetryEngine::Clock wallClock;
    if (useRealtimeClock) {
        wallClock = []() {
            return QDateTime::currentDateTimeUtc().toOffsetFromUtc(8 * 3600);
        };
    }

    auto *engine = new ev::simulator::TelemetryEngine(
        config.seed, initialTime, config.intervalMs, wallClock);

    auto *client = new ev::simulator::SimulatorClient(config, engine);
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

    auto *window = new ev::simulator::SimulatorWindow(client, engine);
    window->show();

    client->start();

    const int exitCode = app.exec();
    return statusWriteFailed ? EXIT_FAILURE : exitCode;
}
