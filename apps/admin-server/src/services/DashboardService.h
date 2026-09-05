#pragma once

#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QStringList>

class DashboardService
{
public:
    explicit DashboardService(QSqlDatabase database);

    QJsonObject summary(int rangeDays = 7) const;
    QList<QStringList> chargerRows() const;
    QList<QStringList> stationRows() const;
    QList<QStringList> userRows() const;

private:
    int countChargersByStatus(const QString &status) const;
    qint64 revenueFen() const;
    qint64 revenueFenForDate(const QString &date) const;

    QSqlDatabase m_database;
};

