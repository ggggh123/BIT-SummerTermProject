#pragma once

#include "core/Result.h"

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

// 管理端业务：站点创建、故障桩远程重启、用户列表与冻结/解冻。
class AdminService
{
public:
    explicit AdminService(QSqlDatabase database);

    Result stationCreate(const QJsonObject &payload, QJsonObject *responseData) const;
    Result chargerRestart(const QJsonObject &payload, QJsonObject *responseData) const;
    Result finishRestart(int chargerId) const;
    Result userList(const QJsonObject &payload, QJsonObject *responseData) const;
    Result userSetStatus(const QJsonObject &payload, QJsonObject *responseData) const;

private:
    QJsonObject stationObject(int stationId) const;
    QJsonObject chargerObject(int chargerId) const;
    QJsonObject userObject(int userId) const;
    Result requirePositiveId(const QJsonObject &payload, const QString &field, int *value) const;

    QSqlDatabase m_database;
};
