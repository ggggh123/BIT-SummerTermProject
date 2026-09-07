#pragma once

#include "core/Result.h"

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class TelemetryService
{
public:
    explicit TelemetryService(QSqlDatabase database);

    Result telemetryPush(const QJsonObject &payload, QJsonObject *responseData) const;
    Result faultSet(const QJsonObject &payload, QJsonObject *responseData) const;
    Result simulatorStatus(const QJsonObject &payload, QJsonObject *responseData) const;

private:
    QJsonObject chargerObject(int chargerId) const;
    QJsonObject orderObject(int orderId) const;
    Result requirePositiveId(const QJsonObject &payload, const QString &field, int *value) const;
    int activeOrderForCharger(int chargerId) const;
    bool validTimestamp(const QString &value) const;
    QString lastDeviceEventAt(int chargerId) const;

    QSqlDatabase m_database;
};
