#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QRandomGenerator>
#include <QString>

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
    TelemetryEngine(quint32 seed, const QDateTime &initialTime, int intervalMs);

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
    quint32 seed_;
    QRandomGenerator rng_;
    QDateTime currentTime_;
    int intervalMs_;
    QMap<int, ChargerSnapshot> chargers_;
    QList<FaultIntent> pendingIntents_;
};

} // namespace ev::simulator

Q_DECLARE_METATYPE(ev::simulator::ChargerSnapshot)
Q_DECLARE_METATYPE(ev::simulator::TelemetrySample)
Q_DECLARE_METATYPE(ev::simulator::FaultIntent)
