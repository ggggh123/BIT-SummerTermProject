#pragma once

#include <QDateTime>
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
    // 空值表示生产启动锚定当前 +08:00；测试可显式传入固定时间。
    QString startTime;
    int maxQueueSamples = 200;
};

// 从命令行解析 --host/--port/--seed/--interval-ms/--token 等参数。
SimulatorConfig configFromCommandLine(const QCoreApplication &app);

// 把空的生产默认值解析为当前 +08:00，显式时间则原样解析以保持确定性。
QDateTime resolvedStartTime(const SimulatorConfig &config, const QDateTime &now);

} // namespace ev::simulator
