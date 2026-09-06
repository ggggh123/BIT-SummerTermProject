#include "services/DashboardService.h"

#include <QSqlQuery>
#include <QDateTime>
#include <QJsonArray>
#include <QTimeZone>
#include <QVariant>
#include <QtGlobal>

#include <utility>

DashboardService::DashboardService(QSqlDatabase database)
    : m_database(std::move(database))
{
}

QJsonObject DashboardService::summary(int rangeDays, const QDateTime &now) const
{
    const int idle = countChargersByStatus(QStringLiteral("idle"));
    const int reserved = countChargersByStatus(QStringLiteral("reserved"));
    const int charging = countChargersByStatus(QStringLiteral("charging"));
    const int fault = countChargersByStatus(QStringLiteral("fault"));
    const int restarting = countChargersByStatus(QStringLiteral("restarting"));
    const qint64 totalRevenue = revenueFen();
    const QDate today = now.toTimeZone(QTimeZone(QByteArrayLiteral("Asia/Shanghai"))).date();
    const QString todayText = today.toString(Qt::ISODate);
    const QString monthText = today.toString(QStringLiteral("yyyy-MM"));
    QJsonArray trend;
    for (int i = rangeDays - 1; i >= 0; --i) {
        const QString date = today.addDays(-i).toString(Qt::ISODate);
        trend.append(QJsonObject{{QStringLiteral("date"), date},
                                 {QStringLiteral("revenueFen"), revenueFenForDate(date)}});
    }

    return {
        {QStringLiteral("revenue"), QJsonObject{
            {QStringLiteral("todayRevenueFen"), revenueFenForDate(todayText)},
            {QStringLiteral("monthRevenueFen"), revenueFenForMonth(monthText)},
            {QStringLiteral("totalRevenueFen"), totalRevenue}}},
        {QStringLiteral("statusCounts"), QJsonObject{
            {QStringLiteral("idle"), idle},
            {QStringLiteral("reserved"), reserved},
            {QStringLiteral("charging"), charging},
            {QStringLiteral("fault"), fault},
            {QStringLiteral("restarting"), restarting},
            {QStringLiteral("total"), idle + reserved + charging + fault + restarting}}},
        {QStringLiteral("trend"), trend},
        {QStringLiteral("alerts"), activeForecastAlerts()}
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

qint64 DashboardService::revenueFenForMonth(const QString &month) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(amount_fen), 0) FROM orders "
        "WHERE status='completed' AND substr(ended_at, 1, 7)=?"));
    query.addBindValue(month);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

QJsonArray DashboardService::activeForecastAlerts() const
{
    QJsonArray alerts;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT f.station_id, s.name, f.forecast_at, f.congestion_level, "
        "f.predicted_load_kw, f.predicted_busy_count, f.predicted_idle_count, f.is_peak "
        "FROM forecasts f "
        "JOIN forecast_runs r ON r.run_id=f.run_id "
        "JOIN stations s ON s.id=f.station_id "
        "WHERE r.status='active' AND (f.congestion_level='high' OR f.is_peak=1) "
        "ORDER BY f.forecast_at, f.station_id"));
    if (!query.exec()) {
        return alerts;
    }

    while (query.next()) {
        alerts.append(QJsonObject{
            {QStringLiteral("stationId"), query.value(0).toInt()},
            {QStringLiteral("stationName"), query.value(1).toString()},
            {QStringLiteral("forecastAt"), query.value(2).toString()},
            {QStringLiteral("congestionLevel"), query.value(3).toString()},
            {QStringLiteral("predictedLoadKw"), query.value(4).toDouble()},
            {QStringLiteral("predictedBusyCount"), query.value(5).toInt()},
            {QStringLiteral("predictedIdleCount"), query.value(6).toInt()},
            {QStringLiteral("isPeak"), query.value(7).toInt() == 1}
        });
    }
    return alerts;
}
