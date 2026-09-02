#pragma once

#include <QString>
#include <QtGlobal>

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

} // namespace ev::simulator
