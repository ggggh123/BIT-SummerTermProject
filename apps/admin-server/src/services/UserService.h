#pragma once

#include "core/Result.h"

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

class UserService
{
public:
    explicit UserService(QSqlDatabase database);

    Result getUser(int userId, QJsonObject *responseData) const;
    Result updateUser(int userId, const QJsonObject &payload, QJsonObject *responseData) const;
    Result recharge(int userId, const QJsonObject &payload, QJsonObject *responseData) const;
    Result stationList(const QJsonObject &payload, QJsonObject *responseData) const;
    Result stationDetail(const QJsonObject &payload, QJsonObject *responseData) const;
    Result chargerList(const QJsonObject &payload, QJsonObject *responseData) const;
    Result currentOrder(int userId, QJsonObject *responseData) const;
    Result orderList(int userId, const QJsonObject &payload, QJsonObject *responseData) const;
    Result reserve(int userId, const QJsonObject &payload, QJsonObject *responseData) const;
    Result start(int userId, const QJsonObject &payload, QJsonObject *responseData) const;
    Result stop(int userId, const QJsonObject &payload, QJsonObject *responseData) const;
    Result settle(int userId, const QJsonObject &payload, QJsonObject *responseData) const;
    Result cancel(int userId, const QJsonObject &payload, QJsonObject *responseData) const;

private:
    QJsonObject userObject(int userId) const;
    QJsonObject stationObject(int stationId, bool includeDistance, double latitude = 0.0, double longitude = 0.0) const;
    QJsonObject chargerObject(int chargerId) const;
    QJsonObject orderObject(int orderId) const;
    Result requirePositiveId(const QJsonObject &payload, const QString &field, int *value) const;
    bool userFrozen(int userId) const;
    int activeOrderId(int userId) const;
    int orderOwner(int orderId) const;

    QSqlDatabase m_database;
};
