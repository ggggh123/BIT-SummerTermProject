#pragma once

#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QStringList>

class DashboardService
{
public:
    explicit DashboardService(QSqlDatabase database);

    QJsonObject summary() const;
    QList<QStringList> chargerRows() const;
    QList<QStringList> stationRows() const;
    QList<QStringList> userRows() const;

private:
    int countChargersByStatus(const QString &status) const;
    qint64 revenueFen() const;

    QSqlDatabase m_database;
};

