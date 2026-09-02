#pragma once

#include "domain/Models.h"

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

class TencentMapClient final : public QObject
{
    Q_OBJECT

public:
    explicit TencentMapClient(QString apiKey, QNetworkAccessManager *networkManager = nullptr,
                              QUrl endpoint = productionEndpoint(), int timeoutMs = 5'000,
                              QObject *parent = nullptr);

    [[nodiscard]] static QUrl productionEndpoint();
    [[nodiscard]] static QUrl buildGeocodeUrl(const QUrl &endpoint, const QString &address,
                                               const QString &apiKey);
    [[nodiscard]] QString geocode(const QString &address);

signals:
    void geocodeSucceeded(QString requestId, ev::user::GeoPoint coordinate);
    void geocodeFailed(ev::user::ApiError error);

private:
    QString apiKey_;
    QNetworkAccessManager *networkManager_;
    QUrl endpoint_;
    int timeoutMs_;
};
