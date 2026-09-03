#pragma once

#include <QString>
#include <QMetaType>
#include <QVector>
#include <QtGlobal>

#include <optional>

namespace ev::user {

struct User final {
    qint64 userId = 0;
    QString mobile;
    QString nickname;
    QString avatarPath;
    qint64 balanceFen = 0;
    QString status; // active | frozen
    QString registeredAt;
};

struct Station final {
    qint64 stationId = 0;
    QString name;
    QString address;
    double latitude = 0.0;
    double longitude = 0.0;
    qint64 priceFenPerKwh = 0;
    bool forecastEnabled = false;
    qint64 chargerCount = 0;
    qint64 idleCount = 0;
    std::optional<double> distanceKm;
};

struct Charger final {
    qint64 chargerId = 0;
    qint64 stationId = 0;
    QString code;
    QString type;
    double powerKw = 0.0;
    QString status; // idle | reserved | charging | fault | restarting
    qint64 chargeCount = 0;
    qint64 totalDurationSec = 0;
    QString updatedAt;
};

struct SystemHealthResult final {
    QString status; // starting | degraded | ready
    qint64 schemaVersion = 0;
    qint64 snapshotVersion = 0;
    std::optional<QString> forecastRunId;
    QString serverTime;
};

struct ChargerListResult final {
    qint64 stationId = 0;
    QVector<Charger> chargers;
};

struct GeoPoint final {
    double latitude = 0.0;
    double longitude = 0.0;
};

struct StationListResult final {
    GeoPoint origin;
    QVector<Station> stations;
};

struct StationDetailResult final {
    Station station;
    QVector<Charger> chargers;
};

struct ForecastRun final {
    QString runId;
    QString generatedAt;
    QString dataCutoff;
    QString activatedAt;
    QString modelVersion;
    QString payloadHash;
    bool stale = false;
};

struct ForecastRecord final {
    qint64 stationId = 0;
    QString forecastAt;
    qint64 horizonH = 0;
    double predictedLoadKw = 0.0;
    qint64 predictedBusyCount = 0;
    qint64 predictedIdleCount = 0;
    QString congestionLevel;
    bool isPeak = false;
};

struct ForecastLatestResult final {
    std::optional<ForecastRun> forecastRun;
    QVector<ForecastRecord> records;
};

struct StationSelection final {
    GeoPoint origin;
    Station station;
    Charger charger;
    quint64 selectionGeneration = 0;
};

struct Order final {
    qint64 orderId = 0;
    qint64 userId = 0;
    qint64 chargerId = 0;
    qint64 stationId = 0;
    QString stationName;
    QString chargerCode;
    QString status; // reserved | charging | completed | cancelled
    QString reservedAt;
    QString startedAt; // Timestamp; empty QString represents a JSON null wire value.
    QString endedAt; // Timestamp; empty QString represents a JSON null wire value.
    double energyKwh = 0.0;
    qint64 amountFen = 0;
    qint64 elapsedSec = 0;
};

using CurrentOrder = Order;

struct CurrentOrderResult final {
    std::optional<Order> order;
};

enum class ChargeOperation {
    Guard,
    Poll,
    Reconcile,
    Reserve,
    Start,
    Stop,
    Settle,
    Cancel,
};

struct RequestContext final {
    QString requestId;
    quint64 sessionGeneration = 0;
    quint64 pageGeneration = 0;
    quint64 selectionGeneration = 0;
    ChargeOperation operation = ChargeOperation::Guard;
    quint64 readEpoch = 0;
};

inline bool operator==(const RequestContext &left, const RequestContext &right)
{
    return left.requestId == right.requestId
        && left.sessionGeneration == right.sessionGeneration
        && left.pageGeneration == right.pageGeneration
        && left.selectionGeneration == right.selectionGeneration
        && left.operation == right.operation
        && left.readEpoch == right.readEpoch;
}

inline bool operator!=(const RequestContext &left, const RequestContext &right)
{
    return !(left == right);
}

using HistoryOrder = Order;

struct OrderListResult final {
    QVector<Order> items;
    qint64 total = 0;
};

struct HistoryRequestContext final {
    QString requestId;
    quint64 sessionGeneration = 0;
    quint64 pageGeneration = 0;
    quint64 readEpoch = 0;
    qint64 limit = 0;
    qint64 offset = 0;
};

inline bool operator==(const HistoryRequestContext &left,
                       const HistoryRequestContext &right)
{
    return left.requestId == right.requestId
        && left.sessionGeneration == right.sessionGeneration
        && left.pageGeneration == right.pageGeneration
        && left.readEpoch == right.readEpoch
        && left.limit == right.limit
        && left.offset == right.offset;
}

inline bool operator!=(const HistoryRequestContext &left,
                       const HistoryRequestContext &right)
{
    return !(left == right);
}

struct ApiError final {
    QString requestId;
    QString code;
    QString message;
};

} // namespace ev::user

Q_DECLARE_METATYPE(ev::user::User)
Q_DECLARE_METATYPE(ev::user::Order)
Q_DECLARE_METATYPE(ev::user::CurrentOrderResult)
Q_DECLARE_METATYPE(ev::user::ChargeOperation)
Q_DECLARE_METATYPE(ev::user::RequestContext)
Q_DECLARE_METATYPE(ev::user::ApiError)
Q_DECLARE_METATYPE(ev::user::GeoPoint)
Q_DECLARE_METATYPE(ev::user::Station)
Q_DECLARE_METATYPE(ev::user::Charger)
Q_DECLARE_METATYPE(ev::user::SystemHealthResult)
Q_DECLARE_METATYPE(ev::user::ChargerListResult)
Q_DECLARE_METATYPE(ev::user::StationListResult)
Q_DECLARE_METATYPE(ev::user::StationDetailResult)
Q_DECLARE_METATYPE(ev::user::ForecastRun)
Q_DECLARE_METATYPE(ev::user::ForecastRecord)
Q_DECLARE_METATYPE(ev::user::ForecastLatestResult)
Q_DECLARE_METATYPE(ev::user::StationSelection)
Q_DECLARE_METATYPE(ev::user::OrderListResult)
Q_DECLARE_METATYPE(ev::user::HistoryRequestContext)
