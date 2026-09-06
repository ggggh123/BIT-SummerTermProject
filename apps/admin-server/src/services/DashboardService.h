#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QStringList>

class DashboardService
{
public:
    explicit DashboardService(QSqlDatabase database);

    QJsonObject summary(int rangeDays = 7,
                        const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
    QList<QStringList> chargerRows(int stationId = 0, const QString &status = QString()) const;
    QList<QStringList> stationRows() const;
    QList<QStringList> userRows(const QString &mobileLike = QString(), int limit = 20, int offset = 0) const;

private:
    int countChargersByStatus(const QString &status) const;
    qint64 revenueFen() const;
    qint64 revenueFenForDate(const QString &date) const;
    qint64 revenueFenForMonth(const QString &month) const;
    QJsonArray activeForecastAlerts() const;

    QSqlDatabase m_database;
};
