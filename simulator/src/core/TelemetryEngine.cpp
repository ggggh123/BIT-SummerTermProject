#include "core/TelemetryEngine.h"

#include <utility>

namespace ev::simulator {

TelemetryEngine::TelemetryEngine(quint32 seed, const QDateTime &initialTime,
                                 int intervalMs, Clock wallClock)
    : seed_(seed),
      rng_(QRandomGenerator(seed)),
      currentTime_(initialTime),
      lastEventAt_(initialTime),
      intervalMs_(intervalMs),
      wallClock_(std::move(wallClock))
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
    if (wallClock_) {
        const QDateTime wallNow = wallClock_();
        if (wallNow.isValid() && wallNow > t)
            t = wallNow;
    }
    if (t <= lastEventAt_)
        t = lastEventAt_.addMSecs(1);
    lastEventAt_ = t;
    return t;
}

// 每个采样/故障事件的时间戳都从单调时钟分配：严格递增，防止事件乱序。
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
            // 确定性正增量：额定功率 × 采样时长 × 0.9~1.1 随机抖动（seed 决定，可复现）。
            const double elapsedHours = intervalMs_ / 3600000.0;
            const double factor = 0.9 + rng_.generateDouble() * 0.2;
            s.powerKw = c.powerKw * factor;
            s.energyIncrementKwh = s.powerKw * elapsedHours;
        } else {
            s.powerKw = 0.0;             // 非充电设备真实 0 功率
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

// 故障注入：仅 idle/reserved/charging 可故障；先改本地状态、排队事件，等客户端发送、
// 服务端回执确认后 UI 才显示权威状态。
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
    currentTime_ = intent.recordedAt;
    pendingIntents_.append(intent);
    return true;
}

// 请求恢复：仅 fault 态可恢复；恢复事件仍走协议上报，不擅自把本地改成 idle。
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
    currentTime_ = intent.recordedAt;
    pendingIntents_.append(intent);
    return true;
}

} // namespace ev::simulator
