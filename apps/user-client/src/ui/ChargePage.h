#pragma once

#include "domain/Models.h"

#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QTimer;
class UserApi;

class ChargePage final : public QWidget
{
    Q_OBJECT

public:
    explicit ChargePage(UserApi *api, QWidget *parent = nullptr);

    void enterSelection(const ev::user::StationSelection &selection);
    void enterOrder(const ev::user::Order &order,
                    std::optional<ev::user::StationSelection> rememberedSelection = std::nullopt);
    void enterGuardOrder(const ev::user::Order &order,
                         std::optional<ev::user::StationSelection> rememberedSelection = std::nullopt);
    void setNearbyRefreshAvailable(bool available);
    void resume();
    void leavePage();
    void invalidateSelection(quint64 selectionGeneration);

public slots:
    void setConnectionAvailable(bool available);
    void observeAuthoritativeCurrent(const ev::user::RequestContext &context,
                                     const ev::user::CurrentOrderResult &result);
    void nearbyRefreshCommitted(quint64 refreshAttemptId, quint64 selectionGeneration,
                                qint64 stationId);
    void nearbyRefreshFailed(quint64 refreshAttemptId, quint64 selectionGeneration,
                             qint64 stationId, ev::user::ApiError error);
    void nearbyRefreshUnavailable(quint64 refreshAttemptId, quint64 selectionGeneration,
                                  qint64 stationId);

signals:
    void backRequested(qint64 presentedOrderId);
    void currentAuthorityObserved(ev::user::RequestContext context,
                                  ev::user::CurrentOrderResult result,
                                  bool pageOwned, qint64 resolvedOwnerOrderId);
    void mutationDispatched(ev::user::RequestContext context, qint64 subjectOrderId);
    void mutationAuthorityObserved(ev::user::RequestContext context,
                                   ev::user::Order order);
    void mutationFinished(ev::user::RequestContext context);
    void nearbyRefreshRequested(ev::user::GeoPoint origin, qint64 stationId,
                                quint64 selectionGeneration, quint64 refreshAttemptId);
    void nearbyDetailRefreshReady(ev::user::StationDetailResult result,
                                  quint64 selectionGeneration, quint64 refreshAttemptId);
    void nearbyDetailRefreshFailed(quint64 refreshAttemptId, quint64 selectionGeneration,
                                   qint64 stationId, ev::user::ApiError error);
    void rememberedSelectionInvalidated();
    void mutationPendingChanged(bool pending);
    void chargeFlowBlockedChanged(bool blocked);
    void chargeSafeReadsInvalidated();

private:
    void beginPage(quint64 selectionGeneration);
    void render();
    void updatePolling();
    void requestPoll();
    void requestReconciliation();
    void requestFacts(bool gateActions = false, bool requireNearbyCommit = false);
    void invalidateSafeReads();
    void adoptMutationReadEpoch();
    void beginMutation(ev::user::ChargeOperation operation);
    void acceptMutation(const ev::user::RequestContext &context,
                        const ev::user::Order &order);
    void handleCurrentOrder(const ev::user::RequestContext &context,
                            const ev::user::CurrentOrderResult &result);
    void handleChargeFailure(const ev::user::RequestContext &context,
                             const ev::user::ApiError &error, bool uncertain);
    void handleGeneralFailure(const ev::user::ApiError &error);
    void updateChargeFlowBlock();
    [[nodiscard]] bool chargeFlowBlocked() const;
    [[nodiscard]] bool matchesPage(const ev::user::RequestContext &context) const;
    [[nodiscard]] bool matchesMutationPage(const ev::user::RequestContext &context) const;
    [[nodiscard]] bool matchingSelection(const ev::user::StationSelection &selection,
                                         const ev::user::Order &order) const;
    [[nodiscard]] static QString localizedError(const ev::user::ApiError &error);
    [[nodiscard]] static bool isBusinessRefreshError(const QString &code);

    UserApi *api_;
    QLabel *status_;
    QLabel *meter_;
    QLabel *summary_;
    QLabel *error_;
    QPushButton *reserveButton_;
    QPushButton *startButton_;
    QPushButton *cancelButton_;
    QPushButton *stopButton_;
    QPushButton *settleButton_;
    QPushButton *backButton_;
    QPushButton *retryButton_;
    QTimer *pollTimer_;
    bool connected_ = false;
    bool pageActive_ = false;
    bool reconciliationRequired_ = false;
    bool factsPending_ = false;
    bool factsFailed_ = false;
    bool factsGateRequired_ = false;
    bool reconciledNoOrder_ = false;
    bool reportedChargeFlowBlocked_ = false;
    bool exitRefreshRequired_ = false;
    bool exitRefreshFailed_ = false;
    bool nearbyRefreshAvailable_ = false;
    quint64 nextExitRefreshAttemptId_ = 0;
    quint64 exitRefreshAttemptId_ = 0;
    quint64 exitRefreshSelectionGeneration_ = 0;
    qint64 exitRefreshStationId_ = 0;
    quint64 pageGeneration_ = 0;
    quint64 sessionGeneration_ = 0;
    quint64 selectionGeneration_ = 0;
    quint64 readEpoch_ = 0;
    qint64 pendingReadOwnerOrderId_ = 0;
    qint64 pendingMutationSubjectOrderId_ = 0;
    bool pendingMutationSuperseded_ = false;
    std::optional<ev::user::StationSelection> selection_;
    std::optional<ev::user::Order> order_;
    std::optional<ev::user::Charger> associatedCharger_;
    std::optional<ev::user::RequestContext> pendingMutation_;
    std::optional<ev::user::RequestContext> pendingRead_;
    QString pendingFactsRequestId_;
    quint64 pendingFactsPageGeneration_ = 0;
    quint64 pendingFactsReadEpoch_ = 0;
    qint64 pendingFactsStationId_ = 0;
    qint64 pendingFactsChargerId_ = 0;
};
