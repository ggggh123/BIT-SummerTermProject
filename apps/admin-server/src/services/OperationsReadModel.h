#pragma once
#include "core/BusinessTime.h"
#include "core/Result.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

// 管理窗口的只读视图，不新增公开协议或改变设备／订单状态。
namespace OperationsReadModel {
// 运营页快照：站点表 + 桩表（联查充电次数/累计时长） + 最新一条遥测，一次查完打包 JSON。
inline Result snapshot(QSqlDatabase db, QJsonObject *out)
{
    QSqlQuery query(db);
    QJsonArray stations, chargers;
    if (!query.exec("SELECT id,name,address FROM stations ORDER BY id"))
        return Result::failure("DB_ERROR",query.lastError().text());
    while (query.next()) stations.append(QJsonObject{{"id",query.value(0).toInt()},
        {"name",query.value(1).toString()},{"address",query.value(2).toString()}});
    if (!query.exec("SELECT c.id,c.code,c.station_id,s.name,c.type,c.power_kw,c.status,c.charge_count,c.total_duration_sec "
                    "FROM chargers c JOIN stations s ON s.id=c.station_id ORDER BY c.station_id,c.id"))
        return Result::failure("DB_ERROR",query.lastError().text());
    while (query.next()) chargers.append(QJsonObject{{"id",query.value(0).toInt()},
        {"code",query.value(1).toString()},{"stationId",query.value(2).toInt()},
        {"stationName",query.value(3).toString()},{"type",query.value(4).toString()},
        {"ratedPowerKw",query.value(5).toDouble()},{"status",query.value(6).toString()},
        {"chargeCount",query.value(7).toLongLong()},{"totalDurationSec",query.value(8).toLongLong()}});
    if (!query.exec("SELECT charger_id,recorded_at,power_kw FROM telemetry WHERE event_type='telemetry' ORDER BY id DESC LIMIT 1"))
        return Result::failure("DB_ERROR",query.lastError().text());
    QJsonValue latest(QJsonValue::Null);
    if (query.next()) latest=QJsonObject{{"chargerId",query.value(0).toInt()},
        {"recordedAt",query.value(1).toString()},{"powerKw",query.value(2).toDouble()}};
    *out={{"stations",stations},{"chargers",chargers},{"latestTelemetry",latest},{"readAt",BusinessTime::now()}};
    return Result::success();
}
}
