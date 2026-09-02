#include "net/TencentMapClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include <cmath>
#include <utility>

namespace {

struct ParsedGeocode final {
    bool success = false;
    ev::user::GeoPoint coordinate;
    QString code;
    QString message;
};

ParsedGeocode parseResponse(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {false, {}, QStringLiteral("INVALID_RESPONSE"), QStringLiteral("腾讯地图响应无效")};
    }
    const QJsonObject root = document.object();
    const QJsonValue statusValue = root.value(QStringLiteral("status"));
    if (!statusValue.isDouble() || !std::isfinite(statusValue.toDouble())
        || std::floor(statusValue.toDouble()) != statusValue.toDouble()) {
        return {false, {}, QStringLiteral("INVALID_RESPONSE"), QStringLiteral("腾讯地图响应无效")};
    }
    if (statusValue.toInt() != 0) {
        return {false, {}, QStringLiteral("MAP_API_ERROR"), QStringLiteral("腾讯地图服务返回错误")};
    }
    const QJsonValue resultValue = root.value(QStringLiteral("result"));
    if (!resultValue.isObject()) {
        return {false, {}, QStringLiteral("INVALID_RESPONSE"), QStringLiteral("腾讯地图响应无效")};
    }
    const QJsonValue locationValue = resultValue.toObject().value(QStringLiteral("location"));
    if (!locationValue.isObject()) {
        return {false, {}, QStringLiteral("INVALID_RESPONSE"), QStringLiteral("腾讯地图响应无效")};
    }
    const QJsonObject location = locationValue.toObject();
    const QJsonValue latitudeValue = location.value(QStringLiteral("lat"));
    const QJsonValue longitudeValue = location.value(QStringLiteral("lng"));
    if (!latitudeValue.isDouble() || !longitudeValue.isDouble()) {
        return {false, {}, QStringLiteral("INVALID_RESPONSE"), QStringLiteral("腾讯地图响应无效")};
    }
    const ev::user::GeoPoint coordinate{latitudeValue.toDouble(), longitudeValue.toDouble()};
    if (!std::isfinite(coordinate.latitude) || coordinate.latitude < -90.0 || coordinate.latitude > 90.0
        || !std::isfinite(coordinate.longitude) || coordinate.longitude < -180.0 || coordinate.longitude > 180.0) {
        return {false, {}, QStringLiteral("INVALID_RESPONSE"), QStringLiteral("腾讯地图响应无效")};
    }
    return {true, coordinate, {}, {}};
}

} // namespace

TencentMapClient::TencentMapClient(QString apiKey, QNetworkAccessManager *networkManager,
                                   QUrl endpoint, int timeoutMs, QObject *parent)
    : QObject(parent)
    , apiKey_(std::move(apiKey))
    , networkManager_(networkManager != nullptr ? networkManager : new QNetworkAccessManager(this))
    , endpoint_(std::move(endpoint))
    , timeoutMs_(timeoutMs)
{
    Q_ASSERT(networkManager_ != nullptr);
    Q_ASSERT(timeoutMs_ > 0);
    qRegisterMetaType<ev::user::GeoPoint>();
    qRegisterMetaType<ev::user::ApiError>();
}

QUrl TencentMapClient::productionEndpoint()
{
    return QUrl(QStringLiteral("https://apis.map.qq.com/ws/geocoder/v1/"));
}

QUrl TencentMapClient::buildGeocodeUrl(const QUrl &endpoint, const QString &address,
                                       const QString &apiKey)
{
    QUrl url(endpoint);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("address"), address);
    query.addQueryItem(QStringLiteral("key"), apiKey);
    query.addQueryItem(QStringLiteral("output"), QStringLiteral("json"));
    url.setQuery(query);
    return url;
}

QString TencentMapClient::geocode(const QString &address)
{
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString normalizedAddress = address.trimmed();
    if (normalizedAddress.isEmpty()) {
        QTimer::singleShot(0, this, [this, requestId] {
            emit geocodeFailed({requestId, QStringLiteral("INVALID_ADDRESS"), QStringLiteral("请输入地址")});
        });
        return requestId;
    }

    QNetworkRequest request(buildGeocodeUrl(endpoint_, normalizedAddress, apiKey_));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ev-user-client/1.0"));
    QNetworkReply *reply = networkManager_->get(request);
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->setInterval(timeoutMs_);

    connect(deadline, &QTimer::timeout, reply, [this, reply, requestId] {
        if (reply->property("evTerminal").toBool()) {
            return;
        }
        reply->setProperty("evTerminal", true);
        reply->abort();
        emit geocodeFailed({requestId, QStringLiteral("MAP_TIMEOUT"), QStringLiteral("腾讯地图请求超时，请重试")});
        reply->deleteLater();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, deadline, requestId] {
        if (reply->property("evTerminal").toBool()) {
            return;
        }
        reply->setProperty("evTerminal", true);
        deadline->stop();
        if (reply->error() != QNetworkReply::NoError) {
            emit geocodeFailed({requestId, QStringLiteral("NETWORK_ERROR"), QStringLiteral("腾讯地图网络请求失败，请重试")});
            reply->deleteLater();
            return;
        }
        const ParsedGeocode parsed = parseResponse(reply->readAll());
        if (parsed.success) {
            emit geocodeSucceeded(requestId, parsed.coordinate);
        } else {
            emit geocodeFailed({requestId, parsed.code, parsed.message});
        }
        reply->deleteLater();
    });
    deadline->start();
    return requestId;
}
