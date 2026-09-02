#include "services/UserApi.h"

#include "net/TcpJsonClient.h"
#include "protocol/Envelope.h"

#include <QDateTime>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <utility>

namespace {

constexpr qint64 kMaxSafeInteger = 9'007'199'254'740'991LL;
const QString kInvalidResponse = QStringLiteral("INVALID_RESPONSE");
const QString kInvalidResponseMessage = QStringLiteral("服务器响应无效");
const QRegularExpression kMobilePattern(QStringLiteral("^1[3-9][0-9]{9}$"));
const QRegularExpression kTimestampPattern(
    QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d+)?\\+08:00$"));

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
    const QRegularExpressionMatch match = kTimestampPattern.match(timestamp);
    if (!match.hasMatch()) {
        return false;
    }
    const QDateTime dateTime = QDateTime::fromString(match.captured(1) + QStringLiteral("+08:00"), Qt::ISODate);
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
        || (status.toString() != QStringLiteral("active") && status.toString() != QStringLiteral("frozen"))) {
        return false;
    }
    decoded.mobile = mobile.toString();
    decoded.avatarPath = avatarPath.toString();
    decoded.status = status.toString();
    *user = std::move(decoded);
    return true;
}

bool parseCurrentOrder(const QJsonValue &value, ev::user::CurrentOrderResult *result)
{
    if (value.isNull()) {
        result->order.reset();
        return true;
    }
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!hasExactlyKeys(object, {"orderId", "userId", "chargerId", "stationId", "stationName", "chargerCode", "status", "reservedAt", "startedAt", "endedAt", "energyKwh", "amountFen", "elapsedSec"})) {
        return false;
    }
    ev::user::CurrentOrder decoded;
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
    if (decoded.status == QStringLiteral("reserved")) {
        if (!decoded.startedAt.isEmpty() || !decoded.endedAt.isEmpty()) {
            return false;
        }
    } else if (decoded.status == QStringLiteral("charging")) {
        if (decoded.startedAt.isEmpty()) {
            return false;
        }
    } else {
        return false;
    }
    result->order = std::move(decoded);
    return true;
}

bool validFailure(const ev::protocol::ResponseEnvelope &response)
{
    return !response.code.isEmpty() && response.data.isObject() && response.data.toObject().isEmpty();
}

} // namespace

UserApi::UserApi(TcpJsonClient *client, QObject *parent)
    : QObject(parent)
    , client_(client)
{
    Q_ASSERT(client_ != nullptr);
    qRegisterMetaType<ev::user::User>();
    qRegisterMetaType<ev::user::CurrentOrder>();
    qRegisterMetaType<ev::user::CurrentOrderResult>();
    qRegisterMetaType<ev::user::ApiError>();
    connect(client_, &TcpJsonClient::responseReceived, this, &UserApi::handleResponse);
    connect(client_, &TcpJsonClient::transportFailed, this, &UserApi::handleTransportFailure);
    connect(client_, &TcpJsonClient::connectionChanged, this, &UserApi::connectionChanged);
}

void UserApi::loginByPhone(const QString &mobile)
{
    ++sessionGeneration_;
    user_.reset();
    token_.clear();
    emit loginPendingChanged(true);
    if (!kMobilePattern.match(mobile).hasMatch()) {
        emit loginPendingChanged(false);
        emitFailure(QString(), QStringLiteral("INVALID_PHONE"), QStringLiteral("请输入有效的手机号"));
        return;
    }
    const QString requestId = client_->send(
        QStringLiteral("auth.user_login"), QJsonObject{{QStringLiteral("mobile"), mobile}});
    pendingOperations_.insert(requestId, {Operation::Login, sessionGeneration_});
}

void UserApi::loadCurrentOrder()
{
    if (!user_.has_value() || token_.isEmpty()) {
        emitFailure(QString(), QStringLiteral("AUTH_REQUIRED"), QStringLiteral("请先登录"));
        return;
    }
    const QString requestId = client_->send(QStringLiteral("order.current"), {}, token_);
    pendingOperations_.insert(requestId, {Operation::CurrentOrder, sessionGeneration_});
}

std::optional<ev::user::User> UserApi::sessionUser() const
{
    return user_;
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
        user_ = user;
        emit loginSucceeded(user);
        return;
    }
    if (!hasExactlyKeys(data, {"order"})) {
        emitInvalidResponse(response.requestId);
        return;
    }
    ev::user::CurrentOrderResult result;
    if (!parseCurrentOrder(data.value(QStringLiteral("order")), &result)) {
        emitInvalidResponse(response.requestId);
        return;
    }
    emit currentOrderLoaded(result);
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
