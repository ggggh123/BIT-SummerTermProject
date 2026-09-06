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

// Pure, deterministic in-memory state machine. Never touches SQLite.
class TelemetryEngine
{
public:
    using Clock = std::function<QDateTime()>;

    TelemetryEngine(quint32 seed, const QDateTime &initialTime, int intervalMs,
                    Clock wallClock = {});

    void replaceChargers(const QList<ChargerSnapshot> &chargers);
    QList<ChargerSnapshot> chargers() const;

    // Advance simulated time by intervalMs and produce one sample per charger.
    QList<TelemetrySample> tick();

    // Drain fault/recovery intents queued since the last call.
    QList<FaultIntent> takePendingIntents();

    bool requestFault(int chargerId);
    bool requestRecovery(int chargerId);

    QDateTime currentTime() const;
    int intervalMs() const { return intervalMs_; }

private:
    // R14: the v1 contract requires recordedAt to increase strictly across
    // telemetry and fault events per charger, so every event timestamp is
    // allocated from this monotonic clock instead of reusing currentTime_.
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
