#pragma once

#include <QString>
#include <QtGlobal>

class QCoreApplication;

namespace ev::simulator {

struct SimulatorConfig
{
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 9100;
    int intervalMs = 3000;
    quint32 seed = 20260901;
    QString token;
    QString startTime = QStringLiteral("2026-09-01T09:00:00+08:00");
    int maxQueueSamples = 200;
};

// 从命令行解析 --host/--port/--seed/--interval-ms/--token 等参数。
SimulatorConfig configFromCommandLine(const QCoreApplication &app);

} // namespace ev::simulator

