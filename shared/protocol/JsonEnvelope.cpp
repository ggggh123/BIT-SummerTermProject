#include "protocol/JsonEnvelope.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace ev::protocol {
namespace {

QJsonObject parseObject(QByteArrayView json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(json.data(), json.size()), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), QStringLiteral("payload must be one JSON object"));
    }
    return document.object();
}

QString requiredString(const QJsonObject &object, const QString &field)
{
    const QJsonValue value = object.value(field);
    if (!value.isString()) {
        throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), field + QStringLiteral(" must be a string"));
    }
    return value.toString();
}

QString requiredNonblankString(const QJsonObject &object, const QString &field)
{
    const QString value = requiredString(object, field);
    if (value.trimmed().isEmpty()) {
        throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), field + QStringLiteral(" must be a nonblank string"));
    }
    return value;
}

} // namespace

EnvelopeError::EnvelopeError(QString code, QString message)
    : std::runtime_error(message.toStdString())
    , code_(std::move(code))
    , message_(std::move(message))
{
}

QString EnvelopeError::code() const
{
    return code_;
}

QString EnvelopeError::message() const
{
    return message_;
}

QByteArray toJson(const RequestEnvelope &request)
{
    QJsonObject object;
    object.insert(QStringLiteral("version"), request.version);
    object.insert(QStringLiteral("requestId"), request.requestId);
    object.insert(QStringLiteral("action"), request.action);
    object.insert(QStringLiteral("token"), request.token);
    object.insert(QStringLiteral("payload"), request.payload);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray toJson(const ResponseEnvelope &response)
{
    QJsonObject object;
    object.insert(QStringLiteral("requestId"), response.requestId);
    object.insert(QStringLiteral("ok"), response.ok);
    object.insert(QStringLiteral("code"), response.code);
    object.insert(QStringLiteral("message"), response.message);
    object.insert(QStringLiteral("data"), response.data);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

RequestEnvelope parseRequest(QByteArrayView json)
{
    const QJsonObject object = parseObject(json);

    const QJsonValue versionValue = object.value(QStringLiteral("version"));
    if (!versionValue.isDouble()) {
        throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), QStringLiteral("version must be a number"));
    }
    if (versionValue.toDouble() != 1.0) {
        throw EnvelopeError(QStringLiteral("UNSUPPORTED_VERSION"), QStringLiteral("version must be 1"));
    }

    QString token;
    const QJsonValue tokenValue = object.value(QStringLiteral("token"));
    if (!tokenValue.isUndefined()) {
        if (!tokenValue.isString()) {
            throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), QStringLiteral("token must be a string"));
        }
        token = tokenValue.toString();
    }

    const QJsonValue payloadValue = object.value(QStringLiteral("payload"));
    if (!payloadValue.isObject()) {
        throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), QStringLiteral("payload must be an object"));
    }

    return {
        versionValue.toInt(),
        requiredNonblankString(object, QStringLiteral("requestId")),
        requiredNonblankString(object, QStringLiteral("action")),
        token,
        payloadValue.toObject()
    };
}

ResponseEnvelope parseResponse(QByteArrayView json)
{
    const QJsonObject object = parseObject(json);
    const QJsonValue okValue = object.value(QStringLiteral("ok"));
    if (!okValue.isBool()) {
        throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), QStringLiteral("ok must be a boolean"));
    }
    if (object.value(QStringLiteral("data")).isUndefined()) {
        throw EnvelopeError(QStringLiteral("INVALID_REQUEST"), QStringLiteral("data is required"));
    }

    return {
        requiredString(object, QStringLiteral("requestId")),
        okValue.toBool(),
        requiredString(object, QStringLiteral("code")),
        requiredString(object, QStringLiteral("message")),
        object.value(QStringLiteral("data"))
    };
}

} // namespace ev::protocol
