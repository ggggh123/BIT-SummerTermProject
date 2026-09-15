#pragma once

#include "core/Result.h"

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

// ML 预测（optional）：接收 forecast.publish 入库激活、forecast.latest 查询、健康页状态。
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

