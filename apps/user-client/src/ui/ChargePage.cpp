#include "ui/ChargePage.h"

#include "domain/Formatters.h"
#include "services/UserApi.h"

#include <QLabel>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString kUncertain = QStringLiteral("结果未确认，需要刷新");

QString durationText(qint64 elapsedSec)
{
    return QStringLiteral("%1:%2")
        .arg(elapsedSec / 60, 2, 10, QLatin1Char('0'))
        .arg(elapsedSec % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

ChargePage::ChargePage(UserApi *api, QWidget *parent)
    : QWidget(parent)
    , api_(api)
    , title_(new QLabel(QStringLiteral("当前订单"), this))
    , progress_(new QLabel(this))
    , status_(new QLabel(this))
    , identity_(new QLabel(this))
    , metrics_(new QWidget(this))
    , metricCaption_(new QLabel(this))
    , meter_(new QLabel(this))
    , metricUnit_(new QLabel(this))
    , duration_(new QLabel(this))
    , secondaryCaption_(new QLabel(this))
    , secondaryMetric_(new QLabel(this))
    , notice_(new QLabel(this))
    , summary_(new QLabel(this))
    , error_(new QLabel(this))
    , reserveButton_(new QPushButton(QStringLiteral("预约充电桩"), this))
    , startButton_(new QPushButton(QStringLiteral("开始充电"), this))
    , cancelButton_(new QPushButton(QStringLiteral("取消预约"), this))
    , stopButton_(new QPushButton(QStringLiteral("停止充电"), this))
    , settleButton_(new QPushButton(QStringLiteral("确认结算"), this))
    , backButton_(new QPushButton(QStringLiteral("返回充电站"), this))
    , retryButton_(new QPushButton(QStringLiteral("刷新订单"), this))
    , pollTimer_(new QTimer(this))
{
    Q_ASSERT(api_ != nullptr);
    setObjectName(QStringLiteral("chargePage"));
    title_->setObjectName(QStringLiteral("chargeTitle"));
    progress_->setObjectName(QStringLiteral("chargeProgress"));
    status_->setObjectName(QStringLiteral("chargeStatus"));
    identity_->setObjectName(QStringLiteral("chargeOrderIdentity"));
    metrics_->setObjectName(QStringLiteral("chargeMetrics"));
    metricCaption_->setObjectName(QStringLiteral("chargeMetricCaption"));
    meter_->setObjectName(QStringLiteral("chargeMeter"));
    metricUnit_->setObjectName(QStringLiteral("chargeMetricUnit"));
    duration_->setObjectName(QStringLiteral("chargeDuration"));
    secondaryMetric_->setObjectName(QStringLiteral("chargeSecondaryMetric"));
    notice_->setObjectName(QStringLiteral("chargeNotice"));
    summary_->setObjectName(QStringLiteral("chargeSummary"));
    error_->setObjectName(QStringLiteral("chargeError"));
    reserveButton_->setObjectName(QStringLiteral("chargeReserveButton"));
    startButton_->setObjectName(QStringLiteral("chargeStartButton"));
    cancelButton_->setObjectName(QStringLiteral("chargeCancelButton"));
    stopButton_->setObjectName(QStringLiteral("chargeStopButton"));
    settleButton_->setObjectName(QStringLiteral("chargeSettleButton"));
    backButton_->setObjectName(QStringLiteral("chargeBackButton"));
    retryButton_->setObjectName(QStringLiteral("chargeRetryButton"));
    for (QLabel *label : {title_, progress_, status_, identity_, metricCaption_, meter_,
                         metricUnit_, duration_, secondaryCaption_, secondaryMetric_, notice_,
                         summary_, error_}) {
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    title_->setProperty("role", QStringLiteral("pageTitle"));
    status_->setProperty("role", QStringLiteral("sectionTitle"));
    for (QLabel *label : {progress_, identity_, metricCaption_, secondaryCaption_, summary_})
        label->setProperty("role", QStringLiteral("secondary"));
    meter_->setProperty("role", QStringLiteral("chargeMetric"));
    duration_->setProperty("role", QStringLiteral("chargeSubMetric"));
    secondaryMetric_->setProperty("role", QStringLiteral("chargeSubMetric"));
    notice_->setProperty("role", QStringLiteral("chargeNotice"));
    error_->setProperty("role", QStringLiteral("danger"));
    for (QPushButton *button : {reserveButton_, startButton_, settleButton_, backButton_})
        button->setProperty("role", QStringLiteral("primary"));
    stopButton_->setProperty("role", QStringLiteral("danger"));
    cancelButton_->setProperty("role", QStringLiteral("textAction"));
    retryButton_->setProperty("role", QStringLiteral("outline"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    layout->addWidget(title_);
    layout->addWidget(progress_);
    auto *card = new QFrame(this);
    card->setProperty("role", QStringLiteral("card"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *icon = new QLabel(card);
    icon->setPixmap(QIcon(QStringLiteral(":/ui/charger.svg")).pixmap(40, 40));
    icon->setFixedSize(44, 44);
    header->addWidget(icon);
    header->addWidget(status_, 1);
    cardLayout->addLayout(header);
    cardLayout->addWidget(identity_);
    auto *metricLayout = new QVBoxLayout(metrics_);
    metricLayout->setContentsMargins(0, 8, 0, 8);
    metricLayout->setSpacing(8);
    metricCaption_->setAlignment(Qt::AlignCenter);
    meter_->setAlignment(Qt::AlignCenter);
    metricUnit_->setAlignment(Qt::AlignCenter);
    metricLayout->addWidget(metricCaption_);
    metricLayout->addWidget(meter_);
    metricLayout->addWidget(metricUnit_);
    auto *secondary = new QHBoxLayout;
    secondary->setSpacing(16);
    auto *durationGroup = new QVBoxLayout;
    auto *durationCaption = new QLabel(QStringLiteral("充电时长 · 分:秒"), metrics_);
    durationCaption->setProperty("role", QStringLiteral("secondary"));
    durationCaption->setAlignment(Qt::AlignCenter);
    duration_->setAlignment(Qt::AlignCenter);
    durationGroup->addWidget(durationCaption);
    durationGroup->addWidget(duration_);
    auto *secondaryGroup = new QVBoxLayout;
    secondaryCaption_->setAlignment(Qt::AlignCenter);
    secondaryMetric_->setAlignment(Qt::AlignCenter);
    secondaryGroup->addWidget(secondaryCaption_);
    secondaryGroup->addWidget(secondaryMetric_);
    secondary->addLayout(durationGroup, 1);
    secondary->addLayout(secondaryGroup, 1);
    metricLayout->addLayout(secondary);
    cardLayout->addWidget(metrics_);
    cardLayout->addWidget(notice_);
    layout->addWidget(card);
    layout->addWidget(error_);
    layout->addWidget(reserveButton_);
    layout->addWidget(startButton_);
    layout->addWidget(cancelButton_);
    layout->addWidget(stopButton_);
    layout->addWidget(settleButton_);
    layout->addWidget(retryButton_);
    layout->addWidget(backButton_);
    // Put the active action before the long receipt so it stays reachable on short screens.
    layout->addWidget(summary_);
    auto *footnote = new QLabel(QStringLiteral("费用以服务端结算结果为准 · 演示余额扣款"), this);
    footnote->setWordWrap(true);
    footnote->setProperty("role", QStringLiteral("secondary"));
    layout->addWidget(footnote);
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
        emit backRequested(order_.has_value() ? order_->orderId : 0);
    });
    connect(retryButton_, &QPushButton::clicked, this, [this] {
        if (!connected_) {
            api_->retryConnection();
            return;
        }
        requestReconciliation();
    });
    connect(api_, &UserApi::connectionChanged, this, &ChargePage::setConnectionAvailable);
    connect(api_, &UserApi::sessionUserApplied, this,
            [this](const ev::user::User &, quint64 sessionGeneration, quint64) {
        const bool sessionChanged = sessionGeneration_ != 0
            && sessionGeneration_ != sessionGeneration;
        if (sessionChanged && pageActive_) {
            leavePage();
        }
        if (sessionChanged && pendingMutation_.has_value()) {
            emit mutationFinished(*pendingMutation_);
            pendingMutation_.reset();
            pendingMutationSubjectOrderId_ = 0;
            pendingMutationSuperseded_ = false;
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
            pendingReadOwnerOrderId_ = 0;
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
        const QString completedRequestId = pendingFactsRequestId_;
        pendingFactsRequestId_.clear();
        factsPending_ = false;
        std::optional<ev::user::Charger> matched;
        for (const auto &charger : result.chargers) {
            if (charger.chargerId == pendingFactsChargerId_) {
                matched = charger;
                break;
            }
        }
        const bool reservedStatusMismatch = matched.has_value() && order_.has_value()
            && order_->status == QStringLiteral("reserved")
            && matched->status != QStringLiteral("reserved");
        if (reservedStatusMismatch) {
            matched.reset();
        }
        if (order_.has_value()) {
            associatedCharger_ = matched;
        } else if (selection_.has_value() && matched.has_value()) {
            selection_->station = result.station;
            selection_->charger = *matched;
            associatedCharger_ = matched;
        }
        if (!matched.has_value()) {
            const ev::user::ApiError failure{
                completedRequestId, QStringLiteral("INVALID_RESPONSE"),
                reservedStatusMismatch
                    ? QStringLiteral("服务器返回的预约充电桩状态不匹配")
                    : QStringLiteral("服务器返回的充电桩信息不匹配")};
            if (exitRefreshRequired_) {
                emit nearbyDetailRefreshFailed(exitRefreshAttemptId_,
                                               exitRefreshSelectionGeneration_,
                                               exitRefreshStationId_, failure);
                if (!exitRefreshRequired_ || exitRefreshFailed_) {
                    return;
                }
            }
            error_->setText(failure.message);
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
    cancelOwnedSafeReads();
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

void ChargePage::observeAuthoritativeCurrent(const ev::user::RequestContext &context,
                                             const ev::user::CurrentOrderResult &result)
{
    if (!result.order.has_value()
        || (result.order->status != QStringLiteral("reserved")
            && result.order->status != QStringLiteral("charging"))) {
        emit currentAuthorityObserved(context, result, false, 0);
        return;
    }
    if (pendingMutation_.has_value()
        && (pendingMutationSubjectOrderId_ == 0
            || pendingMutationSubjectOrderId_ != result.order->orderId)) {
        pendingMutationSuperseded_ = true;
    }
    if (!pageActive_) {
        emit currentAuthorityObserved(context, result, false, 0);
        return;
    }
    const bool selectionMismatch = selection_.has_value()
        && !matchingSelection(*selection_, *result.order);
    invalidateSafeReads();
    pollTimer_->stop();
    exitRefreshRequired_ = false;
    exitRefreshFailed_ = false;
    exitRefreshAttemptId_ = 0;
    exitRefreshSelectionGeneration_ = 0;
    exitRefreshStationId_ = 0;
    reconciliationRequired_ = true;
    factsFailed_ = false;
    factsGateRequired_ = false;
    reconciledNoOrder_ = false;
    error_->clear();
    order_ = *result.order;
    selectionGeneration_ = 0;
    associatedCharger_.reset();
    if (selectionMismatch) {
        selection_.reset();
        emit rememberedSelectionInvalidated();
    }
    updateChargeFlowBlock();
    render();
    emit currentAuthorityObserved(context, result, false, 0);
    requestFacts(true);
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
    cancelOwnedSafeReads();
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
        cancelOwnedSafeReads();
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

void ChargePage::resetForSessionExpiry(quint64 sessionGeneration)
{
    pageActive_ = false;
    ++pageGeneration_;
    cancelOwnedSafeReads();
    pollTimer_->stop();
    pendingMutation_.reset();
    pendingMutationSubjectOrderId_ = 0;
    pendingMutationSuperseded_ = false;
    reconciliationRequired_ = false;
    factsPending_ = false;
    factsFailed_ = false;
    factsGateRequired_ = false;
    reconciledNoOrder_ = false;
    exitRefreshRequired_ = false;
    exitRefreshFailed_ = false;
    exitRefreshAttemptId_ = 0;
    exitRefreshSelectionGeneration_ = 0;
    exitRefreshStationId_ = 0;
    selection_.reset();
    order_.reset();
    associatedCharger_.reset();
    sessionGeneration_ = sessionGeneration;
    error_->clear();
    emit mutationPendingChanged(false);
    updateChargeFlowBlock();
    render();
}

void ChargePage::render()
{
    title_->setText(QStringLiteral("当前订单"));
    progress_->setText(QStringLiteral("预约  /  充电  /  结算"));
    identity_->hide();
    metrics_->hide();
    meter_->setVisible(false);
    summary_->setVisible(false);
    error_->setVisible(!error_->text().isEmpty());
    for (QPushButton *button : {reserveButton_, startButton_, cancelButton_, stopButton_,
                               settleButton_, backButton_}) button->hide();
    reserveButton_->setEnabled(false);
    startButton_->setEnabled(false);
    cancelButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    settleButton_->setEnabled(false);
    backButton_->setEnabled(false);
    retryButton_->setVisible(reconciliationRequired_ || factsFailed_ || !connected_);
    retryButton_->setEnabled(pageActive_ && !pendingRead_.has_value()
                             && !factsPending_ && !pendingMutation_.has_value());
    notice_->setText(!connected_ ? QStringLiteral("连接已断开，当前显示上次确认数据")
        : pendingMutation_.has_value() ? QStringLiteral("正在提交，请等待服务端确认")
        : reconciliationRequired_ || factsFailed_ || (factsPending_ && factsGateRequired_)
            ? QStringLiteral("正在核验订单，确认前暂停操作")
            : QStringLiteral("操作与数据均以服务端确认为准"));

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
            title_->setText(QStringLiteral("预约确认"));
            status_->setText(QStringLiteral("已选择充电桩"));
            identity_->setText(QStringLiteral("%1\n充电桩 %2\n站点挂牌价 ¥ %3 / 度")
                .arg(selection_->station.name, selection_->charger.code,
                     formatFen(selection_->station.priceFenPerKwh)));
            identity_->show();
            reserveButton_->show();
            reserveButton_->setEnabled(
                mutableNow && selection_->charger.status == QStringLiteral("idle"));
        } else if (reconciledNoOrder_) {
            status_->setText(QStringLiteral("当前无未完成订单"));
            backButton_->show();
            backButton_->setEnabled(!chargeFlowBlocked());
        }
        updatePolling();
        return;
    }
    const ev::user::Order &order = *order_;
    identity_->setText(
        QStringLiteral("%2\n充电桩 %3 · 订单 ID：%1")
            .arg(order.orderId)
            .arg(order.stationName.isEmpty() ? QStringLiteral("—") : order.stationName,
                 order.chargerCode.isEmpty() ? QStringLiteral("—") : order.chargerCode));
    identity_->show();
    summary_->setText(QStringLiteral("订单时间记录（服务端原值）\n预约：%1%2%3")
        .arg(order.reservedAt,
             order.startedAt.isEmpty() ? QString() : QStringLiteral("\n开始：%1").arg(order.startedAt),
             order.endedAt.isEmpty() ? QString() : QStringLiteral("\n结束：%1").arg(order.endedAt)));
    summary_->show();
    const bool monetaryPrimary = (order.status == QStringLiteral("charging") && !order.endedAt.isEmpty())
        || order.status == QStringLiteral("completed");
    if (order.status == QStringLiteral("charging") || order.status == QStringLiteral("completed")) {
        metricCaption_->setText(monetaryPrimary ? (order.status == QStringLiteral("completed")
            ? QStringLiteral("已结算金额") : QStringLiteral("应付金额")) : QStringLiteral("已充电量"));
        meter_->setText(monetaryPrimary ? formatFen(order.amountFen)
                                       : QString::number(order.energyKwh, 'f', 3));
        metricUnit_->setText(monetaryPrimary ? QStringLiteral("元") : QStringLiteral("kWh"));
        duration_->setText(durationText(order.elapsedSec));
        secondaryCaption_->setText(monetaryPrimary ? QStringLiteral("已充电量") : QStringLiteral("当前金额"));
        secondaryMetric_->setText(monetaryPrimary
            ? QStringLiteral("%1 kWh").arg(order.energyKwh, 0, 'f', 3)
            : QStringLiteral("¥ %1").arg(formatFen(order.amountFen)));
        metrics_->show();
        meter_->show();
    }
    if (order.status == QStringLiteral("reserved")) {
        progress_->setText(QStringLiteral("第 1 步 · 预约已确认"));
        status_->setText(QStringLiteral("已预约"));
        startButton_->show();
        cancelButton_->show();
        const bool associatedReserved = associatedCharger_.has_value()
            && associatedCharger_->chargerId == order.chargerId
            && associatedCharger_->status == QStringLiteral("reserved");
        startButton_->setEnabled(mutableNow && associatedReserved);
        cancelButton_->setEnabled(mutableNow && associatedReserved);
    } else if (order.status == QStringLiteral("charging") && order.endedAt.isEmpty()) {
        progress_->setText(QStringLiteral("第 2 步 · 充电进行中"));
        status_->setText(QStringLiteral("充电中"));
        stopButton_->show();
        stopButton_->setEnabled(mutableNow);
    } else if (order.status == QStringLiteral("charging")) {
        title_->setText(QStringLiteral("确认结算"));
        progress_->setText(QStringLiteral("第 3 步 · 已停止，待结算"));
        status_->setText(QStringLiteral("充电已停止，等待结算"));
        settleButton_->show();
        settleButton_->setEnabled(mutableNow);
    } else if (order.status == QStringLiteral("completed")) {
        title_->setText(QStringLiteral("结算结果"));
        progress_->setText(QStringLiteral("本次充电已完成"));
        status_->setText(QStringLiteral("充电完成，结算成功"));
        backButton_->show();
        backButton_->setEnabled(!chargeFlowBlocked());
    } else if (order.status == QStringLiteral("cancelled")) {
        progress_->setText(QStringLiteral("本次预约已结束"));
        status_->setText(QStringLiteral("预约已取消"));
        backButton_->show();
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
        pendingReadOwnerOrderId_ = 0;
    } else {
        pendingReadOwnerOrderId_ = order_->orderId;
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
    cancelOwnedSafeReads();
    pendingRead_ = api_->loadCurrentOrder(
        pageGeneration_, selectionGeneration_, ev::user::ChargeOperation::Reconcile);
    if (pendingRead_->requestId.isEmpty()) {
        pendingRead_.reset();
        pendingReadOwnerOrderId_ = 0;
    } else {
        pendingReadOwnerOrderId_ = order_.has_value() ? order_->orderId : 0;
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
    const bool factsDispatchFailed = pendingFactsRequestId_.isEmpty();
    if (factsDispatchFailed) {
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
    if (factsDispatchFailed && requireNearbyCommit && exitRefreshRequired_) {
        emit nearbyDetailRefreshFailed(
            exitRefreshAttemptId_, exitRefreshSelectionGeneration_, exitRefreshStationId_,
            {QString(), QStringLiteral("NOT_CONNECTED"),
             QStringLiteral("充电站信息加载失败，请刷新订单")});
    }
    updateChargeFlowBlock();
    render();
}

void ChargePage::cancelOwnedSafeReads()
{
    if (pendingRead_.has_value() && !pendingRead_->requestId.isEmpty()) {
        api_->cancelSafeRead(pendingRead_->requestId);
    }
    pendingRead_.reset();
    pendingReadOwnerOrderId_ = 0;
    if (!pendingFactsRequestId_.isEmpty()) {
        api_->cancelSafeRead(pendingFactsRequestId_);
    }
    pendingFactsRequestId_.clear();
    factsPending_ = false;
    factsGateRequired_ = false;
    if (exitRefreshRequired_) {
        exitRefreshAttemptId_ = 0;
    }
    readEpoch_ = api_->currentChargeReadEpoch();
    emit chargeSafeReadsInvalidated();
}

void ChargePage::invalidateSafeReads()
{
    (void)api_->invalidateChargeReads();
    cancelOwnedSafeReads();
}

void ChargePage::adoptMutationReadEpoch()
{
    readEpoch_ = api_->currentChargeReadEpoch();
    pendingRead_.reset();
    pendingReadOwnerOrderId_ = 0;
    if (!pendingFactsRequestId_.isEmpty()) {
        pendingFactsReadEpoch_ = readEpoch_;
    }
}

void ChargePage::beginMutation(ev::user::ChargeOperation operation)
{
    if (!pageActive_ || !connected_ || chargeFlowBlocked()) {
        return;
    }
    pendingMutationSubjectOrderId_ = order_.has_value() ? order_->orderId : 0;
    pendingMutationSuperseded_ = false;
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
        pendingMutationSubjectOrderId_ = 0;
        pendingMutationSuperseded_ = false;
    } else if (pendingMutation_.has_value()) {
        emit mutationDispatched(*pendingMutation_, pendingMutationSubjectOrderId_);
        emit mutationPendingChanged(true);
    } else {
        pendingMutationSubjectOrderId_ = 0;
        pendingMutationSuperseded_ = false;
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
    const bool currentDifferentActive = pendingMutationSuperseded_
        && order_.has_value()
        && (order_->status == QStringLiteral("reserved")
            || order_->status == QStringLiteral("charging"))
        && order_->orderId != order.orderId;
    emit mutationAuthorityObserved(context, order);
    emit mutationFinished(context);
    if (!updatePage || currentDifferentActive) {
        adoptMutationReadEpoch();
        pendingMutation_.reset();
        pendingMutationSubjectOrderId_ = 0;
        pendingMutationSuperseded_ = false;
        emit mutationPendingChanged(false);
        updateChargeFlowBlock();
        render();
        return;
    }
    cancelOwnedSafeReads();
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
    const bool reserveFactsRequired = context.operation == ev::user::ChargeOperation::Reserve
        && order.status == QStringLiteral("reserved");
    exitRefreshRequired_ = canRefreshNearby;
    exitRefreshFailed_ = false;
    exitRefreshAttemptId_ = 0;
    exitRefreshSelectionGeneration_ = canRefreshNearby ? selection_->selectionGeneration : 0;
    exitRefreshStationId_ = canRefreshNearby ? order.stationId : 0;
    reconciliationRequired_ = canRefreshNearby || reserveFactsRequired;
    pendingMutation_.reset();
    pendingMutationSubjectOrderId_ = 0;
    pendingMutationSuperseded_ = false;
    emit mutationPendingChanged(false);
    updateChargeFlowBlock();
    render();
    requestFacts(canRefreshNearby || reserveFactsRequired, canRefreshNearby);
}

void ChargePage::handleCurrentOrder(const ev::user::RequestContext &context,
                                    const ev::user::CurrentOrderResult &result)
{
    if (!pendingRead_.has_value() || context != *pendingRead_) {
        observeAuthoritativeCurrent(context, result);
        return;
    }
    if (!matchesPage(context) || context.readEpoch != readEpoch_) {
        pendingRead_.reset();
        pendingReadOwnerOrderId_ = 0;
        return;
    }
    const bool poll = context.operation == ev::user::ChargeOperation::Poll;
    const bool hardReconciliation = !poll && reconciliationRequired_;
    const qint64 resolvedOwnerOrderId = pendingReadOwnerOrderId_;
    pendingRead_.reset();
    pendingReadOwnerOrderId_ = 0;
    if (poll && order_.has_value() && result.order.has_value()
        && result.order->orderId != order_->orderId) {
        observeAuthoritativeCurrent(context, result);
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
    updateChargeFlowBlock();
    render();
    emit currentAuthorityObserved(context, result, true, resolvedOwnerOrderId);
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
        pendingReadOwnerOrderId_ = 0;
        reconciliationRequired_ = true;
        error_->setText(localizedError(failure));
        updateChargeFlowBlock();
        render();
        return;
    }
    if (!pendingMutation_.has_value() || context != *pendingMutation_) {
        return;
    }
    const bool supersededByDifferentAuthority = pendingMutationSuperseded_
        && order_.has_value()
        && (order_->status == QStringLiteral("reserved")
            || order_->status == QStringLiteral("charging"))
        && (pendingMutationSubjectOrderId_ == 0
            || pendingMutationSubjectOrderId_ != order_->orderId);
    emit mutationFinished(context);
    if (supersededByDifferentAuthority) {
        adoptMutationReadEpoch();
        pendingMutation_.reset();
        pendingMutationSubjectOrderId_ = 0;
        pendingMutationSuperseded_ = false;
        emit mutationPendingChanged(false);
        updateChargeFlowBlock();
        render();
        return;
    }
    const bool updatePage = matchesMutationPage(context);
    if (!updatePage) {
        adoptMutationReadEpoch();
        pendingMutation_.reset();
        pendingMutationSubjectOrderId_ = 0;
        pendingMutationSuperseded_ = false;
        emit mutationPendingChanged(false);
        updateChargeFlowBlock();
        render();
        return;
    }
    reconciliationRequired_ = true;
    cancelOwnedSafeReads();
    pollTimer_->stop();
    readEpoch_ = api_->currentChargeReadEpoch();
    error_->setText(uncertain ? kUncertain : localizedError(failure));
    pendingMutation_.reset();
    pendingMutationSubjectOrderId_ = 0;
    pendingMutationSuperseded_ = false;
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
    if (exitRefreshRequired_) {
        emit nearbyDetailRefreshFailed(exitRefreshAttemptId_,
                                       exitRefreshSelectionGeneration_,
                                       exitRefreshStationId_, failure);
        if (!exitRefreshRequired_ || exitRefreshFailed_) {
            return;
        }
    }
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
    if (!pendingFactsRequestId_.isEmpty()) {
        api_->cancelSafeRead(pendingFactsRequestId_);
    }
    pendingFactsRequestId_.clear();
    factsPending_ = false;
    factsGateRequired_ = true;
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
