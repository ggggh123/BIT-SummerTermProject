#pragma once

#include "core/BusinessTime.h"
#include "core/Result.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>

// 只读投影。沿用现有 telemetry 表，不给订单添加新的状态，也不生成模拟曲线。
// 能源监测页/用户端充电曲线的数据源：查库→打包 QJsonObject→界面只消费 JSON，不碰 SQL。
namespace EnergyReadModel {
// 单个订单的功率曲线：窗口函数现算累计电量，倒序截取最近 600 条采样。
inline Result order(QSqlDatabase db, qint64 orderId, qint64 userId, QJsonObject *out)
{
    QSqlQuery q(db);
    q.prepare("SELECT o.user_id,o.charger_id,o.started_at,o.ended_at,c.power_kw "
              "FROM orders o JOIN chargers c ON c.id=o.charger_id WHERE o.id=?");
    q.addBindValue(orderId);
    if (!q.exec()) return Result::failure("INTERNAL_ERROR",q.lastError().text());
    if (!q.next()) return Result::failure("ENTITY_NOT_FOUND",QStringLiteral("订单不存在"));
    if (userId > 0 && q.value(0).toLongLong()!=userId)
        return Result::failure("FORBIDDEN",QStringLiteral("不能读取其他用户的充电曲线"));
    const qint64 chargerId=q.value(1).toLongLong();
    const QString start=q.value(2).toString(), end=q.value(3).toString();
    const double rated=q.value(4).toDouble();
    q.finish();
    QJsonArray points;
    bool truncated=false;
    if (!start.isEmpty()) {
        // 累计量在截取最近 600 条之前计算；不以曲线积分重算账单。
        q.prepare("SELECT recorded_at,power_kw,energy,id FROM ("
                  "SELECT id,recorded_at,power_kw,"
                  "SUM(energy_increment_kwh) OVER (ORDER BY julianday(recorded_at),id ROWS UNBOUNDED PRECEDING) AS energy "
                  "FROM telemetry WHERE charger_id=? AND event_type='telemetry' "
                  "AND julianday(recorded_at)>julianday(?) "
                  "AND (?='' OR julianday(recorded_at)<=julianday(?))) "
                  "ORDER BY julianday(recorded_at) DESC,id DESC LIMIT 601");
        q.addBindValue(chargerId); q.addBindValue(start);
        q.addBindValue(end.isEmpty()?QStringLiteral(""):end);
        q.addBindValue(end.isEmpty()?QStringLiteral(""):end);
        if (!q.exec()) return Result::failure("INTERNAL_ERROR",q.lastError().text());
        QList<QJsonObject> reversed;
        while (q.next()) reversed.append({{"recordedAt",q.value(0).toString()},
            {"powerKw",q.value(1).toDouble()},{"energyKwh",q.value(2).toDouble()}});
        truncated=reversed.size()>600;
        if (truncated) reversed.removeLast();
        std::reverse(reversed.begin(),reversed.end());
        for (const auto &p:reversed) points.append(p);
    }
    *out={{"orderId",orderId},{"chargerId",chargerId},{"ratedPowerKw",rated},
          {"startedAt",start.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(start)},
          {"samples",points},{"truncated",truncated}};
    return Result::success();
}

// 站点曲线：取最近采样往回 30 分钟窗口，每根桩最多 600 条；同时返回最新采样时间供界面显示数据陈旧度。
inline Result station(QSqlDatabase db, qint64 stationId, QJsonObject *out)
{
    QSqlQuery q(db);
    if (stationId<=0) {
        if (!q.exec("SELECT id FROM stations ORDER BY id LIMIT 1"))
            return Result::failure("INTERNAL_ERROR",q.lastError().text());
        if (q.next()) stationId=q.value(0).toLongLong();
        q.finish();
    }
    q.prepare("SELECT id,name FROM stations ORDER BY id");
    if (!q.exec()) return Result::failure("INTERNAL_ERROR",q.lastError().text());
    QJsonArray stations;
    QString name;
    while (q.next()) {
        const qint64 id=q.value(0).toLongLong();
        stations.append(QJsonObject{{"stationId",id},{"name",q.value(1).toString()}});
        if (id==stationId) name=q.value(1).toString();
    }
    q.finish();
    // 站点曲线最多 30 分钟；以最新实际采样为右端点，同时返回采样时间以显示陈旧状态。
    q.prepare("SELECT t.recorded_at FROM telemetry t JOIN chargers c ON c.id=t.charger_id "
              "WHERE c.station_id=? AND t.event_type='telemetry' "
              "ORDER BY julianday(t.recorded_at) DESC,t.id DESC LIMIT 1");
    q.addBindValue(stationId);
    if (!q.exec()) return Result::failure("INTERNAL_ERROR",q.lastError().text());
    const QString latest=q.next()?q.value(0).toString():QString();
    const QDateTime last=QDateTime::fromString(latest,Qt::ISODateWithMs);
    const QString since=last.isValid()?last.addSecs(-1800).toString(Qt::ISODateWithMs):QString();
    q.finish();
    q.prepare("SELECT id,code,type,power_kw,status FROM chargers WHERE station_id=? ORDER BY id");
    q.addBindValue(stationId);
    if (!q.exec()) return Result::failure("INTERNAL_ERROR",q.lastError().text());
    QJsonArray chargers;
    while (q.next()) chargers.append(QJsonObject{{"chargerId",q.value(0).toLongLong()},
        {"code",q.value(1).toString()},{"type",q.value(2).toString()},
        {"ratedPowerKw",q.value(3).toDouble()},{"status",q.value(4).toString()}});
    q.finish();
    for (int i=0;i<chargers.size();++i) {
        auto c=chargers[i].toObject();
        q.prepare("SELECT recorded_at,power_kw FROM telemetry WHERE charger_id=? "
                  "AND event_type='telemetry' AND julianday(recorded_at)>=julianday(?) "
                  "ORDER BY julianday(recorded_at) DESC,id DESC LIMIT 600");
        q.addBindValue(c.value("chargerId").toInteger()); q.addBindValue(since);
        if (!q.exec()) return Result::failure("INTERNAL_ERROR",q.lastError().text());
        QList<QJsonObject> reversed;
        while(q.next()) reversed.append({{"recordedAt",q.value(0).toString()},
                                        {"powerKw",q.value(1).toDouble()}});
        std::reverse(reversed.begin(),reversed.end());
        QJsonArray samples;
        for (const auto &p:reversed) samples.append(p);
        c.insert("samples",samples);
        chargers[i]=c;
        q.finish();
    }
    *out={{"stationId",stationId},{"stationName",name},{"stations",stations},
          {"latestAt",latest},{"chargers",chargers}};
    return Result::success();
}
} // namespace EnergyReadModel
