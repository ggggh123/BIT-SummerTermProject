#include "ui/ChargePage.h"

#include "domain/Formatters.h"
#include "services/UserApi.h"
#include "ui/PulseChart.h"

#include <QLabel>
#include <QDateTime>
#include <QStyle>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString kUncertain = QStringLiteral("结果未确认，需要刷新");

// 原生矢量绘制，与能量脉冲主题一致，高 DPI 下不依赖图片缩放或系统字体。
class ReservationCheck final : public QWidget
{
public:
    explicit ReservationCheck(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("reservationSuccessIcon"));
        setAccessibleName(QStringLiteral("预约成功对号"));
        setFixedSize(168, 168);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#2b515c"), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(7, 7, 154, 154));
        painter.setPen(QPen(QColor("#426c70"), 1.5));
        painter.setBrush(QColor("#173d42"));
        painter.drawEllipse(QRectF(24, 24, 120, 120));
        painter.setPen(QPen(QColor("#8ae5d0"), 8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath check;
        check.moveTo(54, 83);
        check.lineTo(76, 105);
        check.lineTo(115, 64);
        painter.drawPath(check);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#72ddc3"));
        painter.drawEllipse(QPointF(84, 7), 3, 3);
        painter.drawEllipse(QPointF(161, 84), 3, 3);
    }
};

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
    , rechargeButton_(new QPushButton(QStringLiteral("前往账户充值"), this))
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
    rechargeButton_->setObjectName(QStringLiteral("chargeRechargeButton"));
    rechargeButton_->setProperty("role", QStringLiteral("outline"));
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
    layout->setContentsMargins(23,19,23,12);
    layout->setSpacing(0);
    auto *header=new QHBoxLayout;
    header->setSpacing(8);
    title_->setMinimumHeight(38);
    status_->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Preferred);
    status_->setWordWrap(false);
    header->addWidget(title_,1);header->addWidget(status_,0,Qt::AlignVCenter);
    layout->addLayout(header);
    layout->addSpacing(6);
    layout->addWidget(identity_);
    layout->addWidget(progress_);
    layout->addSpacing(15);
    auto *metricLayout=new QVBoxLayout(metrics_);
    metricLayout->setContentsMargins(0,0,0,0);metricLayout->setSpacing(0);
    auto *meterRow=new QFrame;
    meterRow->setObjectName(QStringLiteral("chargeMeterRow"));
    auto *meterLayout=new QHBoxLayout(meterRow);
    meterLayout->setContentsMargins(0,15,0,17);meterLayout->setSpacing(8);
    auto *energyGroup=new QVBoxLayout;
    energyGroup->setSpacing(5);
    energyGroup->addWidget(metricCaption_);
    auto *valueRow=new QHBoxLayout;valueRow->setSpacing(5);
    meter_->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Preferred);
    meter_->setWordWrap(false);metricUnit_->setWordWrap(false);
    meter_->setFixedHeight(50);
    metricUnit_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    metricUnit_->setFixedHeight(24);
    valueRow->addWidget(meter_);valueRow->addWidget(metricUnit_,0,Qt::AlignBottom);valueRow->addStretch();
    energyGroup->addLayout(valueRow);
    meterLayout->addLayout(energyGroup,1);
    powerMetricGroup_=new QWidget;
    auto *powerMetric=new QVBoxLayout(powerMetricGroup_);
    powerMetric->setContentsMargins(0,0,0,0);powerMetric->setSpacing(5);
    powerCaption_=new QLabel(QStringLiteral("最新采样"));powerCaption_->setProperty("role","pulseCaption");
    power_=new QLabel(QStringLiteral("—"));power_->setObjectName(QStringLiteral("chargePower"));
    powerCaption_->setAlignment(Qt::AlignRight);power_->setAlignment(Qt::AlignRight);
    auto *powerValueRow = new QHBoxLayout;
    powerValueRow->setSpacing(4);
    auto *powerUnit = new QLabel(QStringLiteral("kW"));
    powerUnit->setProperty("role", "pulseCaption");
    powerUnit->setFixedHeight(24);
    powerValueRow->addWidget(power_);
    powerValueRow->addWidget(powerUnit, 0, Qt::AlignBottom);
    powerMetric->addStretch();powerMetric->addWidget(powerCaption_);powerMetric->addLayout(powerValueRow);
    meterLayout->addWidget(powerMetricGroup_);
    metricLayout->addWidget(meterRow);

    powerPanel_=new QWidget;
    auto *graphLayout=new QVBoxLayout(powerPanel_);
    graphLayout->setContentsMargins(0,7,0,0);graphLayout->setSpacing(0);
    auto *graphHead=new QHBoxLayout;
    auto *graphTitle=new QLabel(QStringLiteral("功率轨迹"));graphTitle->setProperty("role","pulseCaption");
    sampleTime_=new QLabel;sampleTime_->setProperty("role","pulseCaption");
    graphHead->addWidget(graphTitle);graphHead->addStretch();graphHead->addWidget(sampleTime_);
    graphLayout->addLayout(graphHead);graphLayout->addSpacing(7);
    powerChart_=new ev::ui::PulseChart(true);
    powerChart_->setObjectName(QStringLiteral("chargePowerChart"));
    powerChart_->setFixedHeight(207);
    graphLayout->addWidget(powerChart_);
    sampleNote_=new QLabel(QStringLiteral("设备实际采样 · 点击曲线可回看"));sampleNote_->setProperty("role","pulseFine");
    sampleNote_->setWordWrap(true);graphLayout->addWidget(sampleNote_);
    auto *signal=new QFrame;signal->setObjectName(QStringLiteral("chargeSignalRow"));
    auto *signalLayout=new QHBoxLayout(signal);signalLayout->setContentsMargins(0,12,0,12);
    auto *signalText=new QLabel(QStringLiteral("功率与采样记录同步"));signalText->setProperty("role","pulseCaption");
    liveButton_=new QPushButton(QStringLiteral("回到最新"));liveButton_->setObjectName(QStringLiteral("chargeLiveButton"));
    signalLayout->addWidget(signalText);signalLayout->addStretch();signalLayout->addWidget(liveButton_);
    graphLayout->addWidget(signal);
    metricLayout->addWidget(powerPanel_);
    auto *secondary=new QHBoxLayout;secondary->setContentsMargins(0,12,0,12);secondary->setSpacing(24);
    auto *durationGroup=new QVBoxLayout;durationGroup->setSpacing(7);
    auto *durationCaption=new QLabel(QStringLiteral("充电时长"));durationCaption->setProperty("role","pulseCaption");
    durationGroup->addWidget(durationCaption);durationGroup->addWidget(duration_);
    auto *amountGroup=new QVBoxLayout;amountGroup->setSpacing(7);
    secondaryCaption_->setProperty("role","pulseCaption");amountGroup->addWidget(secondaryCaption_);amountGroup->addWidget(secondaryMetric_);
    secondary->addLayout(durationGroup,1);secondary->addLayout(amountGroup,1);
    metricLayout->addLayout(secondary);
    layout->addWidget(metrics_);
    layout->addWidget(notice_);
    layout->addWidget(error_);
    // 保留其他订单状态的弹性留白；预约状态用同一区域承载明确的成功反馈。
    auto *middle = new QWidget(this);
    auto *middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);
    middleLayout->addStretch(1);
    reservationHero_ = new QWidget(middle);
    reservationHero_->setObjectName(QStringLiteral("reservationSuccess"));
    auto *reservationLayout = new QVBoxLayout(reservationHero_);
    reservationLayout->setContentsMargins(0, 8, 0, 12);
    reservationLayout->setSpacing(10);
    reservationLayout->addWidget(new ReservationCheck(reservationHero_), 0, Qt::AlignHCenter);
    reservationTitle_ = new QLabel(QStringLiteral("预约成功"), reservationHero_);
    reservationTitle_->setObjectName(QStringLiteral("reservationSuccessTitle"));
    reservationHint_ = new QLabel(reservationHero_);
    reservationHint_->setObjectName(QStringLiteral("reservationSuccessHint"));
    for (auto *label : {reservationTitle_, reservationHint_}) {
        label->setAlignment(Qt::AlignCenter);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        reservationLayout->addWidget(label);
    }
    middleLayout->addWidget(reservationHero_);
    middleLayout->addStretch(1);
    layout->addWidget(middle, 1);
    layout->addSpacing(10);
    auto *actions = new QVBoxLayout;
    actions->setSpacing(8);
    for (auto *button : {reserveButton_, startButton_, cancelButton_, stopButton_,
                         settleButton_, rechargeButton_, retryButton_, backButton_}) actions->addWidget(button);
    layout->addLayout(actions);
    auto *footnote=new QLabel(QStringLiteral("费用与订单状态以服务端确认结果为准。\n此项目模拟充电，结算使用演示账户余额。"));
    footnote->setObjectName(QStringLiteral("chargeFootnote"));
    footnote->setProperty("role","pulseFine");footnote->setWordWrap(true);footnote->setContentsMargins(0,8,0,0);
    layout->addWidget(footnote);
    detailsButton_=new QPushButton(QStringLiteral("订单时间记录"));detailsButton_->setObjectName(QStringLiteral("chargeDetailsButton"));
    layout->addWidget(detailsButton_);layout->addWidget(summary_);
    connect(detailsButton_,&QPushButton::clicked,this,[this]{
        detailsExpanded_ = !detailsExpanded_;
        summary_->setVisible(detailsExpanded_);
    });
    connect(liveButton_,&QPushButton::clicked,powerChart_,&ev::ui::PulseChart::followLatest);
    connect(powerChart_,&ev::ui::PulseChart::cursorChanged,this,&ChargePage::renderTelemetry);
    telemetryTimer_=new QTimer(this);telemetryTimer_->setInterval(200);
    telemetryTimer_->setSingleShot(true);
    connect(telemetryTimer_,&QTimer::timeout,this,&ChargePage::requestTelemetry);
    connect(api_,&UserApi::orderTelemetryLoaded,this,[this](const QString &id,const ev::user::OrderTelemetry &data){
        if(id!=telemetryRequestId_ || !pageActive_ || !order_) return;
        telemetryRequestId_.clear();
        if (data.orderId!=order_->orderId || data.chargerId!=order_->chargerId) {
            telemetryError_=QStringLiteral("采样与当前订单不匹配，正在等待重新读取");
            renderTelemetry();
            return;
        }
        telemetry_=data;telemetryError_.clear();
        renderTelemetry();
    });

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
    connect(rechargeButton_, &QPushButton::clicked, this, &ChargePage::rechargeRequested);
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
                restoreMutationError();
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
    clearMutationError();
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
    clearMutationError();
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
    progress_->hide();
    detailsButton_->setVisible(order_.has_value());
    if (!order_ || telemetryOrderId_!=order_->orderId) {
        if(!telemetryRequestId_.isEmpty()) api_->cancelSafeRead(telemetryRequestId_);
        telemetryRequestId_.clear();telemetry_.reset();telemetryError_.clear();
        detailsExpanded_ = false;
        telemetryOrderId_=order_?order_->orderId:0;
        powerChart_->setSeries({},1600);
        powerChart_->followLatest();
    }
    if(!pageActive_||!connected_||!order_||!order_->endedAt.isEmpty())telemetryTimer_->stop();
    identity_->hide();
    reservationHero_->hide();
    metrics_->hide();
    meter_->setVisible(false);
    summary_->setVisible(order_.has_value() && detailsExpanded_);
    error_->setVisible(!error_->text().isEmpty());
    for (QPushButton *button : {reserveButton_, startButton_, cancelButton_, stopButton_,
                               settleButton_, rechargeButton_, backButton_}) button->hide();
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
    notice_->setVisible(!connected_ || pendingMutation_.has_value() || reconciliationRequired_
                        || factsFailed_ || (factsPending_&&factsGateRequired_));

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
    identity_->setText(QStringLiteral("%1 · %2").arg(
        order.stationName.isEmpty()?QStringLiteral("—"):order.stationName,
        order.chargerCode.isEmpty()?QStringLiteral("—"):order.chargerCode));
    identity_->setToolTip(QStringLiteral("订单 ID：%1").arg(order.orderId));
    identity_->setAccessibleDescription(identity_->toolTip());
    identity_->show();
    summary_->setText(QStringLiteral("订单时间记录（服务端原值）\n预约：%1%2%3")
        .arg(order.reservedAt,
             order.startedAt.isEmpty() ? QString() : QStringLiteral("\n开始：%1").arg(order.startedAt),
             order.endedAt.isEmpty() ? QString() : QStringLiteral("\n结束：%1").arg(order.endedAt)));
    detailsButton_->setText(QStringLiteral("订单 #%1 · 时间记录").arg(order.orderId));
    const bool monetaryPrimary = (order.status == QStringLiteral("charging") && !order.endedAt.isEmpty())
        || order.status == QStringLiteral("completed");
    if (order.status == QStringLiteral("charging") || order.status == QStringLiteral("completed")) {
        metricCaption_->setText(monetaryPrimary ? (order.status == QStringLiteral("completed")
            ? QStringLiteral("已结算金额") : QStringLiteral("应付金额")) : QStringLiteral("本次已充电"));
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
        powerMetricGroup_->setVisible(!monetaryPrimary);
        powerPanel_->setVisible(!monetaryPrimary);
    }
    if (order.status == QStringLiteral("reserved")) {
        progress_->setText(QStringLiteral("第 1 步 · 预约已确认"));
        status_->setText(QStringLiteral("已预约"));
        const bool associatedReserved = associatedCharger_.has_value()
            && associatedCharger_->chargerId == order.chargerId
            && associatedCharger_->status == QStringLiteral("reserved");
        // 只有服务端已确认的 reserved 订单才展示成功；待提交／待对账不冒充成功。
        reservationHero_->setVisible(!pendingMutation_ && !reconciliationRequired_);
        reservationTitle_->setText(connected_ ? QStringLiteral("预约成功")
                                              : QStringLiteral("上次确认：已预约"));
        reservationHint_->setText(!connected_ ? QStringLiteral("连接恢复后，请核验订单再继续。")
            : factsFailed_ ? QStringLiteral("预约订单已确认，设备状态暂不可用。\n请刷新订单后继续。")
            : !mutableNow ? QStringLiteral("预约订单已确认，正在核验充电桩状态。")
            : !associatedReserved ? QStringLiteral("预约订单已确认，当前设备暂不可开始充电。")
            : QStringLiteral("充电桩已为你保留\n准备就绪后，点击下方开始充电。"));
        startButton_->show();
        cancelButton_->show();
        startButton_->setEnabled(mutableNow && associatedReserved);
        cancelButton_->setEnabled(mutableNow && associatedReserved);
    } else if (order.status == QStringLiteral("charging") && order.endedAt.isEmpty()) {
        title_->setText(QStringLiteral("充电进行时"));
        progress_->setText(QStringLiteral("第 2 步 · 充电进行中"));
        status_->setText(QStringLiteral("充电中"));
        stopButton_->show();
        stopButton_->setEnabled(mutableNow);
    } else if (order.status == QStringLiteral("charging")) {
        title_->setText(QStringLiteral("确认结算"));
        progress_->setText(QStringLiteral("第 3 步 · 已停止，待结算"));
        status_->setText(QStringLiteral("待结算"));
        settleButton_->show();
        settleButton_->setEnabled(mutableNow);
        rechargeButton_->setVisible(mutationErrorCode_ == QStringLiteral("INSUFFICIENT_BALANCE"));
        rechargeButton_->setEnabled(mutableNow);
    } else if (order.status == QStringLiteral("completed")) {
        title_->setText(QStringLiteral("结算结果"));
        progress_->setText(QStringLiteral("本次充电已完成"));
        status_->setText(QStringLiteral("已完成"));
        backButton_->show();
        backButton_->setEnabled(!chargeFlowBlocked());
    } else if (order.status == QStringLiteral("cancelled")) {
        progress_->setText(QStringLiteral("本次预约已结束"));
        status_->setText(QStringLiteral("预约已取消"));
        backButton_->show();
        backButton_->setEnabled(!chargeFlowBlocked());
    }
    updatePolling();
    renderTelemetry();
}

void ChargePage::requestTelemetry()
{
    // 隐藏页面不读取图表；订单权威状态仍由原有轮询与重连流程管理。
    if(!isVisible()||!connected_||!pageActive_||!order_||order_->startedAt.isEmpty()
       ||!order_->endedAt.isEmpty()
       ||!telemetryRequestId_.isEmpty()||pendingRead_||pendingMutation_||reconciliationRequired_)return;
    telemetryRequestId_=api_->loadOrderTelemetry(order_->orderId);
}

void ChargePage::renderTelemetry()
{
    if(!order_||order_->startedAt.isEmpty())return;
    const bool live=order_->status==QStringLiteral("charging")&&order_->endedAt.isEmpty();
    ev::ui::PowerSeries series;
    series.id=QString::number(order_->chargerId);series.name=order_->chargerCode;
    double seconds=order_->elapsedSec;
    if(telemetry_) {
        const auto start=QDateTime::fromString(telemetry_->startedAt,Qt::ISODateWithMs);
        for(const auto &sample:telemetry_->samples) {
            const auto t=QDateTime::fromString(sample.recordedAt,Qt::ISODateWithMs);
            series.points.append({start.msecsTo(t)/1000.0,sample.powerKw,sample.energyKwh});
        }
        // 最新游标对齐实际采样，不把后台轮询时刻假装为新的采样。
        if(!series.points.isEmpty())seconds=series.points.last().seconds;
    }
    powerChart_->setSeries({series},qMax(1.0,seconds),telemetry_?qMax(60.0,telemetry_->ratedPowerKw):60);
    powerChart_->setMessage(telemetryError_);
    const auto reading=powerChart_->reading(0);
    power_->setText(reading?QString::number(reading->kw,'f',1):QStringLiteral("—"));
    powerCaption_->setText(powerChart_->followingLatest()?QStringLiteral("最新采样"):QStringLiteral("选中时刻"));
    sampleTime_->setText(ev::ui::PulseChart::elapsed(powerChart_->cursorSeconds()));
    if(live) {
        metricCaption_->setText(powerChart_->followingLatest()?QStringLiteral("本次已充电"):QStringLiteral("游标累计电量"));
        // 回看缺失区间时，整单总量不能冒充所选时刻的累计电量。
        meter_->setText(powerChart_->followingLatest() ? QString::number(order_->energyKwh,'f',3)
            : reading ? QString::number(reading->energy,'f',3) : QStringLiteral("—"));
    }
    const bool stale=telemetry_&&!telemetry_->samples.isEmpty()
        && QDateTime::fromString(telemetry_->samples.last().recordedAt,Qt::ISODateWithMs).secsTo(QDateTime::currentDateTimeUtc())>15;
    sampleNote_->setText(!telemetryError_.isEmpty()?telemetryError_
        : stale?QStringLiteral("采样已超过 15 秒未更新 · 当前为历史读数")
        : telemetry_&&telemetry_->truncated?QStringLiteral("最近 600 条采样 · 电量累计含更早记录")
        : QStringLiteral("设备实际采样 · 点击曲线可回看"));
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
    if(!telemetryRequestId_.isEmpty())api_->cancelSafeRead(telemetryRequestId_);
    telemetryRequestId_.clear();
    telemetryTimer_->stop();
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
    clearMutationError();
    error_->clear();
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
    clearMutationError();
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
    restoreMutationError();
    updateChargeFlowBlock();
    render();
    emit currentAuthorityObserved(context, result, true, resolvedOwnerOrderId);
    // 优先完成 2 秒权威轮询，再读取非关键的历史曲线；慢响应期间不额外发起图表请求。
    if (poll && order_ && order_->status==QStringLiteral("charging") && order_->endedAt.isEmpty())
        telemetryTimer_->start();
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
    if (!uncertain) {
        mutationError_ = localizedError(failure);
        mutationErrorCode_ = failure.code;
        mutationErrorOrder_ = order_;
    }
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

void ChargePage::clearMutationError()
{
    mutationError_.clear();
    mutationErrorCode_.clear();
    mutationErrorOrder_.reset();
}

void ChargePage::restoreMutationError()
{
    // 只读核验成功不等于写操作成功。相同订单仍停在原状态时保留业务失败原因。
    const bool sameOrder = order_.has_value() == mutationErrorOrder_.has_value()
        && (!order_ || (order_->orderId == mutationErrorOrder_->orderId
            && order_->status == mutationErrorOrder_->status
            && order_->endedAt.isEmpty() == mutationErrorOrder_->endedAt.isEmpty()));
    if (!sameOrder) clearMutationError();
    error_->setText(mutationError_);
}

void ChargePage::handleGeneralFailure(const ev::user::ApiError &failure)
{
    if(!failure.requestId.isEmpty() && failure.requestId==telemetryRequestId_) {
        telemetryRequestId_.clear();
        telemetryError_=QStringLiteral("曲线暂不可用 · 正在重试，订单操作不受影响");
        renderTelemetry();return;
    }
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
