#include "net/TencentMapClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
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

TencentMapClient::~TencentMapClient()
{
    struct TeardownGuards final {
        QPointer<QNetworkReply> reply;
        QPointer<QTimer> deadline;
    };
    QList<TeardownGuards> outstanding;
    outstanding.reserve(outstandingReplies_.size());
    for (auto it = outstandingReplies_.cbegin(); it != outstandingReplies_.cend(); ++it) {
        outstanding.append({it.key(), it.value()});
    }
    outstandingReplies_.clear();

    // Disconnect every client callback before aborting any reply: an external direct slot
    // may destroy the manager and therefore every other reply during the first abort.
    for (const TeardownGuards &entry : std::as_const(outstanding)) {
        if (entry.deadline != nullptr) {
            entry.deadline->stop();
        }
        if (entry.deadline != nullptr) {
            QObject::disconnect(entry.deadline.data(), nullptr, this, nullptr);
        }
        if (entry.reply != nullptr) {
            QObject::disconnect(entry.reply.data(), nullptr, this, nullptr);
        }
    }
    for (const TeardownGuards &entry : std::as_const(outstanding)) {
        if (entry.reply == nullptr) {
            continue;
        }
        const bool finished = entry.reply->isFinished();
        if (entry.reply != nullptr && !finished) {
            entry.reply->abort();
        }
        if (entry.reply != nullptr) {
            entry.reply->deleteLater();
        }
    }
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
    outstandingReplies_.insert(reply, deadline);
    const QPointer<QNetworkReply> guardedReply(reply);

    connect(reply, &QObject::destroyed, this, [this, reply] {
        outstandingReplies_.remove(reply);
    });
    connect(deadline, &QTimer::timeout, this, [this, guardedReply, requestId] {
        QPointer<TencentMapClient> guardedClient(this);
        QPointer<QNetworkReply> replyGuard = guardedReply;
        if (replyGuard == nullptr
            || outstandingReplies_.remove(replyGuard.data()) == 0) {
            return;
        }
        replyGuard->abort();
        if (guardedClient == nullptr) {
            return;
        }
        emit guardedClient->geocodeFailed(
            {requestId, QStringLiteral("MAP_TIMEOUT"),
             QStringLiteral("腾讯地图请求超时，请重试")});
        if (replyGuard != nullptr) {
            replyGuard->deleteLater();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, guardedReply, requestId] {
        QPointer<QNetworkReply> replyGuard = guardedReply;
        if (replyGuard == nullptr) {
            return;
        }
        const auto it = outstandingReplies_.find(replyGuard.data());
        if (it == outstandingReplies_.end()) {
            return;
        }
        QPointer<QTimer> deadline = it.value();
        outstandingReplies_.erase(it);
        if (deadline != nullptr) {
            deadline->stop();
        }
        if (replyGuard->error() != QNetworkReply::NoError) {
            emit geocodeFailed({requestId, QStringLiteral("NETWORK_ERROR"), QStringLiteral("腾讯地图网络请求失败，请重试")});
            if (replyGuard != nullptr) {
                replyGuard->deleteLater();
            }
            return;
        }
        const ParsedGeocode parsed = parseResponse(replyGuard->readAll());
        if (parsed.success) {
            emit geocodeSucceeded(requestId, parsed.coordinate);
        } else {
            emit geocodeFailed({requestId, parsed.code, parsed.message});
        }
        if (replyGuard != nullptr) {
            replyGuard->deleteLater();
        }
    });
    deadline->start();
    return requestId;
}
