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
    [[nodiscard]] QString loadNearbyStations(const ev::user::GeoPoint &origin);
    [[nodiscard]] QString loadStationDetail(qint64 stationId);
    [[nodiscard]] QString loadLatestForecast(const QString &stationListRequestId);
    [[nodiscard]] QString loadProfile();
    [[nodiscard]] QString updateNickname(const QString &nickname);
    [[nodiscard]] QString rechargeWallet(const QString &amount);
    [[nodiscard]] std::optional<ev::user::User> sessionUser() const;
    [[nodiscard]] bool profileNeedsReconciliation() const;

signals:
    void loginSucceeded(ev::user::User user);
    void currentOrderLoaded(ev::user::CurrentOrderResult result);
    void nearbyStationsLoaded(QString requestId, ev::user::StationListResult result);
    void stationDetailLoaded(QString requestId, ev::user::StationDetailResult result);
    void latestForecastLoaded(QString requestId, ev::user::ForecastLatestResult result);
    void profileUserChanged(ev::user::User user);
    void profileReadPendingChanged(bool pending);
    void profileMutationPendingChanged(bool pending);
    void profileRequestFailed(ev::user::ApiError error);
    void profileReconciliationRequired();
    void profileReconciled(ev::user::User user);
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
        ProfileGet,
        ProfileUpdate,
        ProfileRecharge,
    };

    struct PendingOperation {
        Operation operation;
        quint64 sessionGeneration = 0;
        std::optional<ev::user::GeoPoint> origin;
        qint64 stationId = 0;
        QHash<qint64, qint64> forecastStationCounts;
        bool reconciliation = false;
    };

    [[nodiscard]] QString loadProfile(bool reconciliation);
    [[nodiscard]] bool profileOperationPending() const;
    static bool isProfileOperation(Operation operation);
    static bool isProfileMutation(Operation operation);
    void finishProfileOperation(Operation operation);
    void markProfileUncertain();
    void handleConnectionState(bool connected);
    void handleResponse(const ev::protocol::ResponseEnvelope &response);
    void handleTransportFailure(const QString &requestId, const QString &code, const QString &message);
    void emitInvalidResponse(const QString &requestId);
    void emitFailure(const QString &requestId, const QString &code, const QString &message);
    void emitProfileFailure(const QString &requestId, const QString &code,
                            const QString &message);

    TcpJsonClient *client_;
    QHash<QString, PendingOperation> pendingOperations_;
    quint64 sessionGeneration_ = 0;
    std::optional<ev::user::User> user_;
    QHash<QString, QHash<qint64, qint64>> stationSnapshots_;
    QString token_;
    QString profileRequestId_;
    bool profileOutcomeUncertain_ = false;
};
