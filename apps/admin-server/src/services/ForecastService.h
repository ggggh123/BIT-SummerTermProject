#pragma once

#include "core/Result.h"

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class ForecastService
{
public:
    ForecastService(QSqlDatabase database, QString snapshotPath);

    QJsonObject healthState() const;
    Result latest(QJsonObject *responseData) const;
    Result publish(const QString &requestId, const QJsonObject &payload, QJsonObject *responseData) const;

private:
    Result validatePayload(const QJsonObject &payload, QString *runId) const;
    Result insertForecastRun(const QJsonObject &payload, const QString &runId) const;
    QString acceptedActiveRunHash(const QString &runId) const;
    bool writeSnapshot(QString *errorMessage) const;

    QSqlDatabase m_database;
    QString m_snapshotPath;
};

