#pragma once

#include <QString>
#include <QtGlobal>

namespace ev::user {

struct User final {
    qint64 id = 0;
    QString mobile;
    QString nickname;
    QString avatarPath;
    qint64 balanceFen = 0;
    QString status; // active | frozen
    QString registeredAt;
};

struct Station final {
    qint64 id = 0;
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    qint64 priceFenPerKwh = 0;
    bool forecastEnabled = false;
    double distanceKm = 0.0;
    QString createdAt;
};

struct Charger final {
    qint64 id = 0;
    qint64 stationId = 0;
    QString code;
    QString type;
    double powerKw = 0.0;
    QString status; // idle | reserved | charging | fault | restarting
    qint64 chargeCount = 0;
    qint64 totalDurationSec = 0;
    QString updatedAt;
};

struct CurrentOrder final {
    qint64 id = 0;
    qint64 userId = 0;
    qint64 chargerId = 0;
    QString status; // reserved | charging | completed | cancelled
    QString reservedAt;
    QString startedAt;
    QString endedAt;
    double energyKwh = 0.0;
    qint64 amountFen = 0;
};

struct HistoryOrder final {
    qint64 id = 0;
    qint64 userId = 0;
    qint64 chargerId = 0;
    QString stationName;
    QString chargerCode;
    QString status; // reserved | charging | completed | cancelled
    QString reservedAt;
    QString startedAt;
    QString endedAt;
    double energyKwh = 0.0;
    qint64 amountFen = 0;
};

struct ApiError final {
    QString requestId;
    QString code;
    QString message;
};

} // namespace ev::user
