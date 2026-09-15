#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

// 仅用于视觉回归：与已选 HTML 设计稿相同的 17 个样本，绝不进入生产数据路径。
namespace PulseFixture {
inline QVector<double> powers(bool second = false)
{
    if (second) return {0,0,12,19,22,24,25,24,24,23,22,22,21,20,20,19,18};
    QVector<double> values{0,14,28,35,37,38,36,34,35,34,32,31,30,30,28,26,24};
    double integral = 0;
    for (int i = 1; i < values.size(); ++i) integral += (values[i-1]+values[i])/2*100/3600;
    for (auto &value : values) value *= 12.480/integral;
    return values;
}
inline QJsonArray samples(const QDateTime &start, bool second = false, bool withEnergy = true)
{
    const auto values = powers(second);
    QJsonArray samples;
    double energy = 0;
    for (int i = 0; i < values.size(); ++i) {
        if (i > 0) energy += (values[i-1]+values[i])/2*100/3600;
        QJsonObject sample{{"recordedAt",start.addMSecs(i == 0 ? 1 : i*100000).toOffsetFromUtc(8*3600).toString(Qt::ISODateWithMs)},
                           {"powerKw",values[i]}};
        if (withEnergy) sample.insert("energyKwh",energy);
        samples.append(sample);
    }
    return samples;
}
inline QJsonObject order(const QDateTime &start)
{
    return {{"orderId",1028},{"userId",42},{"chargerId",1001},{"stationId",1},
        {"stationName",QStringLiteral("中关村示例充电站")},{"chargerCode","A-01"},
        {"status","charging"},{"reservedAt",start.addSecs(-120).toOffsetFromUtc(8*3600).toString(Qt::ISODateWithMs)},
        {"startedAt",start.toOffsetFromUtc(8*3600).toString(Qt::ISODateWithMs)},{"endedAt",QJsonValue(QJsonValue::Null)},
        {"energyKwh",12.480},{"amountFen",1685},{"elapsedSec",1600}};
}
inline QJsonObject telemetry(const QDateTime &start)
{
    return {{"orderId",1028},{"chargerId",1001},{"ratedPowerKw",60},
        {"startedAt",start.toOffsetFromUtc(8*3600).toString(Qt::ISODateWithMs)},{"samples",samples(start)},{"truncated",false}};
}
inline QJsonObject station(const QDateTime &start)
{
    QJsonArray devices;
    const QStringList states{"charging","idle","reserved","fault","idle","charging","reserved","reserved"};
    for (int i = 0; i < 8; ++i) {
        devices.append(QJsonObject{{"chargerId",1001+i},{"code",QString("A-%1").arg(i+1,2,10,QLatin1Char('0'))},
            {"type","fast"},{"ratedPowerKw",60},{"status",states[i]},
            {"samples",i==0 || i==5 ? samples(start,i==5,false) : QJsonArray()}});
    }
    return {{"stationId",1},{"stationName",QStringLiteral("中关村示例充电站")},
        {"stations",QJsonArray{QJsonObject{{"stationId",1},{"name",QStringLiteral("中关村示例充电站")}}}},
        {"latestAt",start.addSecs(1600).toOffsetFromUtc(8*3600).toString(Qt::ISODateWithMs)},{"chargers",devices}};
}
}
