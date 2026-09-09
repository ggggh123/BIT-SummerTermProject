#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QRandomGenerator>
#include <QString>

#include <functional>

namespace ev::simulator {

struct ChargerSnapshot
{
    int chargerId = 0;
    QString status = QStringLiteral("idle");
    double powerKw = 0.0;
};

struct TelemetrySample
{
    int chargerId = 0;
    QDateTime recordedAt;
    double powerKw = 0.0;
    double energyIncrementKwh = 0.0;
    QString status = QStringLiteral("idle");
};

struct FaultIntent
{
    int chargerId = 0;
    bool fault = false;
    QDateTime recordedAt;
};

// 纯内存确定性状态机：固定 seed 产出相同遥测序列，只生成数据、不碰 SQLite、
// 不算钱——订单金额等权威状态一律由服务端决定。
class TelemetryEngine
{
public:
    using Clock = std::function<QDateTime()>;

    TelemetryEngine(quint32 seed, const QDateTime &initialTime, int intervalMs,
                    Clock wallClock = {});

    void replaceChargers(const QList<ChargerSnapshot> &chargers);
    QList<ChargerSnapshot> chargers() const;

    // 推进 intervalMs 的模拟时间，并为每台桩生成一条采样。
    QList<TelemetrySample> tick();

    // 取出上次调用以来排队的故障/恢复事件。
    QList<FaultIntent> takePendingIntents();

    bool requestFault(int chargerId);
    bool requestRecovery(int chargerId);

    QDateTime currentTime() const;
    int intervalMs() const { return intervalMs_; }

private:
    // R14：v1 契约要求每台桩的遥测与故障事件 recordedAt 严格递增，
    // 因此所有事件时间戳都由该单调时钟分配，而不是直接复用 currentTime_。
    QDateTime nextEventTime(const QDateTime &base);

    quint32 seed_;
    QRandomGenerator rng_;
    QDateTime currentTime_;
    QDateTime lastEventAt_;
    int intervalMs_;
    Clock wallClock_;
    QMap<int, ChargerSnapshot> chargers_;
    QList<FaultIntent> pendingIntents_;
};

} // namespace ev::simulator

Q_DECLARE_METATYPE(ev::simulator::ChargerSnapshot)
Q_DECLARE_METATYPE(ev::simulator::TelemetrySample)
Q_DECLARE_METATYPE(ev::simulator::FaultIntent)
