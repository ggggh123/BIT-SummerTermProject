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

    [[nodiscard]] QString loadSystemHealth();
    void loginByPhone(const QString &mobile);
    [[nodiscard]] ev::user::RequestContext loadCurrentOrder(
        quint64 pageGeneration = 0, quint64 selectionGeneration = 0,
        ev::user::ChargeOperation operation = ev::user::ChargeOperation::Guard);
    [[nodiscard]] ev::user::RequestContext reserveCharger(
        qint64 chargerId, quint64 pageGeneration, quint64 selectionGeneration);
    [[nodiscard]] ev::user::RequestContext startCharging(
        qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration);
    [[nodiscard]] ev::user::RequestContext stopCharging(
        qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration);
    [[nodiscard]] ev::user::RequestContext settleCharging(
        qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration);
    [[nodiscard]] ev::user::RequestContext cancelOrder(
        qint64 orderId, quint64 pageGeneration, quint64 selectionGeneration);
    [[nodiscard]] quint64 invalidateChargeReads();
    [[nodiscard]] quint64 currentChargeReadEpoch() const;
    void cancelSafeRead(const QString &requestId);
    [[nodiscard]] QString loadNearbyStations(const ev::user::GeoPoint &origin);
    [[nodiscard]] QString loadStationDetail(qint64 stationId);
    [[nodiscard]] QString loadChargers(qint64 stationId);
    [[nodiscard]] QString loadLatestForecast(const QString &stationListRequestId);
    [[nodiscard]] ev::user::HistoryRequestContext loadOrderHistory(
        qint64 limit, qint64 offset, quint64 pageGeneration, quint64 readEpoch);
    void retryConnection();
    [[nodiscard]] QString loadProfile();
    [[nodiscard]] QString updateNickname(const QString &nickname);
    [[nodiscard]] QString rechargeWallet(const QString &amount);
    [[nodiscard]] std::optional<ev::user::User> sessionUser() const;
    [[nodiscard]] bool profileNeedsReconciliation() const;

signals:
    void systemHealthLoaded(QString requestId, ev::user::SystemHealthResult result);
    void chargerListLoaded(QString requestId, ev::user::ChargerListResult result);
    void loginSucceeded(ev::user::User user);
    void sessionExpired(quint64 sessionGeneration);
    void currentOrderLoaded(ev::user::RequestContext context,
                            ev::user::CurrentOrderResult result);
    void chargeOrderChanged(ev::user::RequestContext context, ev::user::Order order);
    void chargeSettled(ev::user::RequestContext context, ev::user::Order order,
                       qint64 balanceFen);
    void chargeRequestFailed(ev::user::RequestContext context, ev::user::ApiError error,
                             bool uncertain);
    void nearbyStationsLoaded(QString requestId, ev::user::StationListResult result);
    void stationDetailLoaded(QString requestId, ev::user::StationDetailResult result);
    void latestForecastLoaded(QString requestId, ev::user::ForecastLatestResult result);
    void orderHistoryLoaded(ev::user::HistoryRequestContext context,
                            ev::user::OrderListResult result);
    void orderHistoryRequestFailed(ev::user::HistoryRequestContext context,
                                   ev::user::ApiError error);
    void sessionUserApplied(ev::user::User user, quint64 sessionGeneration, quint64 revision);
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
        Health,
        Login,
        NearbyStations,
        StationDetail,
        ChargerList,
        LatestForecast,
        HistoryList,
        ProfileGet,
        ProfileUpdate,
        ProfileRecharge,
        ChargeCurrent,
        ChargeReserve,
        ChargeStart,
        ChargeStop,
        ChargeSettle,
        ChargeCancel,
    };

    struct PendingOperation {
        Operation operation;
        quint64 sessionGeneration = 0;
        std::optional<ev::user::GeoPoint> origin;
        qint64 stationId = 0;
        QHash<qint64, qint64> forecastStationCounts;
        bool reconciliation = false;
        quint64 userRevision = 0;
        std::optional<ev::user::RequestContext> chargeContext;
        std::optional<ev::user::HistoryRequestContext> historyContext;
        qint64 expectedEntityId = 0;
    };

    [[nodiscard]] QString loadProfile(bool reconciliation);
    [[nodiscard]] bool profileOperationPending() const;
    static bool isProfileOperation(Operation operation);
    static bool isProfileMutation(Operation operation);
    static bool isChargeOperation(Operation operation);
    static bool isChargeMutation(Operation operation);
    [[nodiscard]] ev::user::RequestContext sendChargeRequest(
        Operation operation, ev::user::ChargeOperation publicOperation, const QString &action,
        const QJsonObject &payload, qint64 expectedEntityId, quint64 pageGeneration,
        quint64 selectionGeneration);
    void finishChargeFailure(const PendingOperation &pending, const QString &requestId,
                             const QString &code, const QString &message, bool uncertain);
    void applySessionUser(ev::user::User user);
    void finishProfileOperation(Operation operation);
    void markProfileUncertain();
    void expireAuthenticatedSession();
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
    quint64 userRevision_ = 0;
    quint64 chargeReadEpoch_ = 0;
    std::optional<ev::user::User> user_;
    QHash<QString, QHash<qint64, qint64>> stationSnapshots_;
    QString token_;
    QString profileRequestId_;
    bool profileOutcomeUncertain_ = false;
};
