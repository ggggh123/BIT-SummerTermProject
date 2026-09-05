#include "services/DashboardService.h"

#include <QSqlQuery>
#include <QDate>
#include <QJsonArray>
#include <QVariant>
#include <QtGlobal>

#include <utility>

DashboardService::DashboardService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

QJsonObject DashboardService::summary(int rangeDays) const
{
    const int idle = countChargersByStatus(QStringLiteral("idle"));
    const int reserved = countChargersByStatus(QStringLiteral("reserved"));
    const int charging = countChargersByStatus(QStringLiteral("charging"));
    const int fault = countChargersByStatus(QStringLiteral("fault"));
    const int restarting = countChargersByStatus(QStringLiteral("restarting"));
    const qint64 totalRevenue = revenueFen();
    const QDate today = QDate::currentDate();
    QJsonArray trend;
    for (int i = rangeDays - 1; i >= 0; --i) {
        const QString date = today.addDays(-i).toString(Qt::ISODate);
        trend.append(QJsonObject{{QStringLiteral("date"), date},
                                 {QStringLiteral("revenueFen"), revenueFenForDate(date)}});
    }

    return {
        {QStringLiteral("revenue"), QJsonObject{
            {QStringLiteral("todayRevenueFen"), revenueFenForDate(today.toString(Qt::ISODate))},
            {QStringLiteral("monthRevenueFen"), totalRevenue},
            {QStringLiteral("totalRevenueFen"), totalRevenue}}},
        {QStringLiteral("statusCounts"), QJsonObject{
            {QStringLiteral("idle"), idle},
            {QStringLiteral("reserved"), reserved},
            {QStringLiteral("charging"), charging},
            {QStringLiteral("fault"), fault},
            {QStringLiteral("restarting"), restarting},
            {QStringLiteral("total"), idle + reserved + charging + fault + restarting}}},
        {QStringLiteral("trend"), trend},
        {QStringLiteral("alerts"), QJsonArray{}}
    };
}

QList<QStringList> DashboardService::chargerRows(int stationId, const QString &status) const
{
    QList<QStringList> rows;
    QSqlQuery query(m_database);
    QString sql = QStringLiteral(
        "SELECT c.id, c.code, s.name, c.type, c.power_kw, c.status, c.charge_count, c.total_duration_sec "
        "FROM chargers c "
        "JOIN stations s ON s.id = c.station_id WHERE 1=1 ");
    if (stationId > 0) {
        sql += QStringLiteral("AND c.station_id = ? ");
    }
    if (!status.trimmed().isEmpty()) {
        sql += QStringLiteral("AND c.status = ? ");
    }
    sql += QStringLiteral("ORDER BY CAST(c.code AS INTEGER)");
    query.prepare(sql);
    if (stationId > 0) {
        query.addBindValue(stationId);
    }
    if (!status.trimmed().isEmpty()) {
        query.addBindValue(status.trimmed());
    }
    if (!query.exec()) {
        return rows;
    }

    while (query.next()) {
        rows.append(QStringList{
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            QString::number(query.value(4).toDouble(), 'f', 0),
            query.value(5).toString(),
            query.value(6).toString(),
            query.value(7).toString()
        });
    }
    return rows;
}

QList<QStringList> DashboardService::stationRows() const
{
    QList<QStringList> rows;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.name, s.address, s.latitude, s.longitude, s.forecast_enabled, "
        "COUNT(c.id) AS total_count, "
        "SUM(CASE WHEN c.status <> 'fault' THEN 1 ELSE 0 END) AS online_count "
        "FROM stations s "
        "LEFT JOIN chargers c ON c.station_id = s.id "
        "GROUP BY s.id, s.name, s.address "
        "ORDER BY s.id"));
    if (!query.exec()) {
        return rows;
    }

    while (query.next()) {
        const int total = query.value(6).toInt();
        const int online = query.value(7).toInt();
        const int percent = total == 0 ? 0 : qRound((online * 100.0) / total);
        rows.append(QStringList{
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            QString::number(query.value(3).toDouble(), 'f', 6),
            QString::number(query.value(4).toDouble(), 'f', 6),
            query.value(6).toString(),
            QString::number(percent) + QStringLiteral("%")
            , query.value(5).toInt() == 1 ? QStringLiteral("true") : QStringLiteral("false")
        });
    }
    return rows;
}

QList<QStringList> DashboardService::userRows(const QString &mobileLike, int limit, int offset) const
{
    QList<QStringList> rows;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, mobile, nickname, balance_fen, registered_at, status "
        "FROM users WHERE mobile LIKE ? "
        "ORDER BY id LIMIT ? OFFSET ?"));
    query.addBindValue(QStringLiteral("%") + mobileLike + QStringLiteral("%"));
    query.addBindValue(limit);
    query.addBindValue(offset);
    if (!query.exec()) {
        return rows;
    }

    while (query.next()) {
        rows.append(QStringList{
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            QString::number(query.value(3).toLongLong() / 100.0, 'f', 2),
            query.value(4).toString(),
            query.value(5).toString()
        });
    }
    return rows;
}

int DashboardService::countChargersByStatus(const QString &status) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM chargers WHERE status = ?"));
    query.addBindValue(status);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

qint64 DashboardService::revenueFen() const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COALESCE(SUM(amount_fen), 0) FROM orders WHERE status = 'completed'"));
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

qint64 DashboardService::revenueFenForDate(const QString &date) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COALESCE(SUM(amount_fen), 0) FROM orders WHERE status='completed' AND substr(ended_at, 1, 10)=?"));
    query.addBindValue(date);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

