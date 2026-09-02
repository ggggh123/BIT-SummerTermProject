#include "services/UserApi.h"

#include "contracts/Actions.h"
#include "contracts/Statuses.h"
#include "domain/Formatters.h"
#include "net/TcpJsonClient.h"
#include "protocol/Envelope.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr qint64 kMaxSafeInteger = 9'007'199'254'740'991LL;
const QString kInvalidResponse = QStringLiteral("INVALID_RESPONSE");
const QString kInvalidResponseMessage = QStringLiteral("服务器响应无效");
const QString kUncertainMessage = QStringLiteral("结果未确认，请重新连接后刷新账户信息");
const QRegularExpression kMobilePattern(QStringLiteral("^1[3-9][0-9]{9}$"));
const QRegularExpression kTimestampPattern(
    QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d+)?\\+08:00$"));
const QRegularExpression kPayloadHashPattern(QStringLiteral("^[0-9a-f]{64}$"));

bool hasExactlyKeys(const QJsonObject &object, std::initializer_list<const char *> keys)
{
    if (object.size() != static_cast<qsizetype>(keys.size())) {
        return false;
    }
    for (const char *key : keys) {
        if (!object.contains(QLatin1String(key))) {
            return false;
        }
    }
    return true;
}

bool isSafeInteger(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < -static_cast<double>(kMaxSafeInteger)
        || number > static_cast<double>(kMaxSafeInteger)) {
        return false;
    }
    *result = static_cast<qint64>(number);
    return true;
}

bool positiveInteger(const QJsonObject &object, const char *field, qint64 *result)
{
    return isSafeInteger(object.value(QLatin1String(field)), result) && *result > 0;
}

bool nonnegativeInteger(const QJsonObject &object, const char *field, qint64 *result)
{
    return isSafeInteger(object.value(QLatin1String(field)), result) && *result >= 0;
}

bool nonblankString(const QJsonObject &object, const char *field, QString *result)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        return false;
    }
    *result = value.toString();
    return true;
}

bool validTimestamp(const QJsonValue &value, QString *result)
{
    if (!value.isString()) {
        return false;
    }
    const QString timestamp = value.toString();
    if (!kTimestampPattern.match(timestamp).hasMatch()) {
        return false;
    }
    const QDateTime dateTime = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    if (!dateTime.isValid() || dateTime.offsetFromUtc() != 8 * 60 * 60) {
        return false;
    }
    *result = timestamp;
    return true;
}

bool nullableTimestamp(const QJsonObject &object, const char *field, QString *result)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (value.isNull()) {
        result->clear();
        return true;
    }
    return validTimestamp(value, result);
}

bool finiteNumber(const QJsonObject &object, const char *field, double minimum, double maximum,
                  double *result)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum) {
        return false;
    }
    *result = number;
    return true;
}

bool parseStation(const QJsonValue &value, bool requireDistance, ev::user::Station *station)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    const bool exact = requireDistance
        ? hasExactlyKeys(object, {"stationId", "name", "address", "latitude", "longitude",
                                  "priceFenPerKwh", "forecastEnabled", "chargerCount", "idleCount",
                                  "distanceKm"})
        : hasExactlyKeys(object, {"stationId", "name", "address", "latitude", "longitude",
                                  "priceFenPerKwh", "forecastEnabled", "chargerCount", "idleCount"});
    if (!exact) {
        return false;
    }
    ev::user::Station decoded;
    if (!positiveInteger(object, "stationId", &decoded.stationId)
        || !nonblankString(object, "name", &decoded.name)
        || !nonblankString(object, "address", &decoded.address)
        || !finiteNumber(object, "latitude", -90.0, 90.0, &decoded.latitude)
        || !finiteNumber(object, "longitude", -180.0, 180.0, &decoded.longitude)
        || !positiveInteger(object, "priceFenPerKwh", &decoded.priceFenPerKwh)
        || !nonnegativeInteger(object, "chargerCount", &decoded.chargerCount)
        || !nonnegativeInteger(object, "idleCount", &decoded.idleCount)
        || decoded.idleCount > decoded.chargerCount
        || !object.value(QStringLiteral("forecastEnabled")).isBool()) {
        return false;
    }
    decoded.forecastEnabled = object.value(QStringLiteral("forecastEnabled")).toBool();
    if (requireDistance) {
        double distance = 0.0;
        if (!finiteNumber(object, "distanceKm", 0.0, std::numeric_limits<double>::max(), &distance)) {
            return false;
        }
        decoded.distanceKm = distance;
    }
    *station = std::move(decoded);
    return true;
}

bool parseCharger(const QJsonValue &value, ev::user::Charger *charger)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!hasExactlyKeys(object, {"chargerId", "stationId", "code", "type", "powerKw", "status",
                                 "chargeCount", "totalDurationSec", "updatedAt"})) {
        return false;
    }
    ev::user::Charger decoded;
    if (!positiveInteger(object, "chargerId", &decoded.chargerId)
        || !positiveInteger(object, "stationId", &decoded.stationId)
        || !nonblankString(object, "code", &decoded.code)
        || !finiteNumber(object, "powerKw", 0.0, std::numeric_limits<double>::max(), &decoded.powerKw)
        || !nonnegativeInteger(object, "chargeCount", &decoded.chargeCount)
        || !nonnegativeInteger(object, "totalDurationSec", &decoded.totalDurationSec)
        || !validTimestamp(object.value(QStringLiteral("updatedAt")), &decoded.updatedAt)) {
        return false;
    }
    const QJsonValue type = object.value(QStringLiteral("type"));
    const QJsonValue status = object.value(QStringLiteral("status"));
    if (!type.isString() || (type.toString() != QStringLiteral("fast") && type.toString() != QStringLiteral("slow"))
        || !status.isString() || !ev::status::isCharger(status.toString())) {
        return false;
    }
    decoded.type = type.toString();
    decoded.status = status.toString();
    *charger = std::move(decoded);
    return true;
}

bool parseForecastRun(const QJsonValue &value, ev::user::ForecastRun *run)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!hasExactlyKeys(object, {"runId", "generatedAt", "dataCutoff", "activatedAt", "modelVersion",
                                 "payloadHash", "stale"})) {
        return false;
    }
    ev::user::ForecastRun decoded;
    if (!nonblankString(object, "runId", &decoded.runId)
        || !validTimestamp(object.value(QStringLiteral("generatedAt")), &decoded.generatedAt)
        || !validTimestamp(object.value(QStringLiteral("dataCutoff")), &decoded.dataCutoff)
        || !validTimestamp(object.value(QStringLiteral("activatedAt")), &decoded.activatedAt)
        || !nonblankString(object, "modelVersion", &decoded.modelVersion)
        || !object.value(QStringLiteral("payloadHash")).isString()
        || !kPayloadHashPattern.match(object.value(QStringLiteral("payloadHash")).toString()).hasMatch()
        || !object.value(QStringLiteral("stale")).isBool()) {
        return false;
    }
    if (QDateTime::fromString(decoded.dataCutoff, Qt::ISODate)
        > QDateTime::fromString(decoded.generatedAt, Qt::ISODate)) {
        return false;
    }
    decoded.payloadHash = object.value(QStringLiteral("payloadHash")).toString();
    decoded.stale = object.value(QStringLiteral("stale")).toBool();
    *run = std::move(decoded);
    return true;
}

bool parseForecastRecord(const QJsonValue &value, ev::user::ForecastRecord *record)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!hasExactlyKeys(object, {"stationId", "forecastAt", "horizonH", "predictedLoadKw",
                                 "predictedBusyCount", "predictedIdleCount", "congestionLevel", "isPeak"})) {
        return false;
    }
    ev::user::ForecastRecord decoded;
    if (!positiveInteger(object, "stationId", &decoded.stationId)
        || !validTimestamp(object.value(QStringLiteral("forecastAt")), &decoded.forecastAt)
        || !positiveInteger(object, "horizonH", &decoded.horizonH) || decoded.horizonH > 24
        || !finiteNumber(object, "predictedLoadKw", 0.0, std::numeric_limits<double>::max(), &decoded.predictedLoadKw)
        || !nonnegativeInteger(object, "predictedBusyCount", &decoded.predictedBusyCount)
        || !nonnegativeInteger(object, "predictedIdleCount", &decoded.predictedIdleCount)
        || !object.value(QStringLiteral("congestionLevel")).isString()
        || !object.value(QStringLiteral("isPeak")).isBool()) {
        return false;
    }
    decoded.congestionLevel = object.value(QStringLiteral("congestionLevel")).toString();
    const qint64 chargerCount = decoded.predictedBusyCount + decoded.predictedIdleCount;
    if (chargerCount <= 0) {
        return false;
    }
    const long double busyRatio = static_cast<long double>(decoded.predictedBusyCount)
        / static_cast<long double>(chargerCount);
    const qsizetype expectedCongestionIndex = busyRatio < 0.5L ? 0 : (busyRatio < 0.8L ? 1 : 2);
    if (!ev::status::isCongestion(decoded.congestionLevel)
        || decoded.congestionLevel != ev::status::Congestions.at(expectedCongestionIndex)) {
        return false;
    }
    decoded.isPeak = object.value(QStringLiteral("isPeak")).toBool();
    *record = std::move(decoded);
    return true;
}

bool parseNearbyStations(const QJsonObject &data, const ev::user::GeoPoint &origin,
                         ev::user::StationListResult *result)
{
    if (!hasExactlyKeys(data, {"stations"}) || !data.value(QStringLiteral("stations")).isArray()) {
        return false;
    }
    ev::user::StationListResult decoded;
    decoded.origin = origin;
    QSet<qint64> stationIds;
    for (const QJsonValue &value : data.value(QStringLiteral("stations")).toArray()) {
        ev::user::Station station;
        if (!parseStation(value, true, &station) || stationIds.contains(station.stationId)) {
            return false;
        }
        stationIds.insert(station.stationId);
        decoded.stations.append(std::move(station));
    }
    std::sort(decoded.stations.begin(), decoded.stations.end(), [](const auto &left, const auto &right) {
        if (*left.distanceKm != *right.distanceKm) {
            return *left.distanceKm < *right.distanceKm;
        }
        return left.stationId < right.stationId;
    });
    *result = std::move(decoded);
    return true;
}

bool parseStationDetail(const QJsonObject &data, qint64 requestedStationId,
                        ev::user::StationDetailResult *result)
{
    if (!hasExactlyKeys(data, {"station", "chargers"})
        || !data.value(QStringLiteral("chargers")).isArray()) {
        return false;
    }
    ev::user::StationDetailResult decoded;
    if (!parseStation(data.value(QStringLiteral("station")), false, &decoded.station)
        || decoded.station.stationId != requestedStationId) {
        return false;
    }
    QSet<qint64> chargerIds;
    QSet<QString> chargerCodes;
    qint64 idleCount = 0;
    for (const QJsonValue &value : data.value(QStringLiteral("chargers")).toArray()) {
        ev::user::Charger charger;
        if (!parseCharger(value, &charger) || charger.stationId != decoded.station.stationId
            || chargerIds.contains(charger.chargerId) || chargerCodes.contains(charger.code)) {
            return false;
        }
        chargerIds.insert(charger.chargerId);
        chargerCodes.insert(charger.code);
        if (charger.status == ev::status::Chargers.constFirst()) {
            ++idleCount;
        }
        decoded.chargers.append(std::move(charger));
    }
    if (decoded.chargers.size() != decoded.station.chargerCount
        || idleCount != decoded.station.idleCount) {
        return false;
    }
    *result = std::move(decoded);
    return true;
}

bool parseLatestForecast(const QJsonObject &data,
                         const QHash<qint64, qint64> &forecastStationCounts,
                         ev::user::ForecastLatestResult *result)
{
    if (!hasExactlyKeys(data, {"forecastRun", "records"})
        || !data.value(QStringLiteral("records")).isArray()) {
        return false;
    }
    const QJsonValue runValue = data.value(QStringLiteral("forecastRun"));
    const QJsonArray records = data.value(QStringLiteral("records")).toArray();
    ev::user::ForecastLatestResult decoded;
    if (runValue.isNull()) {
        if (!records.isEmpty()) {
            return false;
        }
        *result = std::move(decoded);
        return true;
    }
    ev::user::ForecastRun run;
    if (!parseForecastRun(runValue, &run) || records.size() != 144
        || forecastStationCounts.size() != 6) {
        return false;
    }
    qint64 previousStationId = 0;
    qint64 previousHorizon = 0;
    QSet<QString> unique;
    QHash<qint64, int> stationCounts;
    const QDateTime cutoff = QDateTime::fromString(run.dataCutoff, Qt::ISODate);
    for (const QJsonValue &value : records) {
        ev::user::ForecastRecord record;
        if (!parseForecastRecord(value, &record)) {
            return false;
        }
        const QString key = QStringLiteral("%1:%2").arg(record.stationId).arg(record.horizonH);
        const auto expectedCount = forecastStationCounts.constFind(record.stationId);
        if (expectedCount == forecastStationCounts.cend()
            || record.predictedBusyCount + record.predictedIdleCount != expectedCount.value()
            || unique.contains(key)
            || (record.stationId < previousStationId)
            || (record.stationId == previousStationId && record.horizonH <= previousHorizon)
            || QDateTime::fromString(record.forecastAt, Qt::ISODate) != cutoff.addSecs(record.horizonH * 3600)) {
            return false;
        }
        unique.insert(key);
        previousStationId = record.stationId;
        previousHorizon = record.horizonH;
        ++stationCounts[record.stationId];
        decoded.records.append(std::move(record));
    }
    if (stationCounts.size() != 6) {
        return false;
    }
    for (auto it = stationCounts.cbegin(); it != stationCounts.cend(); ++it) {
        if (it.value() != 24) {
            return false;
        }
    }
    decoded.forecastRun = std::move(run);
    *result = std::move(decoded);
    return true;
}

bool parseUser(const QJsonValue &value, ev::user::User *user)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!hasExactlyKeys(object, {"userId", "mobile", "nickname", "avatarPath", "balanceFen", "status", "registeredAt"})) {
        return false;
    }
    ev::user::User decoded;
    if (!positiveInteger(object, "userId", &decoded.userId)
        || !nonblankString(object, "nickname", &decoded.nickname)
        || !nonnegativeInteger(object, "balanceFen", &decoded.balanceFen)
        || !validTimestamp(object.value(QStringLiteral("registeredAt")), &decoded.registeredAt)) {
        return false;
    }
    const QJsonValue mobile = object.value(QStringLiteral("mobile"));
    const QJsonValue avatarPath = object.value(QStringLiteral("avatarPath"));
    const QJsonValue status = object.value(QStringLiteral("status"));
    if (!mobile.isString() || !kMobilePattern.match(mobile.toString()).hasMatch()
        || !avatarPath.isString() || !status.isString()
        || !ev::status::isUser(status.toString())) {
        return false;
    }
    decoded.mobile = mobile.toString();
    decoded.avatarPath = avatarPath.toString();
    decoded.status = status.toString();
    *user = std::move(decoded);
    return true;
}

bool parseOrder(const QJsonValue &value, ev::user::Order *order)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!hasExactlyKeys(object, {"orderId", "userId", "chargerId", "stationId", "stationName", "chargerCode", "status", "reservedAt", "startedAt", "endedAt", "energyKwh", "amountFen", "elapsedSec"})) {
        return false;
    }
    ev::user::Order decoded;
    if (!positiveInteger(object, "orderId", &decoded.orderId)
        || !positiveInteger(object, "userId", &decoded.userId)
        || !positiveInteger(object, "chargerId", &decoded.chargerId)
        || !positiveInteger(object, "stationId", &decoded.stationId)
        || !nonblankString(object, "stationName", &decoded.stationName)
        || !nonblankString(object, "chargerCode", &decoded.chargerCode)
        || !validTimestamp(object.value(QStringLiteral("reservedAt")), &decoded.reservedAt)
        || !nullableTimestamp(object, "startedAt", &decoded.startedAt)
        || !nullableTimestamp(object, "endedAt", &decoded.endedAt)
        || !nonnegativeInteger(object, "amountFen", &decoded.amountFen)
        || !nonnegativeInteger(object, "elapsedSec", &decoded.elapsedSec)) {
        return false;
    }
    const QJsonValue energy = object.value(QStringLiteral("energyKwh"));
    const QJsonValue status = object.value(QStringLiteral("status"));
    if (!energy.isDouble() || !std::isfinite(energy.toDouble()) || energy.toDouble() < 0.0
        || !status.isString()) {
        return false;
    }
    decoded.energyKwh = energy.toDouble();
    decoded.status = status.toString();
    if (!ev::status::isOrder(decoded.status)) {
        return false;
    }
    const bool startedMustBeNull = decoded.status == QStringLiteral("reserved")
        || decoded.status == QStringLiteral("cancelled");
    if (startedMustBeNull != decoded.startedAt.isEmpty()) {
        return false;
    }
    if (decoded.status == QStringLiteral("reserved") && !decoded.endedAt.isEmpty()) {
        return false;
    }
    if ((decoded.status == QStringLiteral("completed")
         || decoded.status == QStringLiteral("cancelled"))
        && decoded.endedAt.isEmpty()) {
        return false;
    }
    *order = std::move(decoded);
    return true;
}

bool parseCurrentOrder(const QJsonValue &value, ev::user::CurrentOrderResult *result)
{
    if (value.isNull()) {
        result->order.reset();
        return true;
    }
    ev::user::Order decoded;
    if (!parseOrder(value, &decoded)
        || (decoded.status != QStringLiteral("reserved")
            && decoded.status != QStringLiteral("charging"))) {
        return false;
    }
    result->order = std::move(decoded);
    return true;
}

bool validFailure(const ev::protocol::ResponseEnvelope &response)
{
    static const QSet<QString> failureCodes{
        QStringLiteral("INVALID_REQUEST"),
        QStringLiteral("UNSUPPORTED_VERSION"),
        QStringLiteral("AUTH_REQUIRED"),
        QStringLiteral("FORBIDDEN"),
        QStringLiteral("INVALID_PHONE"),
        QStringLiteral("INVALID_CREDENTIALS"),
        QStringLiteral("ENTITY_NOT_FOUND"),
        QStringLiteral("USER_FROZEN"),
        QStringLiteral("ACTIVE_ORDER_EXISTS"),
        QStringLiteral("CHARGER_NOT_AVAILABLE"),
        QStringLiteral("ORDER_STATE_CONFLICT"),
        QStringLiteral("INSUFFICIENT_BALANCE"),
        QStringLiteral("MAP_API_ERROR"),
        QStringLiteral("FORECAST_INVALID"),
        QStringLiteral("FORECAST_STALE"),
        QStringLiteral("SERVER_BUSY"),
        QStringLiteral("DB_BUSY"),
        QStringLiteral("INTERNAL_ERROR"),
    };
    return failureCodes.contains(response.code) && response.data.isObject()
        && response.data.toObject().isEmpty();
}

} // namespace

UserApi::UserApi(TcpJsonClient *client, QObject *parent)
    : QObject(parent)
    , client_(client)
{
    Q_ASSERT(client_ != nullptr);
    qRegisterMetaType<ev::user::User>();
    qRegisterMetaType<ev::user::Order>();
    qRegisterMetaType<ev::user::CurrentOrderResult>();
    qRegisterMetaType<ev::user::ChargeOperation>();
    qRegisterMetaType<ev::user::RequestContext>();
    qRegisterMetaType<ev::user::ApiError>();
    qRegisterMetaType<ev::user::GeoPoint>();
    qRegisterMetaType<ev::user::Station>();
    qRegisterMetaType<ev::user::Charger>();
    qRegisterMetaType<ev::user::StationListResult>();
    qRegisterMetaType<ev::user::StationDetailResult>();
    qRegisterMetaType<ev::user::ForecastLatestResult>();
    connect(client_, &TcpJsonClient::responseReceived, this, &UserApi::handleResponse);
    connect(client_, &TcpJsonClient::transportFailed, this, &UserApi::handleTransportFailure);
    connect(client_, &TcpJsonClient::connectionChanged, this, &UserApi::handleConnectionState);
}

void UserApi::loginByPhone(const QString &mobile)
{
    ++sessionGeneration_;
    userRevision_ = 0;
    ++chargeReadEpoch_;
    user_.reset();
    stationSnapshots_.clear();
    token_.clear();
    profileRequestId_.clear();
    if (profileOutcomeUncertain_) {
        profileOutcomeUncertain_ = false;
    }
    emit profileReadPendingChanged(false);
    emit profileMutationPendingChanged(false);
    emit loginPendingChanged(true);
    if (!kMobilePattern.match(mobile).hasMatch()) {
        emit loginPendingChanged(false);
        emitFailure(QString(), QStringLiteral("INVALID_PHONE"), QStringLiteral("请输入有效的手机号"));
        return;
    }
    const QString requestId = client_->send(
        ev::actions::AuthUserLogin, QJsonObject{{QStringLiteral("mobile"), mobile}});
    pendingOperations_.insert(requestId, {Operation::Login, sessionGeneration_});
}

ev::user::RequestContext UserApi::loadCurrentOrder(
    quint64 pageGeneration, quint64 selectionGeneration,
    ev::user::ChargeOperation operation)
{
    ev::user::RequestContext context;
    context.sessionGeneration = sessionGeneration_;
    context.pageGeneration = pageGeneration;
    context.selectionGeneration = selectionGeneration;
    context.operation = operation;
    context.readEpoch = chargeReadEpoch_;
    if (!user_.has_value() || token_.isEmpty()) {
        emitFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return context;
    }
    const QString requestId = client_->send(ev::actions::OrderCurrent, {}, token_);
    context.requestId = requestId;
    PendingOperation pending{Operation::ChargeCurrent, sessionGeneration_};
    pending.chargeContext = context;
    pendingOperations_.insert(requestId, std::move(pending));
    return context;
}

ev::user::RequestContext UserApi::reserveCharger(
    qint64 chargerId, quint64 pageGeneration, quint64 selectionGeneration)
{
    return sendChargeRequest(
        Operation::ChargeReserve, ev::user::ChargeOperation::Reserve,
        ev::actions::ChargeReserve, QJsonObject{{QStringLiteral("chargerId"), chargerId}},
        chargerId, pageGeneration, selectionGeneration);
}

ev::user::RequestContext UserApi::startCharging(
    qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration)
{
    return sendChargeRequest(
        Operation::ChargeStart, ev::user::ChargeOperation::Start,
        ev::actions::ChargeStart, QJsonObject{{QStringLiteral("orderId"), orderId}},
        orderId, pageGeneration, selectionGeneration);
}

ev::user::RequestContext UserApi::stopCharging(
    qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration)
{
    return sendChargeRequest(
        Operation::ChargeStop, ev::user::ChargeOperation::Stop,
        ev::actions::ChargeStop, QJsonObject{{QStringLiteral("orderId"), orderId}},
        orderId, pageGeneration, selectionGeneration);
}

ev::user::RequestContext UserApi::settleCharging(
    qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration)
{
    return sendChargeRequest(
        Operation::ChargeSettle, ev::user::ChargeOperation::Settle,
        ev::actions::ChargeSettle, QJsonObject{{QStringLiteral("orderId"), orderId}},
        orderId, pageGeneration, selectionGeneration);
}

ev::user::RequestContext UserApi::cancelOrder(
    qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration)
{
    return sendChargeRequest(
        Operation::ChargeCancel, ev::user::ChargeOperation::Cancel,
        ev::actions::OrderCancel, QJsonObject{{QStringLiteral("orderId"), orderId}},
        orderId, pageGeneration, selectionGeneration);
}

quint64 UserApi::invalidateChargeReads()
{
    return ++chargeReadEpoch_;
}

quint64 UserApi::currentChargeReadEpoch() const
{
    return chargeReadEpoch_;
}

QString UserApi::loadNearbyStations(const ev::user::GeoPoint &origin)
{
    if (!user_.has_value() || token_.isEmpty()) {
        emitFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return {};
    }
    if (!std::isfinite(origin.latitude) || origin.latitude < -90.0 || origin.latitude > 90.0
        || !std::isfinite(origin.longitude) || origin.longitude < -180.0 || origin.longitude > 180.0) {
        emitFailure(QString(), QStringLiteral("INVALID_COORDINATE"), QStringLiteral("坐标无效"));
        return {};
    }
    const QString requestId = client_->send(
        ev::actions::StationList,
        QJsonObject{{QStringLiteral("latitude"), origin.latitude},
                    {QStringLiteral("longitude"), origin.longitude}}, token_);
    pendingOperations_.insert(requestId, {Operation::NearbyStations, sessionGeneration_, origin});
    return requestId;
}

QString UserApi::loadStationDetail(qint64 stationId)
{
    if (!user_.has_value() || token_.isEmpty()) {
        emitFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return {};
    }
    if (stationId <= 0 || stationId > kMaxSafeInteger) {
        emitFailure(QString(), QStringLiteral("INVALID_STATION"), QStringLiteral("充电站无效"));
        return {};
    }
    const QString requestId = client_->send(
        ev::actions::StationDetail, QJsonObject{{QStringLiteral("stationId"), stationId}}, token_);
    pendingOperations_.insert(requestId, {Operation::StationDetail, sessionGeneration_, {}, stationId});
    return requestId;
}

QString UserApi::loadLatestForecast(const QString &stationListRequestId)
{
    if (!user_.has_value() || token_.isEmpty()) {
        emitFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return {};
    }
    const auto snapshot = stationSnapshots_.constFind(stationListRequestId);
    if (snapshot == stationSnapshots_.cend() || snapshot->size() != 6) {
        emitFailure(QString(), QStringLiteral("INVALID_RESPONSE"), kInvalidResponseMessage);
        return {};
    }
    const QString requestId = client_->send(ev::actions::ForecastLatest, {}, token_);
    PendingOperation pending{Operation::LatestForecast, sessionGeneration_};
    pending.forecastStationCounts = snapshot.value();
    pendingOperations_.insert(requestId, std::move(pending));
    return requestId;
}

QString UserApi::loadProfile()
{
    return loadProfile(profileOutcomeUncertain_);
}

QString UserApi::loadProfile(bool reconciliation)
{
    if (!user_.has_value() || token_.isEmpty()) {
        emitProfileFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return {};
    }
    if (profileOperationPending()) {
        emitProfileFailure(QString(), QStringLiteral("PROFILE_BUSY"),
                           QStringLiteral("账户操作正在进行，请稍候"));
        return {};
    }
    const QString requestId = client_->send(ev::actions::UserGet, {}, token_);
    PendingOperation pending{Operation::ProfileGet, sessionGeneration_};
    pending.reconciliation = reconciliation;
    pending.userRevision = userRevision_;
    pendingOperations_.insert(requestId, std::move(pending));
    profileRequestId_ = requestId;
    emit profileReadPendingChanged(true);
    return requestId;
}

QString UserApi::updateNickname(const QString &nickname)
{
    if (!user_.has_value() || token_.isEmpty()) {
        emitProfileFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return {};
    }
    const QString normalized = nickname.trimmed();
    if (normalized.isEmpty()) {
        emitProfileFailure(QString(), QStringLiteral("INVALID_NICKNAME"),
                           QStringLiteral("昵称不能为空"));
        return {};
    }
    if (profileOutcomeUncertain_) {
        emitProfileFailure(QString(), QStringLiteral("RECONCILIATION_REQUIRED"),
                           kUncertainMessage);
        return {};
    }
    if (profileOperationPending()) {
        emitProfileFailure(QString(), QStringLiteral("PROFILE_BUSY"),
                           QStringLiteral("账户操作正在进行，请稍候"));
        return {};
    }
    const QString requestId = client_->send(
        ev::actions::UserUpdate,
        QJsonObject{{QStringLiteral("nickname"), normalized}}, token_);
    PendingOperation pending{Operation::ProfileUpdate, sessionGeneration_};
    pending.userRevision = userRevision_;
    pendingOperations_.insert(requestId, std::move(pending));
    profileRequestId_ = requestId;
    emit profileMutationPendingChanged(true);
    return requestId;
}

QString UserApi::rechargeWallet(const QString &amount)
{
    if (!user_.has_value() || token_.isEmpty()) {
        emitProfileFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return {};
    }
    const auto amountFen = parsePositiveFen(amount);
    if (!amountFen.has_value()) {
        emitProfileFailure(QString(), QStringLiteral("INVALID_AMOUNT"),
                           QStringLiteral("请输入有效的充值金额（最多两位小数）"));
        return {};
    }
    if (profileOutcomeUncertain_) {
        emitProfileFailure(QString(), QStringLiteral("RECONCILIATION_REQUIRED"),
                           kUncertainMessage);
        return {};
    }
    if (profileOperationPending()) {
        emitProfileFailure(QString(), QStringLiteral("PROFILE_BUSY"),
                           QStringLiteral("账户操作正在进行，请稍候"));
        return {};
    }
    const QString requestId = client_->send(
        ev::actions::WalletRecharge,
        QJsonObject{{QStringLiteral("amountFen"), *amountFen}}, token_);
    PendingOperation pending{Operation::ProfileRecharge, sessionGeneration_};
    pending.userRevision = userRevision_;
    pendingOperations_.insert(requestId, std::move(pending));
    profileRequestId_ = requestId;
    emit profileMutationPendingChanged(true);
    return requestId;
}

std::optional<ev::user::User> UserApi::sessionUser() const
{
    return user_;
}

bool UserApi::profileNeedsReconciliation() const
{
    return profileOutcomeUncertain_;
}

bool UserApi::profileOperationPending() const
{
    return !profileRequestId_.isEmpty();
}

bool UserApi::isProfileOperation(Operation operation)
{
    return operation == Operation::ProfileGet || operation == Operation::ProfileUpdate
        || operation == Operation::ProfileRecharge;
}

bool UserApi::isProfileMutation(Operation operation)
{
    return operation == Operation::ProfileUpdate || operation == Operation::ProfileRecharge;
}

bool UserApi::isChargeOperation(Operation operation)
{
    return operation == Operation::ChargeCurrent || operation == Operation::ChargeReserve
        || operation == Operation::ChargeStart || operation == Operation::ChargeStop
        || operation == Operation::ChargeSettle || operation == Operation::ChargeCancel;
}

bool UserApi::isChargeMutation(Operation operation)
{
    return isChargeOperation(operation) && operation != Operation::ChargeCurrent;
}

ev::user::RequestContext UserApi::sendChargeRequest(
    Operation operation, ev::user::ChargeOperation publicOperation, const QString &action,
    const QJsonObject &payload, qint64 expectedEntityId, quint64 pageGeneration,
    quint64 selectionGeneration)
{
    ev::user::RequestContext context;
    context.sessionGeneration = sessionGeneration_;
    context.pageGeneration = pageGeneration;
    context.selectionGeneration = selectionGeneration;
    context.operation = publicOperation;
    context.readEpoch = chargeReadEpoch_;
    if (!user_.has_value() || token_.isEmpty()) {
        emitFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return context;
    }
    if (expectedEntityId <= 0 || expectedEntityId > kMaxSafeInteger) {
        emitFailure(QString(), QStringLiteral("INVALID_REQUEST"), QStringLiteral("订单或充电桩无效"));
        return context;
    }
    context.requestId = client_->send(action, payload, token_);
    PendingOperation pending{operation, sessionGeneration_};
    pending.chargeContext = context;
    pending.expectedEntityId = expectedEntityId;
    pendingOperations_.insert(context.requestId, std::move(pending));
    return context;
}

void UserApi::finishChargeFailure(const PendingOperation &pending, const QString &requestId,
                                  const QString &code, const QString &message, bool uncertain)
{
    if (!pending.chargeContext.has_value()) {
        return;
    }
    if (isChargeMutation(pending.operation)) {
        ++chargeReadEpoch_;
    }
    emit chargeRequestFailed(*pending.chargeContext, {requestId, code, message}, uncertain);
}

void UserApi::applySessionUser(ev::user::User user)
{
    user_ = std::move(user);
    ++userRevision_;
    emit sessionUserApplied(*user_, sessionGeneration_, userRevision_);
}

void UserApi::finishProfileOperation(Operation operation)
{
    profileRequestId_.clear();
    if (isProfileMutation(operation)) {
        emit profileMutationPendingChanged(false);
    } else {
        emit profileReadPendingChanged(false);
    }
}

void UserApi::markProfileUncertain()
{
    if (profileOutcomeUncertain_) {
        return;
    }
    profileOutcomeUncertain_ = true;
    emit profileReconciliationRequired();
}

void UserApi::handleConnectionState(bool connected)
{
    emit connectionChanged(connected);
    if (connected && profileOutcomeUncertain_ && user_.has_value() && !token_.isEmpty()
        && !profileOperationPending()) {
        (void)loadProfile(true);
    }
}

void UserApi::handleResponse(const ev::protocol::ResponseEnvelope &response)
{
    const auto it = pendingOperations_.find(response.requestId);
    if (it == pendingOperations_.end()) {
        return;
    }
    const PendingOperation pending = it.value();
    pendingOperations_.erase(it);
    if (pending.sessionGeneration != sessionGeneration_) {
        return;
    }
    if (pending.operation == Operation::Login) {
        emit loginPendingChanged(false);
    }
    if (isProfileOperation(pending.operation)) {
        const auto invalidProfileResponse = [this, &response, &pending] {
            if (isProfileMutation(pending.operation)) {
                markProfileUncertain();
            }
            finishProfileOperation(pending.operation);
            emitProfileFailure(response.requestId, kInvalidResponse, kInvalidResponseMessage);
        };
        if (!response.ok) {
            if (!validFailure(response)) {
                invalidProfileResponse();
            } else {
                finishProfileOperation(pending.operation);
                emitProfileFailure(response.requestId, response.code, response.message);
            }
            return;
        }
        if (response.code != QStringLiteral("OK") || !response.data.isObject()) {
            invalidProfileResponse();
            return;
        }
        const QJsonObject profileData = response.data.toObject();
        if (pending.operation == Operation::ProfileGet
            || pending.operation == Operation::ProfileUpdate) {
            ev::user::User decoded;
            if (!hasExactlyKeys(profileData, {"user"})
                || !parseUser(profileData.value(QStringLiteral("user")), &decoded)
                || !user_.has_value() || decoded.userId != user_->userId
                || decoded.mobile != user_->mobile) {
                invalidProfileResponse();
                return;
            }
            if (pending.userRevision != userRevision_) {
                finishProfileOperation(pending.operation);
                return;
            }
            const bool reconciled = pending.reconciliation && profileOutcomeUncertain_;
            applySessionUser(decoded);
            finishProfileOperation(pending.operation);
            if (reconciled) {
                profileOutcomeUncertain_ = false;
                emit profileReconciled(decoded);
            }
            return;
        }
        qint64 userId = 0;
        qint64 balanceFen = 0;
        if (!hasExactlyKeys(profileData, {"userId", "balanceFen"})
            || !positiveInteger(profileData, "userId", &userId)
            || !nonnegativeInteger(profileData, "balanceFen", &balanceFen)
            || !user_.has_value() || userId != user_->userId) {
            invalidProfileResponse();
            return;
        }
        if (pending.userRevision != userRevision_) {
            finishProfileOperation(pending.operation);
            return;
        }
        ev::user::User updated = *user_;
        updated.balanceFen = balanceFen;
        applySessionUser(updated);
        finishProfileOperation(pending.operation);
        return;
    }
    if (isChargeOperation(pending.operation)) {
        if (!pending.chargeContext.has_value()) {
            return;
        }
        if (pending.chargeContext->readEpoch != chargeReadEpoch_) {
            return;
        }
        if (!response.ok) {
            if (!validFailure(response)) {
                finishChargeFailure(pending, response.requestId, kInvalidResponse,
                                    kInvalidResponseMessage,
                                    isChargeMutation(pending.operation));
            } else {
                finishChargeFailure(pending, response.requestId, response.code,
                                    response.message, false);
            }
            return;
        }
        if (response.code != QStringLiteral("OK") || !response.data.isObject()) {
            finishChargeFailure(pending, response.requestId, kInvalidResponse,
                                kInvalidResponseMessage,
                                isChargeMutation(pending.operation));
            return;
        }
        const QJsonObject chargeData = response.data.toObject();
        if (pending.operation == Operation::ChargeCurrent) {
            if (!hasExactlyKeys(chargeData, {"order"})) {
                finishChargeFailure(pending, response.requestId, kInvalidResponse,
                                    kInvalidResponseMessage, false);
                return;
            }
            ev::user::CurrentOrderResult result;
            if (!parseCurrentOrder(chargeData.value(QStringLiteral("order")), &result)
                || (result.order.has_value() && user_.has_value()
                    && result.order->userId != user_->userId)) {
                finishChargeFailure(pending, response.requestId, kInvalidResponse,
                                    kInvalidResponseMessage, false);
                return;
            }
            emit currentOrderLoaded(*pending.chargeContext, result);
            return;
        }
        const bool settle = pending.operation == Operation::ChargeSettle;
        if ((settle && !hasExactlyKeys(chargeData, {"order", "balanceFen"}))
            || (!settle && !hasExactlyKeys(chargeData, {"order"}))) {
            finishChargeFailure(pending, response.requestId, kInvalidResponse,
                                kInvalidResponseMessage, true);
            return;
        }
        ev::user::Order order;
        if (!parseOrder(chargeData.value(QStringLiteral("order")), &order)
            || !user_.has_value() || order.userId != user_->userId) {
            finishChargeFailure(pending, response.requestId, kInvalidResponse,
                                kInvalidResponseMessage, true);
            return;
        }
        const bool semanticMatch =
            (pending.operation == Operation::ChargeReserve
             && order.chargerId == pending.expectedEntityId
             && order.status == QStringLiteral("reserved"))
            || (pending.operation == Operation::ChargeStart
                && order.orderId == pending.expectedEntityId
                && order.status == QStringLiteral("charging") && order.endedAt.isEmpty())
            || (pending.operation == Operation::ChargeStop
                && order.orderId == pending.expectedEntityId
                && order.status == QStringLiteral("charging") && !order.endedAt.isEmpty())
            || (pending.operation == Operation::ChargeSettle
                && order.orderId == pending.expectedEntityId
                && order.status == QStringLiteral("completed"))
            || (pending.operation == Operation::ChargeCancel
                && order.orderId == pending.expectedEntityId
                && order.status == QStringLiteral("cancelled"));
        if (!semanticMatch) {
            finishChargeFailure(pending, response.requestId, kInvalidResponse,
                                kInvalidResponseMessage, true);
            return;
        }
        ++chargeReadEpoch_;
        if (!settle) {
            emit chargeOrderChanged(*pending.chargeContext, order);
            return;
        }
        qint64 balanceFen = 0;
        if (!nonnegativeInteger(chargeData, "balanceFen", &balanceFen)) {
            emit chargeRequestFailed(*pending.chargeContext,
                                     {response.requestId, kInvalidResponse, kInvalidResponseMessage},
                                     true);
            return;
        }
        ev::user::User updated = *user_;
        updated.balanceFen = balanceFen;
        applySessionUser(updated);
        emit chargeSettled(*pending.chargeContext, order, balanceFen);
        return;
    }
    if (!response.ok) {
        if (!validFailure(response)) {
            emitInvalidResponse(response.requestId);
        } else {
            emitFailure(response.requestId, response.code, response.message);
        }
        return;
    }
    if (response.code != QStringLiteral("OK") || !response.data.isObject()) {
        emitInvalidResponse(response.requestId);
        return;
    }
    const QJsonObject data = response.data.toObject();
    if (pending.operation == Operation::Login) {
        if (!hasExactlyKeys(data, {"token", "user"}) || !data.value(QStringLiteral("token")).isString()
            || data.value(QStringLiteral("token")).toString().trimmed().isEmpty()) {
            emitInvalidResponse(response.requestId);
            return;
        }
        ev::user::User user;
        if (!parseUser(data.value(QStringLiteral("user")), &user)) {
            emitInvalidResponse(response.requestId);
            return;
        }
        token_ = data.value(QStringLiteral("token")).toString();
        applySessionUser(user);
        emit loginSucceeded(user);
        return;
    }
    if (pending.operation == Operation::NearbyStations) {
        ev::user::StationListResult result;
        if (!pending.origin.has_value() || !parseNearbyStations(data, *pending.origin, &result)) {
            emitInvalidResponse(response.requestId);
            return;
        }
        QHash<qint64, qint64> stationCounts;
        for (const auto &station : result.stations) {
            if (station.forecastEnabled) {
                stationCounts.insert(station.stationId, station.chargerCount);
            }
        }
        stationSnapshots_.insert(response.requestId, std::move(stationCounts));
        emit nearbyStationsLoaded(response.requestId, result);
        return;
    }
    if (pending.operation == Operation::StationDetail) {
        ev::user::StationDetailResult result;
        if (!parseStationDetail(data, pending.stationId, &result)) {
            emitInvalidResponse(response.requestId);
            return;
        }
        emit stationDetailLoaded(response.requestId, result);
        return;
    }
    ev::user::ForecastLatestResult result;
    if (!parseLatestForecast(data, pending.forecastStationCounts, &result)) {
        emitInvalidResponse(response.requestId);
        return;
    }
    emit latestForecastLoaded(response.requestId, result);
}

void UserApi::handleTransportFailure(const QString &requestId, const QString &code, const QString &message)
{
    const auto it = pendingOperations_.find(requestId);
    if (it == pendingOperations_.end()) {
        return;
    }
    const PendingOperation pending = it.value();
    pendingOperations_.erase(it);
    if (pending.sessionGeneration != sessionGeneration_) {
        return;
    }
    if (pending.operation == Operation::Login) {
        emit loginPendingChanged(false);
    }
    if (isProfileOperation(pending.operation)) {
        if (isProfileMutation(pending.operation) && code != QStringLiteral("NOT_CONNECTED")) {
            markProfileUncertain();
        }
        finishProfileOperation(pending.operation);
        emitProfileFailure(requestId, code, message);
        return;
    }
    if (isChargeOperation(pending.operation)) {
        if (!pending.chargeContext.has_value()
            || pending.chargeContext->readEpoch != chargeReadEpoch_) {
            return;
        }
        const bool uncertain = isChargeMutation(pending.operation)
            && code != QStringLiteral("NOT_CONNECTED");
        finishChargeFailure(pending, requestId, code, message, uncertain);
        return;
    }
    emitFailure(requestId, code, message);
}

void UserApi::emitInvalidResponse(const QString &requestId)
{
    emitFailure(requestId, kInvalidResponse, kInvalidResponseMessage);
}

void UserApi::emitFailure(const QString &requestId, const QString &code, const QString &message)
{
    emit requestFailed({requestId, code, message});
}

void UserApi::emitProfileFailure(const QString &requestId, const QString &code,
                                 const QString &message)
{
    const ev::user::ApiError error{requestId, code, message};
    emit profileRequestFailed(error);
    emit requestFailed(error);
}
