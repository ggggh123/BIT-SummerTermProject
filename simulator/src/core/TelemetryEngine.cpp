#include "core/TelemetryEngine.h"

namespace ev::simulator {

TelemetryEngine::TelemetryEngine(quint32 seed, const QDateTime &initialTime,
                                 int intervalMs)
    : seed_(seed),
      rng_(QRandomGenerator(seed)),
      currentTime_(initialTime),
      lastEventAt_(initialTime),
      intervalMs_(intervalMs)
{}

void TelemetryEngine::replaceChargers(const QList<ChargerSnapshot> &chargers)
{
    chargers_.clear();
    for (const ChargerSnapshot &c : chargers)
        chargers_.insert(c.chargerId, c);
}

QList<ChargerSnapshot> TelemetryEngine::chargers() const
{
    return chargers_.values();
}

QDateTime TelemetryEngine::currentTime() const
{
    return currentTime_;
}

QDateTime TelemetryEngine::nextEventTime(const QDateTime &base)
{
    QDateTime t = base;
    if (t <= lastEventAt_)
        t = lastEventAt_.addMSecs(1);
    lastEventAt_ = t;
    return t;
}

QList<TelemetrySample> TelemetryEngine::tick()
{
    currentTime_ = nextEventTime(currentTime_.addMSecs(intervalMs_));
    QList<TelemetrySample> samples;
    samples.reserve(chargers_.size());

    for (auto it = chargers_.cbegin(); it != chargers_.cend(); ++it) {
        const ChargerSnapshot &c = it.value();
        TelemetrySample s;
        s.chargerId = c.chargerId;
        s.recordedAt = currentTime_;
        s.status = c.status;

        if (c.status == QLatin1String("charging")) {
            // Deterministic positive increment: power * elapsed time * jitter.
            const double elapsedHours = intervalMs_ / 3600000.0;
            const double factor = 0.9 + rng_.generateDouble() * 0.2;
            s.powerKw = c.powerKw * factor;
            s.energyIncrementKwh = s.powerKw * elapsedHours;
        } else {
            s.powerKw = 0.0;
            s.energyIncrementKwh = 0.0;
        }
        samples.append(s);
    }
    return samples;
}

QList<FaultIntent> TelemetryEngine::takePendingIntents()
{
    QList<FaultIntent> out = pendingIntents_;
    pendingIntents_.clear();
    return out;
}

bool TelemetryEngine::requestFault(int chargerId)
{
    auto it = chargers_.find(chargerId);
    if (it == chargers_.end())
        return false;
    const QString &status = it.value().status;
    if (status != QLatin1String("idle")
        && status != QLatin1String("reserved")
        && status != QLatin1String("charging"))
        return false;

    it.value().status = QStringLiteral("fault");
    FaultIntent intent;
    intent.chargerId = chargerId;
    intent.fault = true;
    intent.recordedAt = nextEventTime(currentTime_);
    pendingIntents_.append(intent);
    return true;
}

bool TelemetryEngine::requestRecovery(int chargerId)
{
    auto it = chargers_.find(chargerId);
    if (it == chargers_.end())
        return false;
    if (it.value().status != QLatin1String("fault"))
        return false;

    FaultIntent intent;
    intent.chargerId = chargerId;
    intent.fault = false;
    intent.recordedAt = nextEventTime(currentTime_);
    pendingIntents_.append(intent);
    return true;
}

} // namespace ev::simulator
