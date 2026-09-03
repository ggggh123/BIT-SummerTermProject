#include "ui/ChargePage.h"

#include "domain/Formatters.h"
#include "services/UserApi.h"

#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString kUncertain = QStringLiteral("结果未确认，需要刷新");

QString meterText(const ev::user::Order &order)
{
    return QStringLiteral("已充电 %1 秒 · 电量 %2 kWh · 金额 %3 元")
        .arg(order.elapsedSec)
        .arg(order.energyKwh, 0, 'f', 3)
        .arg(formatFen(order.amountFen));
}

} // namespace

ChargePage::ChargePage(UserApi *api, QWidget *parent)
    : QWidget(parent)
    , api_(api)
    , status_(new QLabel(this))
    , meter_(new QLabel(this))
    , summary_(new QLabel(this))
    , error_(new QLabel(this))
    , reserveButton_(new QPushButton(QStringLiteral("预约充电桩"), this))
    , startButton_(new QPushButton(QStringLiteral("开始充电"), this))
    , cancelButton_(new QPushButton(QStringLiteral("取消预约"), this))
    , stopButton_(new QPushButton(QStringLiteral("停止充电"), this))
    , settleButton_(new QPushButton(QStringLiteral("结算"), this))
    , backButton_(new QPushButton(QStringLiteral("返回充电站"), this))
    , retryButton_(new QPushButton(QStringLiteral("刷新订单"), this))
    , pollTimer_(new QTimer(this))
{
    Q_ASSERT(api_ != nullptr);
    setObjectName(QStringLiteral("chargePage"));
    status_->setObjectName(QStringLiteral("chargeStatus"));
    meter_->setObjectName(QStringLiteral("chargeMeter"));
    summary_->setObjectName(QStringLiteral("chargeSummary"));
    error_->setObjectName(QStringLiteral("chargeError"));
    reserveButton_->setObjectName(QStringLiteral("chargeReserveButton"));
    startButton_->setObjectName(QStringLiteral("chargeStartButton"));
    cancelButton_->setObjectName(QStringLiteral("chargeCancelButton"));
    stopButton_->setObjectName(QStringLiteral("chargeStopButton"));
    settleButton_->setObjectName(QStringLiteral("chargeSettleButton"));
    backButton_->setObjectName(QStringLiteral("chargeBackButton"));
    retryButton_->setObjectName(QStringLiteral("chargeRetryButton"));
    status_->setWordWrap(true);
    meter_->setWordWrap(true);
    summary_->setWordWrap(true);
    error_->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(status_);
    layout->addWidget(meter_);
    layout->addWidget(summary_);
    layout->addWidget(error_);
    layout->addWidget(reserveButton_);
    layout->addWidget(startButton_);
    layout->addWidget(cancelButton_);
    layout->addWidget(stopButton_);
    layout->addWidget(settleButton_);
    layout->addWidget(retryButton_);
    layout->addWidget(backButton_);
    layout->addStretch();

    pollTimer_->setInterval(2'000);
    pollTimer_->setSingleShot(false);
    connect(pollTimer_, &QTimer::timeout, this, &ChargePage::requestPoll);
    connect(reserveButton_, &QPushButton::clicked, this,
            [this] { beginMutation(ev::user::ChargeOperation::Reserve); });
    connect(startButton_, &QPushButton::clicked, this,
            [this] { beginMutation(ev::user::ChargeOperation::Start); });
    connect(cancelButton_, &QPushButton::clicked, this,
            [this] { beginMutation(ev::user::ChargeOperation::Cancel); });
    connect(stopButton_, &QPushButton::clicked, this,
            [this] { beginMutation(ev::user::ChargeOperation::Stop); });
    connect(settleButton_, &QPushButton::clicked, this,
            [this] { beginMutation(ev::user::ChargeOperation::Settle); });
    connect(backButton_, &QPushButton::clicked, this, [this] {
        leavePage();
        emit backRequested();
    });
    connect(retryButton_, &QPushButton::clicked, this, &ChargePage::requestReconciliation);
    connect(api_, &UserApi::connectionChanged, this, &ChargePage::setConnectionAvailable);
    connect(api_, &UserApi::sessionUserApplied, this,
            [this](const ev::user::User &, quint64 sessionGeneration, quint64) {
        const bool sessionChanged = sessionGeneration_ != 0
            && sessionGeneration_ != sessionGeneration;
        if (sessionChanged && pageActive_) {
            leavePage();
        }
        if (sessionChanged && pendingMutation_.has_value()) {
            pendingMutation_.reset();
            emit mutationPendingChanged(false);
        }
        if (sessionChanged) {
            reconciliationRequired_ = false;
            factsPending_ = false;
            factsFailed_ = false;
            factsGateRequired_ = false;
            exitRefreshRequired_ = false;
            exitRefreshFailed_ = false;
            exitRefreshAttemptId_ = 0;
            exitRefreshSelectionGeneration_ = 0;
            exitRefreshStationId_ = 0;
            selection_.reset();
            order_.reset();
            associatedCharger_.reset();
        }
        sessionGeneration_ = sessionGeneration;
        updateChargeFlowBlock();
    });
    connect(api_, &UserApi::currentOrderLoaded, this, &ChargePage::handleCurrentOrder);
    connect(api_, &UserApi::chargeOrderChanged, this,
            [this](const ev::user::RequestContext &context, const ev::user::Order &order) {
        acceptMutation(context, order);
    });
    connect(api_, &UserApi::chargeSettled, this,
            [this](const ev::user::RequestContext &context, const ev::user::Order &order, qint64) {
        acceptMutation(context, order);
    });
    connect(api_, &UserApi::chargeRequestFailed, this, &ChargePage::handleChargeFailure);
    connect(api_, &UserApi::stationDetailLoaded, this,
            [this](const QString &requestId, const ev::user::StationDetailResult &result) {
        if (requestId != pendingFactsRequestId_
            || pendingFactsPageGeneration_ != pageGeneration_
            || pendingFactsReadEpoch_ != readEpoch_
            || result.station.stationId != pendingFactsStationId_) {
            return;
        }
        pendingFactsRequestId_.clear();
        factsPending_ = false;
        std::optional<ev::user::Charger> matched;
        for (const auto &charger : result.chargers) {
            if (charger.chargerId == pendingFactsChargerId_) {
                matched = charger;
                break;
            }
        }
        if (order_.has_value()) {
            associatedCharger_ = matched;
        } else if (selection_.has_value() && matched.has_value()) {
            selection_->station = result.station;
            selection_->charger = *matched;
            associatedCharger_ = matched;
        }
        if (!matched.has_value()) {
            error_->setText(QStringLiteral("服务器返回的充电桩信息不匹配"));
            const bool mustGate = factsGateRequired_ || exitRefreshRequired_;
            factsFailed_ = mustGate;
            factsGateRequired_ = mustGate;
            reconciliationRequired_ = mustGate;
        } else {
            if (!exitRefreshFailed_) {
                factsFailed_ = false;
                if (factsGateRequired_) {
                    reconciliationRequired_ = false;
                }
                error_->clear();
            }
            factsGateRequired_ = false;
            if (exitRefreshRequired_) {
                emit nearbyDetailRefreshReady(result, exitRefreshSelectionGeneration_,
                                              exitRefreshAttemptId_);
            }
        }
        updateChargeFlowBlock();
        render();
    });
    connect(api_, &UserApi::requestFailed, this, &ChargePage::handleGeneralFailure);
    render();
}

void ChargePage::beginPage(quint64 selectionGeneration)
{
    ++pageGeneration_;
    selectionGeneration_ = selectionGeneration;
    invalidateSafeReads();
    pageActive_ = true;
    reconciliationRequired_ = false;
    factsFailed_ = false;
    factsGateRequired_ = false;
    exitRefreshRequired_ = false;
    exitRefreshFailed_ = false;
    exitRefreshAttemptId_ = 0;
    exitRefreshSelectionGeneration_ = 0;
    exitRefreshStationId_ = 0;
    reconciledNoOrder_ = false;
    pollTimer_->stop();
    error_->clear();
}

void ChargePage::enterSelection(const ev::user::StationSelection &selection)
{
    if (chargeFlowBlocked()) {
        return;
    }
    beginPage(selection.selectionGeneration);
    selection_ = selection;
    order_.reset();
    associatedCharger_ = selection.charger;
    render();
}

void ChargePage::enterOrder(
    const ev::user::Order &order,
    std::optional<ev::user::StationSelection> rememberedSelection)
{
    if (rememberedSelection.has_value() && !matchingSelection(*rememberedSelection, order)) {
        rememberedSelection.reset();
    }
    beginPage(0);
    selection_ = std::move(rememberedSelection);
    order_ = order;
    associatedCharger_.reset();
    if (selection_.has_value()) {
        associatedCharger_ = selection_->charger;
    }
    render();
}

void ChargePage::enterGuardOrder(
    const ev::user::Order &order,
    std::optional<ev::user::StationSelection> rememberedSelection)
{
    enterOrder(order, std::move(rememberedSelection));
    requestFacts(true);
}

void ChargePage::setNearbyRefreshAvailable(bool available)
{
    nearbyRefreshAvailable_ = available;
}

void ChargePage::observeAuthoritativeCurrent(const ev::user::CurrentOrderResult &result)
{
    if (!pageActive_ || !result.order.has_value() || !selection_.has_value()
        || matchingSelection(*selection_, *result.order)) {
        return;
    }
    selection_.reset();
    selectionGeneration_ = 0;
    associatedCharger_.reset();
    emit rememberedSelectionInvalidated();
    render();
}

void ChargePage::resume()
{
    if (!order_.has_value()) {
        return;
    }
    beginPage(0);
    render();
    requestReconciliation();
}

void ChargePage::leavePage()
{
    if (!pageActive_) {
        return;
    }
    pageActive_ = false;
    ++pageGeneration_;
    invalidateSafeReads();
    pollTimer_->stop();
    render();
}

void ChargePage::invalidateSelection(quint64 selectionGeneration)
{
    if (chargeFlowBlocked()) {
        return;
    }
    if (!selection_.has_value()
        || selection_->selectionGeneration >= selectionGeneration) {
        return;
    }
    selection_.reset();
    if (!order_.has_value()) {
        selectionGeneration_ = selectionGeneration;
        associatedCharger_.reset();
        invalidateSafeReads();
        reconciledNoOrder_ = true;
        reconciliationRequired_ = false;
        factsFailed_ = false;
    } else {
        selectionGeneration_ = 0;
    }
    render();
}

void ChargePage::setConnectionAvailable(bool available)
{
    const bool reconnected = !connected_ && available;
    connected_ = available;
    if (!connected_) {
        pollTimer_->stop();
        if (pageActive_) {
            invalidateSafeReads();
            reconciliationRequired_ = true;
            factsFailed_ = false;
            error_->setText(QStringLiteral("服务器连接不可用，请连接后刷新订单"));
        }
    } else if (reconnected && pageActive_) {
        requestReconciliation();
    }
    updateChargeFlowBlock();
    render();
}

void ChargePage::render()
{
    meter_->setVisible(false);
    summary_->setVisible(false);
    reserveButton_->setEnabled(false);
    startButton_->setEnabled(false);
    cancelButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    settleButton_->setEnabled(false);
    backButton_->setEnabled(false);
    retryButton_->setVisible(reconciliationRequired_ || factsFailed_ || !connected_);
    retryButton_->setEnabled(connected_ && pageActive_ && !pendingRead_.has_value()
                             && !factsPending_ && !pendingMutation_.has_value());

    if (!pageActive_) {
        status_->setText(QStringLiteral("充电页面已离开"));
        return;
    }
    const bool mutableNow = connected_ && !pendingMutation_.has_value()
        && !reconciliationRequired_ && !factsFailed_
        && !(factsPending_ && factsGateRequired_);
    if (!order_.has_value()) {
        status_->setText(QStringLiteral("尚未预约"));
        if (selection_.has_value()) {
            status_->setText(QStringLiteral("已选择 %1 · %2")
                                 .arg(selection_->station.name, selection_->charger.code));
            reserveButton_->setEnabled(
                mutableNow && selection_->charger.status == QStringLiteral("idle"));
        } else if (reconciledNoOrder_) {
            status_->setText(QStringLiteral("当前无未完成订单"));
            backButton_->setEnabled(!chargeFlowBlocked());
        }
        updatePolling();
        return;
    }
    const ev::user::Order &order = *order_;
    if (order.status == QStringLiteral("reserved")) {
        status_->setText(QStringLiteral("已预约"));
        const bool associatedReserved = associatedCharger_.has_value()
            && associatedCharger_->chargerId == order.chargerId
            && associatedCharger_->status == QStringLiteral("reserved");
        startButton_->setEnabled(mutableNow && associatedReserved);
        cancelButton_->setEnabled(mutableNow && associatedReserved);
    } else if (order.status == QStringLiteral("charging") && order.endedAt.isEmpty()) {
        status_->setText(QStringLiteral("充电中"));
        meter_->setText(meterText(order));
        meter_->setVisible(true);
        stopButton_->setEnabled(mutableNow);
    } else if (order.status == QStringLiteral("charging")) {
        status_->setText(QStringLiteral("充电已停止，等待结算"));
        summary_->setText(QStringLiteral("最终数据：%1 · 停止时间 %2")
                              .arg(meterText(order), order.endedAt));
        summary_->setVisible(true);
        settleButton_->setEnabled(mutableNow);
    } else if (order.status == QStringLiteral("completed")) {
        status_->setText(QStringLiteral("充电完成，结算成功"));
        summary_->setText(QStringLiteral("结算结果：%1").arg(meterText(order)));
        summary_->setVisible(true);
        backButton_->setEnabled(!chargeFlowBlocked());
    } else if (order.status == QStringLiteral("cancelled")) {
        status_->setText(QStringLiteral("预约已取消"));
        summary_->setText(QStringLiteral("订单 %1 · 取消时间 %2")
                              .arg(order.orderId).arg(order.endedAt));
        summary_->setVisible(true);
        backButton_->setEnabled(!chargeFlowBlocked());
    }
    updatePolling();
}

void ChargePage::updatePolling()
{
    const bool shouldPoll = connected_ && pageActive_ && !reconciliationRequired_
        && !pendingMutation_.has_value() && order_.has_value()
        && order_->status == QStringLiteral("charging") && order_->endedAt.isEmpty();
    if (shouldPoll) {
        if (!pollTimer_->isActive()) {
            pollTimer_->start();
        }
    } else {
        pollTimer_->stop();
    }
}

void ChargePage::requestPoll()
{
    if (pendingRead_.has_value() || !pageActive_ || !order_.has_value()
        || order_->status != QStringLiteral("charging") || !order_->endedAt.isEmpty()) {
        return;
    }
    pendingRead_ = api_->loadCurrentOrder(
        pageGeneration_, selectionGeneration_, ev::user::ChargeOperation::Poll);
    if (pendingRead_->requestId.isEmpty()) {
        pendingRead_.reset();
    }
}

void ChargePage::requestReconciliation()
{
    if (!connected_ || !pageActive_ || pendingRead_.has_value()
        || pendingMutation_.has_value()) {
        return;
    }
    pollTimer_->stop();
    reconciliationRequired_ = true;
    factsFailed_ = false;
    exitRefreshFailed_ = false;
    invalidateSafeReads();
    pendingRead_ = api_->loadCurrentOrder(
        pageGeneration_, selectionGeneration_, ev::user::ChargeOperation::Reconcile);
    if (pendingRead_->requestId.isEmpty()) {
        pendingRead_.reset();
    }
    updateChargeFlowBlock();
    render();
}

void ChargePage::requestFacts(bool gateActions, bool requireNearbyCommit)
{
    if (!pageActive_ || (!order_.has_value() && !selection_.has_value())) {
        return;
    }
    const qint64 stationId = order_.has_value()
        ? order_->stationId : selection_->station.stationId;
    const qint64 chargerId = order_.has_value()
        ? order_->chargerId : selection_->charger.chargerId;
    const bool originMatches = selection_.has_value()
        && ((!order_.has_value()) || matchingSelection(*selection_, *order_));
    if (requireNearbyCommit && !originMatches) {
        exitRefreshRequired_ = false;
        exitRefreshAttemptId_ = 0;
        exitRefreshSelectionGeneration_ = 0;
        exitRefreshStationId_ = 0;
    } else if (requireNearbyCommit) {
        exitRefreshAttemptId_ = ++nextExitRefreshAttemptId_;
        exitRefreshSelectionGeneration_ = selection_->selectionGeneration;
        exitRefreshStationId_ = stationId;
    }
    if (!pendingFactsRequestId_.isEmpty()) {
        api_->cancelSafeRead(pendingFactsRequestId_);
    }
    factsPending_ = true;
    factsFailed_ = false;
    factsGateRequired_ = gateActions;
    pendingFactsPageGeneration_ = pageGeneration_;
    pendingFactsReadEpoch_ = readEpoch_;
    pendingFactsStationId_ = stationId;
    pendingFactsChargerId_ = chargerId;
    pendingFactsRequestId_ = api_->loadStationDetail(stationId);
    if (pendingFactsRequestId_.isEmpty()) {
        factsPending_ = false;
        const bool mustGate = gateActions || requireNearbyCommit;
        factsFailed_ = mustGate;
        factsGateRequired_ = mustGate;
        reconciliationRequired_ = mustGate;
        error_->setText(QStringLiteral("充电站信息加载失败，请刷新订单"));
    }
    if (originMatches) {
        emit nearbyRefreshRequested(selection_->origin, stationId,
                                    selection_->selectionGeneration,
                                    requireNearbyCommit ? exitRefreshAttemptId_ : 0);
    }
    updateChargeFlowBlock();
    render();
}

void ChargePage::invalidateSafeReads()
{
    readEpoch_ = api_->invalidateChargeReads();
    pendingRead_.reset();
    if (!pendingFactsRequestId_.isEmpty()) {
        api_->cancelSafeRead(pendingFactsRequestId_);
    }
    pendingFactsRequestId_.clear();
    factsPending_ = false;
    factsGateRequired_ = false;
    if (exitRefreshRequired_) {
        exitRefreshAttemptId_ = 0;
    }
    emit chargeSafeReadsInvalidated();
}

void ChargePage::beginMutation(ev::user::ChargeOperation operation)
{
    if (!pageActive_ || !connected_ || chargeFlowBlocked()) {
        return;
    }
    if (operation == ev::user::ChargeOperation::Reserve && selection_.has_value()) {
        pendingMutation_ = api_->reserveCharger(
            selection_->charger.chargerId, pageGeneration_, selectionGeneration_);
    } else if (order_.has_value()) {
        if (operation == ev::user::ChargeOperation::Start) {
            pendingMutation_ = api_->startCharging(
                order_->orderId, pageGeneration_, selectionGeneration_);
        } else if (operation == ev::user::ChargeOperation::Stop) {
            pendingMutation_ = api_->stopCharging(
                order_->orderId, pageGeneration_, selectionGeneration_);
        } else if (operation == ev::user::ChargeOperation::Settle) {
            pendingMutation_ = api_->settleCharging(
                order_->orderId, pageGeneration_, selectionGeneration_);
        } else if (operation == ev::user::ChargeOperation::Cancel) {
            pendingMutation_ = api_->cancelOrder(
                order_->orderId, pageGeneration_, selectionGeneration_);
        }
    }
    if (pendingMutation_.has_value() && pendingMutation_->requestId.isEmpty()) {
        pendingMutation_.reset();
    } else if (pendingMutation_.has_value()) {
        emit mutationPendingChanged(true);
    }
    updateChargeFlowBlock();
    render();
}

void ChargePage::acceptMutation(const ev::user::RequestContext &context,
                                const ev::user::Order &order)
{
    if (!pendingMutation_.has_value() || context != *pendingMutation_) {
        return;
    }
    const bool updatePage = matchesMutationPage(context);
    if (!updatePage) {
        pendingMutation_.reset();
        emit mutationPendingChanged(false);
        updateChargeFlowBlock();
        render();
        return;
    }
    invalidateSafeReads();
    pollTimer_->stop();
    readEpoch_ = api_->currentChargeReadEpoch();
    reconciliationRequired_ = false;
    error_->clear();
    order_ = order;
    reconciledNoOrder_ = false;
    selectionGeneration_ = 0;
    associatedCharger_.reset();
    const bool terminal = order.status == QStringLiteral("completed")
        || order.status == QStringLiteral("cancelled");
    const bool canRefreshNearby = terminal && nearbyRefreshAvailable_
        && selection_.has_value()
        && matchingSelection(*selection_, order);
    exitRefreshRequired_ = canRefreshNearby;
    exitRefreshFailed_ = false;
    exitRefreshAttemptId_ = 0;
    exitRefreshSelectionGeneration_ = canRefreshNearby ? selection_->selectionGeneration : 0;
    exitRefreshStationId_ = canRefreshNearby ? order.stationId : 0;
    reconciliationRequired_ = canRefreshNearby;
    const bool active = order.status == QStringLiteral("reserved")
        || order.status == QStringLiteral("charging");
    emit activeOrderResolved(active);
    pendingMutation_.reset();
    emit mutationPendingChanged(false);
    updateChargeFlowBlock();
    render();
    requestFacts(canRefreshNearby, canRefreshNearby);
}

void ChargePage::handleCurrentOrder(const ev::user::RequestContext &context,
                                    const ev::user::CurrentOrderResult &result)
{
    if (!pendingRead_.has_value() || context != *pendingRead_
        || !matchesPage(context) || context.readEpoch != readEpoch_) {
        return;
    }
    const bool poll = context.operation == ev::user::ChargeOperation::Poll;
    const bool hardReconciliation = !poll && reconciliationRequired_;
    pendingRead_.reset();
    if (poll && order_.has_value() && result.order.has_value()
        && result.order->orderId != order_->orderId) {
        return;
    }
    const bool keepTerminalPresentation = !result.order.has_value()
        && exitRefreshRequired_ && order_.has_value()
        && (order_->status == QStringLiteral("completed")
            || order_->status == QStringLiteral("cancelled"));
    if (result.order.has_value()) {
        if (selection_.has_value() && !matchingSelection(*selection_, *result.order)) {
            selection_.reset();
            selectionGeneration_ = 0;
            exitRefreshRequired_ = false;
            exitRefreshAttemptId_ = 0;
            exitRefreshSelectionGeneration_ = 0;
            exitRefreshStationId_ = 0;
            emit rememberedSelectionInvalidated();
        }
        order_ = result.order;
    } else if (!keepTerminalPresentation) {
        order_.reset();
    }
    reconciledNoOrder_ = !result.order.has_value();
    if (order_.has_value()) {
        selectionGeneration_ = 0;
    }
    associatedCharger_.reset();
    const bool hasFactsTarget = order_.has_value() || selection_.has_value();
    const bool factsRequired = (hardReconciliation && hasFactsTarget)
        || (order_.has_value() && order_->status == QStringLiteral("reserved"))
        || (!order_.has_value() && selection_.has_value());
    reconciliationRequired_ = factsRequired;
    factsFailed_ = false;
    error_->clear();
    emit activeOrderResolved(result.order.has_value());
    updateChargeFlowBlock();
    render();
    if (!poll && hasFactsTarget) {
        requestFacts(factsRequired, exitRefreshRequired_);
    }
}

void ChargePage::handleChargeFailure(const ev::user::RequestContext &context,
                                     const ev::user::ApiError &failure, bool uncertain)
{
    if (context.operation == ev::user::ChargeOperation::Poll
        || context.operation == ev::user::ChargeOperation::Reconcile
        || context.operation == ev::user::ChargeOperation::Guard) {
        if (!pendingRead_.has_value() || context != *pendingRead_ || !matchesPage(context)) {
            return;
        }
        pendingRead_.reset();
        reconciliationRequired_ = true;
        error_->setText(localizedError(failure));
        updateChargeFlowBlock();
        render();
        return;
    }
    if (!pendingMutation_.has_value() || context != *pendingMutation_) {
        return;
    }
    const bool updatePage = matchesMutationPage(context);
    reconciliationRequired_ = true;
    if (!updatePage) {
        pendingMutation_.reset();
        emit mutationPendingChanged(false);
        updateChargeFlowBlock();
        render();
        return;
    }
    invalidateSafeReads();
    pollTimer_->stop();
    readEpoch_ = api_->currentChargeReadEpoch();
    error_->setText(uncertain ? kUncertain : localizedError(failure));
    pendingMutation_.reset();
    emit mutationPendingChanged(false);
    updateChargeFlowBlock();
    render();
    if (connected_ && (uncertain || isBusinessRefreshError(failure.code))) {
        requestReconciliation();
    }
}

void ChargePage::handleGeneralFailure(const ev::user::ApiError &failure)
{
    if (failure.requestId.isEmpty() || failure.requestId != pendingFactsRequestId_) {
        return;
    }
    pendingFactsRequestId_.clear();
    factsPending_ = false;
    const bool mustGate = factsGateRequired_ || exitRefreshRequired_;
    factsFailed_ = mustGate;
    factsGateRequired_ = mustGate;
    reconciliationRequired_ = mustGate;
    error_->setText(localizedError(failure));
    updateChargeFlowBlock();
    render();
}

bool ChargePage::chargeFlowBlocked() const
{
    return pendingMutation_.has_value() || reconciliationRequired_ || factsFailed_
        || exitRefreshRequired_ || (factsPending_ && factsGateRequired_);
}

void ChargePage::nearbyRefreshCommitted(quint64 refreshAttemptId,
                                        quint64 selectionGeneration,
                                        qint64 stationId)
{
    if (!exitRefreshRequired_ || refreshAttemptId != exitRefreshAttemptId_
        || selectionGeneration != exitRefreshSelectionGeneration_
        || stationId != exitRefreshStationId_) {
        return;
    }
    exitRefreshRequired_ = false;
    exitRefreshFailed_ = false;
    exitRefreshAttemptId_ = 0;
    exitRefreshSelectionGeneration_ = 0;
    exitRefreshStationId_ = 0;
    updateChargeFlowBlock();
    render();
}

void ChargePage::nearbyRefreshFailed(quint64 refreshAttemptId,
                                     quint64 selectionGeneration,
                                     qint64 stationId,
                                     ev::user::ApiError failure)
{
    if (!exitRefreshRequired_ || refreshAttemptId != exitRefreshAttemptId_
        || selectionGeneration != exitRefreshSelectionGeneration_
        || stationId != exitRefreshStationId_) {
        return;
    }
    factsFailed_ = true;
    exitRefreshFailed_ = true;
    reconciliationRequired_ = true;
    error_->setText(localizedError(failure));
    updateChargeFlowBlock();
    render();
}

void ChargePage::nearbyRefreshUnavailable(quint64 refreshAttemptId,
                                          quint64 selectionGeneration,
                                          qint64 stationId)
{
    if (!exitRefreshRequired_ || refreshAttemptId != exitRefreshAttemptId_
        || selectionGeneration != exitRefreshSelectionGeneration_
        || stationId != exitRefreshStationId_) {
        return;
    }
    if (!pendingFactsRequestId_.isEmpty()) {
        api_->cancelSafeRead(pendingFactsRequestId_);
    }
    pendingFactsRequestId_.clear();
    factsPending_ = false;
    factsGateRequired_ = false;
    factsFailed_ = false;
    reconciliationRequired_ = !connected_;
    exitRefreshRequired_ = false;
    exitRefreshFailed_ = false;
    exitRefreshAttemptId_ = 0;
    exitRefreshSelectionGeneration_ = 0;
    exitRefreshStationId_ = 0;
    selection_.reset();
    selectionGeneration_ = 0;
    emit rememberedSelectionInvalidated();
    updateChargeFlowBlock();
    render();
}

void ChargePage::updateChargeFlowBlock()
{
    const bool blocked = chargeFlowBlocked();
    if (blocked == reportedChargeFlowBlocked_) {
        return;
    }
    reportedChargeFlowBlocked_ = blocked;
    emit chargeFlowBlockedChanged(blocked);
}

bool ChargePage::matchesPage(const ev::user::RequestContext &context) const
{
    return pageActive_ && context.pageGeneration == pageGeneration_
        && context.selectionGeneration == selectionGeneration_
        && (sessionGeneration_ == 0 || context.sessionGeneration == sessionGeneration_);
}

bool ChargePage::matchesMutationPage(const ev::user::RequestContext &context) const
{
    return pageActive_ && context.pageGeneration == pageGeneration_
        && (sessionGeneration_ == 0 || context.sessionGeneration == sessionGeneration_);
}

bool ChargePage::matchingSelection(const ev::user::StationSelection &selection,
                                   const ev::user::Order &order) const
{
    return selection.station.stationId == order.stationId
        && selection.charger.chargerId == order.chargerId;
}

QString ChargePage::localizedError(const ev::user::ApiError &failure)
{
    if (failure.code == QStringLiteral("USER_FROZEN")) {
        return QStringLiteral("账户已冻结，无法预约或开始充电");
    }
    if (failure.code == QStringLiteral("ACTIVE_ORDER_EXISTS")) {
        return QStringLiteral("已有未完成订单，请先处理当前订单");
    }
    if (failure.code == QStringLiteral("CHARGER_NOT_AVAILABLE")) {
        return QStringLiteral("充电桩当前不可用，请刷新后重试");
    }
    if (failure.code == QStringLiteral("ORDER_STATE_CONFLICT")) {
        return QStringLiteral("订单状态已变化，正在刷新");
    }
    if (failure.code == QStringLiteral("INSUFFICIENT_BALANCE")) {
        return QStringLiteral("余额不足，请充值后再结算");
    }
    if (failure.code == QStringLiteral("DB_BUSY")) {
        return QStringLiteral("服务繁忙，请稍后刷新重试");
    }
    if (failure.code == QStringLiteral("NOT_CONNECTED")) {
        return QStringLiteral("服务器连接不可用，请连接后刷新订单");
    }
    if (failure.code == QStringLiteral("TRANSPORT_ERROR")) {
        return QStringLiteral("服务器连接中断，请刷新订单");
    }
    if (failure.code == QStringLiteral("TIMEOUT")) {
        return QStringLiteral("服务器响应超时，请刷新订单");
    }
    if (failure.code == QStringLiteral("PROTOCOL_ERROR")) {
        return QStringLiteral("服务器通信异常，请刷新订单");
    }
    if (failure.code == QStringLiteral("INVALID_RESPONSE")) {
        return QStringLiteral("服务器返回的订单信息无效");
    }
    return QStringLiteral("订单操作失败，请刷新后重试");
}

bool ChargePage::isBusinessRefreshError(const QString &code)
{
    return code == QStringLiteral("USER_FROZEN")
        || code == QStringLiteral("ACTIVE_ORDER_EXISTS")
        || code == QStringLiteral("CHARGER_NOT_AVAILABLE")
        || code == QStringLiteral("ORDER_STATE_CONFLICT")
        || code == QStringLiteral("INSUFFICIENT_BALANCE")
        || code == QStringLiteral("DB_BUSY");
}
