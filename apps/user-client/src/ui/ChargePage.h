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
    void resume();
    void leavePage();

public slots:
    void setConnectionAvailable(bool available);

signals:
    void backRequested();
    void activeOrderResolved(bool active);
    void nearbyRefreshRequested(ev::user::GeoPoint origin, qint64 stationId,
                                quint64 selectionGeneration);

private:
    void beginPage(quint64 selectionGeneration);
    void render();
    void updatePolling();
    void requestPoll();
    void requestReconciliation();
    void requestFacts();
    void beginMutation(ev::user::ChargeOperation operation);
    void acceptMutation(const ev::user::RequestContext &context,
                        const ev::user::Order &order);
    void handleCurrentOrder(const ev::user::RequestContext &context,
                            const ev::user::CurrentOrderResult &result);
    void handleChargeFailure(const ev::user::RequestContext &context,
                             const ev::user::ApiError &error, bool uncertain);
    void handleGeneralFailure(const ev::user::ApiError &error);
    [[nodiscard]] bool matchesPage(const ev::user::RequestContext &context) const;
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
    bool reconciledNoOrder_ = false;
    quint64 pageGeneration_ = 0;
    quint64 sessionGeneration_ = 0;
    quint64 selectionGeneration_ = 0;
    quint64 readEpoch_ = 0;
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
