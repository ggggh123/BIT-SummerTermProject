#pragma once

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>

namespace ev::test {
// 确定性的离线测试数据，不参与真实客户端或服务器业务数据。
inline QJsonObject usageStatistics(qint64 userId = 42)
{
    const double values[]{2.5, 0, 4.25, 8, 3, 0, 1.25};
    QJsonArray days;
    for (int i = 0; i < 7; ++i) days.append(QJsonObject{
        {"date", QDate(2026, 9, 2).addDays(i).toString(Qt::ISODate)}, {"energyKwh", values[i]}});
    return {{"userId", userId}, {"asOf", "2026-09-08T18:26:20+08:00"}, {"orderCount", 5},
        {"completedCount", 4}, {"energyKwh", 42.375}, {"paidFen", 3200}, {"durationSec", 12345},
        {"pendingSettlementCount", 1}, {"pendingSettlementFen", 165}, {"monthEnergyKwh", 19.0}, {"days", days}};
}
}
