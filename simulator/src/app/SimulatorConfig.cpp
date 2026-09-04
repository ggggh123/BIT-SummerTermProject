#include "app/SimulatorConfig.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

namespace ev::simulator {

SimulatorConfig configFromCommandLine(const QCoreApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("EV charger simulator"));
    parser.addHelpOption();

    const QCommandLineOption hostOpt(
        QStringList() << QStringLiteral("host"),
        QStringLiteral("Server host."), QStringLiteral("host"),
        QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOpt(
        QStringList() << QStringLiteral("port"),
        QStringLiteral("Server port."), QStringLiteral("port"),
        QStringLiteral("9100"));
    const QCommandLineOption seedOpt(
        QStringList() << QStringLiteral("seed"),
        QStringLiteral("Random seed."), QStringLiteral("seed"),
        QStringLiteral("20260901"));
    const QCommandLineOption intervalOpt(
        QStringList() << QStringLiteral("interval-ms"),
        QStringLiteral("Telemetry interval in milliseconds."),
        QStringLiteral("ms"), QStringLiteral("3000"));
    const QCommandLineOption tokenOpt(
        QStringList() << QStringLiteral("token"),
        QStringLiteral("Simulator service token."), QStringLiteral("token"),
        QString());

    parser.addOption(hostOpt);
    parser.addOption(portOpt);
    parser.addOption(seedOpt);
    parser.addOption(intervalOpt);
    parser.addOption(tokenOpt);
    parser.process(app);

    SimulatorConfig config;
    config.host = parser.value(hostOpt);
    config.port = static_cast<quint16>(parser.value(portOpt).toUShort());
    config.seed = parser.value(seedOpt).toUInt();
    config.intervalMs = parser.value(intervalOpt).toInt();
    config.token = parser.value(tokenOpt);
    return config;
}

} // namespace ev::simulator
