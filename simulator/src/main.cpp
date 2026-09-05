#include <QApplication>
#include <QDateTime>

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
    auto *window = new ev::simulator::SimulatorWindow(client, engine);
    window->show();

    client->start();

    return app.exec();
}
