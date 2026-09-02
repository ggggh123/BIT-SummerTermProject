#pragma once

#include "domain/Models.h"
#include "protocol/Envelope.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <optional>

class TcpJsonClient;

class UserApi final : public QObject
{
    Q_OBJECT

public:
    explicit UserApi(TcpJsonClient *client, QObject *parent = nullptr);

    void loginByPhone(const QString &mobile);
    void loadCurrentOrder();
    void loadNearbyStations(const ev::user::GeoPoint &origin);
    void loadStationDetail(qint64 stationId);
    void loadLatestForecast();
    [[nodiscard]] std::optional<ev::user::User> sessionUser() const;

signals:
    void loginSucceeded(ev::user::User user);
    void currentOrderLoaded(ev::user::CurrentOrderResult result);
    void nearbyStationsLoaded(ev::user::StationListResult result);
    void stationDetailLoaded(ev::user::StationDetailResult result);
    void latestForecastLoaded(ev::user::ForecastLatestResult result);
    void requestFailed(ev::user::ApiError error);
    void loginPendingChanged(bool pending);
    void connectionChanged(bool connected);

private:
    enum class Operation {
        Login,
        CurrentOrder,
        NearbyStations,
        StationDetail,
        LatestForecast,
    };

    struct PendingOperation {
        Operation operation;
        quint64 sessionGeneration = 0;
        std::optional<ev::user::GeoPoint> origin;
        qint64 stationId = 0;
    };

    void handleResponse(const ev::protocol::ResponseEnvelope &response);
    void handleTransportFailure(const QString &requestId, const QString &code, const QString &message);
    void emitInvalidResponse(const QString &requestId);
    void emitFailure(const QString &requestId, const QString &code, const QString &message);

    TcpJsonClient *client_;
    QHash<QString, PendingOperation> pendingOperations_;
    quint64 sessionGeneration_ = 0;
    std::optional<ev::user::User> user_;
    QString token_;
};
