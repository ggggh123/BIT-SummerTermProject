#include "services/DashboardService.h"

#include <QSqlQuery>
#include <QVariant>
#include <QtGlobal>

#include <utility>

DashboardService::DashboardService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

QJsonObject DashboardService::summary() const
{
    const int idle = countChargersByStatus(QStringLiteral("idle"));
    const int reserved = countChargersByStatus(QStringLiteral("reserved"));
    const int charging = countChargersByStatus(QStringLiteral("charging"));
    const int fault = countChargersByStatus(QStringLiteral("fault"));
    const int restarting = countChargersByStatus(QStringLiteral("restarting"));
    const double totalRevenue = revenueFen() / 100.0;

    return {
        {QStringLiteral("todayRevenue"), 0.0},
        {QStringLiteral("monthRevenue"), totalRevenue},
        {QStringLiteral("totalRevenue"), totalRevenue},
        {QStringLiteral("pileTotal"), idle + reserved + charging + fault + restarting},
        {QStringLiteral("pileIdle"), idle},
        {QStringLiteral("pileUsing"), reserved + charging + restarting},
        {QStringLiteral("pileFault"), fault}
    };
}

QList<QStringList> DashboardService::chargerRows() const
{
    QList<QStringList> rows;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT c.code, s.name, c.type, c.power_kw, c.status "
        "FROM chargers c "
        "JOIN stations s ON s.id = c.station_id "
        "ORDER BY CAST(c.code AS INTEGER)"));
    if (!query.exec()) {
        return rows;
    }

    while (query.next()) {
        rows.append(QStringList{
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            QString::number(query.value(3).toDouble(), 'f', 0) + QStringLiteral("kW"),
            query.value(4).toString()
        });
    }
    return rows;
}

QList<QStringList> DashboardService::stationRows() const
{
    QList<QStringList> rows;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT s.id, s.name, s.address, "
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
        const int total = query.value(3).toInt();
        const int online = query.value(4).toInt();
        const int percent = total == 0 ? 0 : qRound((online * 100.0) / total);
        rows.append(QStringList{
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            QString::number(percent) + QStringLiteral("%")
        });
    }
    return rows;
}

QList<QStringList> DashboardService::userRows() const
{
    QList<QStringList> rows;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, mobile, nickname, balance_fen, status "
        "FROM users "
        "ORDER BY id"));
    if (!query.exec()) {
        return rows;
    }

    while (query.next()) {
        rows.append(QStringList{
            query.value(0).toString(),
            query.value(1).toString(),
            query.value(2).toString(),
            QString::number(query.value(3).toLongLong() / 100.0, 'f', 2),
            query.value(4).toString()
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

