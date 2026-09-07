#include "ui/NearbyPage.h"

#include "net/TencentMapClient.h"
#include "services/UserApi.h"

#include <QComboBox>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

// Both subviews own a vertical scroll area. Hidden detail content must not impose
// its minimum size on the list or on MainWindow's outer content viewport.
class NearbyViews final : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;
    QSize minimumSizeHint() const override { return {0, 0}; }
    QSize sizeHint() const override { return {390, 600}; }
};

QLabel *wrappedLabel(const QString &text, QWidget *parent, const char *role = "secondary")
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    label->setProperty("role", QString::fromLatin1(role));
    return label;
}

void setLabelRole(QLabel *label, const char *role)
{
    const QString nextRole = QString::fromLatin1(role);
    if (label->property("role").toString() == nextRole) {
        return;
    }
    label->setProperty("role", nextRole);
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->update();
}

QWidget *priceLabel(qint64 priceFenPerKwh, QWidget *parent, const QString &name)
{
    auto *group = new QWidget(parent);
    group->setObjectName(name);
    group->setProperty("role", QStringLiteral("priceGroup"));
    auto *layout = new QHBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto *amount = wrappedLabel(QStringLiteral("¥%1").arg(priceFenPerKwh / 100.0, 0, 'f', 2), group, "price");
    amount->setObjectName(name + QStringLiteral("Amount"));
    amount->setWordWrap(false);
    amount->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto *unit = wrappedLabel(QStringLiteral("/ 度"), group, "priceUnit");
    unit->setObjectName(name + QStringLiteral("Unit"));
    unit->setWordWrap(false);
    unit->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    layout->addWidget(amount, 0, Qt::AlignVCenter);
    layout->addWidget(unit, 0, Qt::AlignVCenter);
    layout->addStretch();
    return group;
}

QScrollArea *scrollingView(QWidget *content, QWidget *parent, const char *name)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName(QString::fromLatin1(name));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    scroll->setWidget(content);
    return scroll;
}

QString distanceText(const std::optional<double> &distance)
{
    return distance.has_value() ? QStringLiteral("%1 km").arg(*distance, 0, 'f', 2)
                               : QStringLiteral("距离未提供");
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
}

QString congestionText(const QString &level)
{
    if (level == QStringLiteral("high")) {
        return QStringLiteral("高");
    }
    if (level == QStringLiteral("medium")) {
        return QStringLiteral("中");
    }
    return QStringLiteral("低");
}

QString chargerStatusText(const QString &status)
{
    if (status == QStringLiteral("idle")) {
        return QStringLiteral("空闲");
    }
    if (status == QStringLiteral("reserved")) {
        return QStringLiteral("已预约");
    }
    if (status == QStringLiteral("charging")) {
        return QStringLiteral("充电中");
    }
    if (status == QStringLiteral("fault")) {
        return QStringLiteral("故障");
    }
    if (status == QStringLiteral("restarting")) {
        return QStringLiteral("重启中");
    }
    return QStringLiteral("状态未知");
}

const ev::user::ForecastRecord *horizonOneRecord(
    const ev::user::Station &station,
    const ev::user::ForecastLatestResult &forecast)
{
    if (!station.forecastEnabled || !forecast.forecastRun.has_value()) {
        return nullptr;
    }
    const auto it = std::find_if(
        forecast.records.cbegin(), forecast.records.cend(),
        [&station](const ev::user::ForecastRecord &record) {
            return record.stationId == station.stationId && record.horizonH == 1;
        });
    return it == forecast.records.cend() ? nullptr : &*it;
}

QString forecastRecordText(const ev::user::ForecastRecord &record, bool stale)
{
    QString text = QStringLiteral("1小时预测：繁忙 %1 / 空闲 %2，拥堵%3")
                       .arg(record.predictedBusyCount)
                       .arg(record.predictedIdleCount)
                       .arg(congestionText(record.congestionLevel));
    if (stale) {
        text += QStringLiteral("（预测已过期）");
    }
    return text;
}

int congestionRank(const QString &level)
{
    if (level == QStringLiteral("low")) {
        return 0;
    }
    if (level == QStringLiteral("medium")) {
        return 1;
    }
    return 2;
}

double stationDistance(const ev::user::Station &station)
{
    return station.distanceKm.value_or(std::numeric_limits<double>::max());
}

} // namespace

NearbyPage::NearbyPage(UserApi *userApi, TencentMapClient *mapClient, QWidget *parent)
    : QWidget(parent)
    , userApi_(userApi)
    , mapClient_(mapClient)
    , addressBox_(new QComboBox(this))
    , searchButton_(new QPushButton(QStringLiteral("查找附近充电站"), this))
    , retryButton_(new QPushButton(QStringLiteral("重试连接"), this))
    , connectionBanner_(new QLabel(QStringLiteral("服务器连接中…"), this))
    , statusLabel_(new QLabel(QStringLiteral("请选择预设地点或输入地址"), this))
    , forecastSource_(new QLabel(QStringLiteral("暂无预测来源"), this))
    , detailStatus_(new QLabel(QStringLiteral("请选择充电站查看充电桩"), this))
{
    setObjectName(QStringLiteral("nearbyPage"));
    qRegisterMetaType<ev::user::StationSelection>();
    qRegisterMetaType<ev::user::Station>();
    qRegisterMetaType<ev::user::GeoPoint>();

    addressBox_->setObjectName(QStringLiteral("addressBox"));
    addressBox_->setEditable(true);
    addressBox_->addItems(presetAddresses());
    searchButton_->setObjectName(QStringLiteral("nearbySearchButton"));
    retryButton_->setObjectName(QStringLiteral("nearbyRetryButton"));
    connectionBanner_->setObjectName(QStringLiteral("nearbyConnectionBanner"));
    statusLabel_->setObjectName(QStringLiteral("nearbyStatus"));
    statusLabel_->setWordWrap(true);
    forecastSource_->setObjectName(QStringLiteral("forecastSource"));
    forecastSource_->setWordWrap(true);
    detailStatus_->setObjectName(QStringLiteral("detailStatus"));
    detailStatus_->setWordWrap(true);
    statusLabel_->setProperty("role", QStringLiteral("secondary"));
    forecastSource_->setProperty("role", QStringLiteral("secondary"));
    detailStatus_->setProperty("role", QStringLiteral("secondary"));
    connectionBanner_->setProperty("role", QStringLiteral("danger"));
    connectionBanner_->setWordWrap(true);
    forecastSource_->hide();
    searchButton_->setProperty("role", QStringLiteral("primary"));
    addressBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    addressBox_->setMinimumContentsLength(1);
    addressBox_->setMinimumWidth(0);

    nearbyViews_ = new NearbyViews(this);
    nearbyViews_->setObjectName(QStringLiteral("nearbyViews"));
    stationListView_ = new QWidget(nearbyViews_);
    stationListView_->setObjectName(QStringLiteral("stationListView"));
    stationDetailView_ = new QWidget(nearbyViews_);
    stationDetailView_->setObjectName(QStringLiteral("stationDetailView"));
    nearbyViews_->addWidget(stationListView_);
    nearbyViews_->addWidget(stationDetailView_);

    auto *listContent = new QWidget(stationListView_);
    auto *listContentLayout = new QVBoxLayout(listContent);
    listContentLayout->setContentsMargins(20, 20, 20, 20);
    listContentLayout->setSpacing(6);
    listContentLayout->addWidget(wrappedLabel(QStringLiteral("附近充电站"), listContent, "pageTitle"));
    auto *addressLayout = new QVBoxLayout;
    addressLayout->setSpacing(4);
    addressLayout->addWidget(wrappedLabel(QStringLiteral("起点地址"), listContent, "body"));
    addressLayout->addWidget(addressBox_);
    listContentLayout->addLayout(addressLayout);
    listContentLayout->addWidget(searchButton_);
    listContentLayout->addWidget(connectionBanner_);
    listContentLayout->addWidget(retryButton_);
    listContentLayout->addWidget(statusLabel_);
    listContentLayout->addWidget(forecastSource_);
    auto *stationContainer = new QWidget(listContent);
    stationLayout_ = new QVBoxLayout(stationContainer);
    stationLayout_->setContentsMargins(0, 0, 0, 0);
    stationLayout_->setSpacing(12);
    listContentLayout->addWidget(stationContainer);
    listContentLayout->addStretch();
    auto *listLayout = new QVBoxLayout(stationListView_);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(scrollingView(listContent, stationListView_, "stationListScroll"));

    auto *detailViewLayout = new QVBoxLayout(stationDetailView_);
    detailViewLayout->setContentsMargins(0, 0, 0, 0);
    detailViewLayout->setSpacing(0);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(12, 8, 12, 8);
    auto *back = new QPushButton(stationDetailView_);
    back->setObjectName(QStringLiteral("stationDetailBackButton"));
    back->setAccessibleName(QStringLiteral("返回附近充电站"));
    back->setIcon(QIcon(QStringLiteral(":/ui/back.svg")));
    back->setIconSize({24, 24});
    back->setProperty("role", QStringLiteral("back"));
    back->setFixedSize(44, 44);
    header->addWidget(back);
    auto *heading = wrappedLabel(QStringLiteral("站点详情"), stationDetailView_, "sectionTitle");
    heading->setAlignment(Qt::AlignCenter);
    header->addWidget(heading, 1);
    header->addSpacing(44);
    detailViewLayout->addLayout(header);
    auto *detailContent = new QWidget(stationDetailView_);
    auto *detailContentLayout = new QVBoxLayout(detailContent);
    detailContentLayout->setContentsMargins(20, 8, 20, 20);
    detailContentLayout->setSpacing(12);
    detailContentLayout->addWidget(detailStatus_);
    auto *detailContainer = new QWidget(detailContent);
    detailLayout_ = new QVBoxLayout(detailContainer);
    detailLayout_->setContentsMargins(0, 0, 0, 0);
    detailLayout_->setSpacing(16);
    detailContentLayout->addWidget(detailContainer);
    detailContentLayout->addStretch();
    detailViewLayout->addWidget(scrollingView(detailContent, stationDetailView_, "stationDetailScroll"), 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(nearbyViews_);

    connect(back, &QPushButton::clicked, this, [this] {
        // Cancel only the user-initiated detail read. An authoritative post-charge
        // refresh keeps its generations and can update cached detail without navigating.
        if (!pendingDetailRequestId_.isEmpty()) {
            const QString requestId = std::exchange(pendingDetailRequestId_, {});
            pendingDetailOrigin_.reset();
            userApi_->cancelSafeRead(requestId);
            setDetailControlsEnabled(!foregroundSearchPending_);
        }
        nearbyViews_->setCurrentWidget(stationListView_);
    });

    connect(searchButton_, &QPushButton::clicked, this, &NearbyPage::searchCurrentAddress);
    connect(retryButton_, &QPushButton::clicked, this, [this] {
        if (!connected_) {
            reconnectRefreshPending_ = origin_.has_value();
            userApi_->retryConnection();
            return;
        }
        refreshAfterReconnect();
    });
    if (mapClient_ != nullptr) {
        connect(mapClient_, &TencentMapClient::geocodeSucceeded, this,
                [this](const QString &requestId, ev::user::GeoPoint origin) {
            if (requestId != pendingGeocodeId_) {
                return;
            }
            pendingGeocodeId_.clear();
            foregroundSearchPending_ = true;
            abandonChargeRefresh();
            cancelPendingStationList();
            if (!pendingForecastRequestId_.isEmpty()) {
                supersededSafeReadIds_.insert(pendingForecastRequestId_);
            }
            pendingForecastRequestId_.clear();
            if (!pendingDetailRequestId_.isEmpty()) {
                supersededSafeReadIds_.insert(pendingDetailRequestId_);
            }
            pendingDetailRequestId_.clear();
            pendingDetailOrigin_.reset();
            setDetailControlsEnabled(false);
            requestNearbyStations(origin, std::nullopt, std::nullopt, 0, true);
        });
        connect(mapClient_, &TencentMapClient::geocodeFailed, this,
                [this](const ev::user::ApiError &error) {
            if (error.requestId == pendingGeocodeId_) {
                pendingGeocodeId_.clear();
                foregroundSearchPending_ = false;
                setSearchPending(false);
                setDetailControlsEnabled(connected_);
                const QString message = errorText(error);
                statusLabel_->setText(stations_.isEmpty()
                    ? message
                    : QStringLiteral("定位失败，继续显示缓存：%1").arg(message));
            }
        });
    }
    if (userApi_ != nullptr) {
        connect(userApi_, &UserApi::loginSucceeded, this, [this] {
            setDetailControlsEnabled(true);
            if (stations_.isEmpty()) {
                statusLabel_->setText(QStringLiteral("请选择起点地址，查找附近充电站"));
                detailStatus_->setText(QStringLiteral("请选择充电站查看充电桩"));
            }
        });
        connect(userApi_, &UserApi::nearbyStationsLoaded, this,
                [this](const QString &requestId, ev::user::StationListResult result) {
            if (requestId != pendingStationsRequestId_) {
                return;
            }
            const bool chargeRefresh = pendingStationsSelectionGeneration_.has_value();
            const bool reconnectRefresh = reconnectStationsPending_;
            const bool foregroundSearch = pendingForegroundOrigin_.has_value();
            const std::optional<quint64> forecastAttempt = chargeRefreshAttemptId_;
            if (pendingStationsOriginGeneration_ != originGeneration_
                || pendingStationsSearchGeneration_ != searchGeneration_) {
                pendingStationsRequestId_.clear();
                pendingStationsSelectionGeneration_.reset();
                reconnectStationsPending_ = false;
                pendingForegroundOrigin_.reset();
                if (foregroundSearch) {
                    foregroundSearchPending_ = false;
                }
                setSearchPending(false);
                if (chargeRefresh) {
                    finishChargeRefreshUnavailable();
                }
                return;
            }
            if (pendingStationsSelectionGeneration_.has_value()
                && *pendingStationsSelectionGeneration_ != selectionGeneration_) {
                pendingStationsRequestId_.clear();
                pendingStationsSelectionGeneration_.reset();
                reconnectStationsPending_ = false;
                pendingForegroundOrigin_.reset();
                if (foregroundSearch) {
                    foregroundSearchPending_ = false;
                }
                setSearchPending(false);
                finishChargeRefreshUnavailable();
                return;
            }
            const std::optional<quint64> chargeGeneration =
                pendingStationsSelectionGeneration_;
            pendingStationsRequestId_.clear();
            pendingStationsSelectionGeneration_.reset();
            reconnectStationsPending_ = false;
            if (foregroundSearch
                && (result.origin.latitude != pendingForegroundOrigin_->latitude
                    || result.origin.longitude != pendingForegroundOrigin_->longitude)) {
                pendingForegroundOrigin_.reset();
                foregroundSearchPending_ = false;
                setSearchPending(false);
                setDetailControlsEnabled(connected_);
                showError(QStringLiteral("服务器响应无效，继续显示缓存"));
                return;
            }
            if (!chargeRefresh && !reconnectRefresh) {
                if (!pendingDetailRequestId_.isEmpty()) {
                    supersededSafeReadIds_.insert(pendingDetailRequestId_);
                }
                pendingDetailRequestId_.clear();
                pendingDetailOrigin_.reset();
            }
            const bool containsExpectedStation = !chargeRefreshStationId_.has_value()
                || std::any_of(result.stations.cbegin(), result.stations.cend(),
                               [this](const ev::user::Station &station) {
                    return station.stationId == *chargeRefreshStationId_;
                });
            if (foregroundSearch) {
                const bool newOrigin = !origin_.has_value()
                    || origin_->latitude != result.origin.latitude
                    || origin_->longitude != result.origin.longitude;
                ++originGeneration_;
                if (newOrigin) {
                    clearForecastCache();
                }
                pendingForegroundOrigin_.reset();
                foregroundSearchPending_ = false;
                applyStations(std::move(result), true);
            } else {
                applyStations(std::move(result), !chargeRefresh && !reconnectRefresh);
            }
            if (chargeGeneration.has_value()
                && chargeRefreshSelectionGeneration_ == chargeGeneration) {
                if (!containsExpectedStation) {
                    clearDisplayedDetailForMissingStation();
                    finishChargeRefreshUnavailable();
                } else {
                    chargeListApplied_ = true;
                    finishChargeRefreshIfReady();
                }
            }
            setSearchPending(false);
            if ((!chargeRefresh && !reconnectRefresh) || foregroundSearch) {
                clearLayout(detailLayout_);
                displayedDetailOrigin_.reset();
                detailStatus_->setText(QStringLiteral("请选择充电站查看充电桩"));
            }
            setDetailControlsEnabled(connected_);
            const int forecastStations = std::count_if(
                stations_.cbegin(), stations_.cend(), [](const auto &station) {
                    return station.forecastEnabled;
            });
            if (forecastStations == 6) {
                pendingForecastOriginGeneration_ = originGeneration_;
                pendingForecastSearchGeneration_ = searchGeneration_;
                pendingForecastRequestId_ = userApi_->loadLatestForecast(requestId);
                pendingForecastSelectionGeneration_ = chargeGeneration;
                pendingForecastRefreshAttemptId_ = forecastAttempt;
                if (pendingForecastRequestId_.isEmpty()) {
                    pendingForecastSelectionGeneration_.reset();
                    pendingForecastRefreshAttemptId_.reset();
                    showError(QStringLiteral("服务器响应无效，请重试"));
                }
            }
        });
        connect(userApi_, &UserApi::latestForecastLoaded, this,
                [this](const QString &requestId, ev::user::ForecastLatestResult result) {
            if (requestId != pendingForecastRequestId_
                || pendingForecastOriginGeneration_ != originGeneration_
                || pendingForecastSearchGeneration_ != searchGeneration_) {
                supersededSafeReadIds_.remove(requestId);
                return;
            }
            pendingForecastRequestId_.clear();
            pendingForecastSelectionGeneration_.reset();
            pendingForecastRefreshAttemptId_.reset();
            displayForecast(std::move(result));
        });
        connect(userApi_, &UserApi::stationDetailLoaded, this,
                [this](const QString &requestId, ev::user::StationDetailResult result) {
            if (requestId != pendingDetailRequestId_
                || pendingDetailOriginGeneration_ != originGeneration_
                || pendingDetailSearchGeneration_ != searchGeneration_
                || pendingDetailSelectionGeneration_ != selectionGeneration_
                || !pendingDetailOrigin_.has_value()) {
                supersededSafeReadIds_.remove(requestId);
                return;
            }
            pendingDetailRequestId_.clear();
            displayedDetailOrigin_ = pendingDetailOrigin_;
            displayStationDetail(std::move(result));
            setDetailPending(false);
        });
        connect(userApi_, &UserApi::requestFailed, this, &NearbyPage::handleApiFailure);
    }
    setSearchPending(false);
}

QStringList NearbyPage::presetAddresses()
{
    return {
        QStringLiteral("北京理工大学中关村校区"),
        QStringLiteral("北京航空航天大学学院路校区"),
        QStringLiteral("北京市海淀区西二旗"),
    };
}

QString NearbyPage::forecastText(const ev::user::Station &station,
                                 const ev::user::ForecastLatestResult &forecast)
{
    const auto *record = horizonOneRecord(station, forecast);
    if (record == nullptr) {
        return QStringLiteral("暂无预测");
    }
    return forecastRecordText(*record, forecast.forecastRun->stale);
}

void NearbyPage::setConnectionAvailable(bool available)
{
    connected_ = available;
    if (!available) {
        connectionBanner_->show();
        retryButton_->show();
        pendingGeocodeId_.clear();
        foregroundSearchPending_ = false;
        connectionBanner_->setText(stations_.isEmpty()
            ? QStringLiteral("服务器连接不可用")
            : QStringLiteral("服务器连接不可用 · 离线缓存"));
        connectionBanner_->setStyleSheet(QStringLiteral("color: #BE4B42;"));
        reconnectRefreshPending_ = origin_.has_value();
        const auto superseded = supersededSafeReadIds_.values();
        for (const QString &requestId : superseded) {
            userApi_->cancelSafeRead(requestId);
        }
        supersededSafeReadIds_.clear();
        cancelChargeRefresh();
        cancelPendingStationList();
        if (!pendingForecastRequestId_.isEmpty()) {
            userApi_->cancelSafeRead(pendingForecastRequestId_);
            pendingForecastRequestId_.clear();
        }
        if (!pendingDetailRequestId_.isEmpty()) {
            userApi_->cancelSafeRead(pendingDetailRequestId_);
            pendingDetailRequestId_.clear();
        }
        pendingForecastSelectionGeneration_.reset();
        pendingForecastRefreshAttemptId_.reset();
        pendingDetailOrigin_.reset();
        setDetailPending(false);
        if (nearbyViews_->currentWidget() == stationDetailView_) {
            detailStatus_->setText(QStringLiteral("服务器连接不可用 · 以下为离线缓存"));
            setLabelRole(detailStatus_, "danger");
            detailStatus_->show();
        }
        statusLabel_->setText(stations_.isEmpty()
            ? QStringLiteral("离线，暂无可用站点缓存")
            : QStringLiteral("离线缓存 · %1 个附近充电站").arg(stations_.size()));
        setSearchPending(false);
        retryButton_->setEnabled(true);
        return;
    }
    connectionBanner_->setText(QStringLiteral("服务器已连接"));
    connectionBanner_->setStyleSheet(QString());
    connectionBanner_->hide();
    retryButton_->hide();
    if (detailStatus_->text() == QStringLiteral("服务器连接不可用 · 以下为离线缓存")) {
        detailStatus_->setText(QStringLiteral("连接已恢复 · 站点信息为缓存"));
        setLabelRole(detailStatus_, "secondary");
    }
    retryButton_->setEnabled(true);
    setSearchPending(foregroundSearchPending_ || !pendingGeocodeId_.isEmpty()
                     || !pendingStationsRequestId_.isEmpty());
    if (pendingStationsRequestId_.isEmpty() && !foregroundSearchPending_) {
        setDetailControlsEnabled(true);
    }
}

void NearbyPage::refreshAfterReconnect()
{
    if (!connected_) {
        reconnectRefreshPending_ = origin_.has_value();
        return;
    }
    if (foregroundSearchPending_) {
        return;
    }
    requestReconnectStations();
}

void NearbyPage::cancelChargeRefresh()
{
    if (pendingStationsSelectionGeneration_.has_value()) {
        cancelPendingStationList();
    }
    if (pendingForecastSelectionGeneration_.has_value()
        && !pendingForecastRequestId_.isEmpty()) {
        userApi_->cancelSafeRead(pendingForecastRequestId_);
        pendingForecastRequestId_.clear();
    }
    pendingForecastSelectionGeneration_.reset();
    pendingForecastRefreshAttemptId_.reset();
    resetChargeRefresh();
}

void NearbyPage::abandonChargeRefresh()
{
    if (!chargeRefreshAttemptId_.has_value()) {
        cancelChargeRefresh();
        return;
    }
    const quint64 attemptId = *chargeRefreshAttemptId_;
    const quint64 selectionGeneration = *chargeRefreshSelectionGeneration_;
    const qint64 stationId = *chargeRefreshStationId_;
    cancelChargeRefresh();
    if (attemptId != 0) {
        emit chargeRefreshUnavailable(attemptId, selectionGeneration, stationId);
    }
}

void NearbyPage::cancelPendingStationList()
{
    if (!pendingStationsRequestId_.isEmpty()) {
        userApi_->cancelSafeRead(pendingStationsRequestId_);
    }
    pendingStationsRequestId_.clear();
    pendingStationsSelectionGeneration_.reset();
    pendingForegroundOrigin_.reset();
    reconnectStationsPending_ = false;
    if (!foregroundSearchPending_) {
        setSearchPending(false);
    }
}

void NearbyPage::refreshAfterCharge(ev::user::GeoPoint origin, qint64 stationId,
                                    quint64 selectionGeneration, quint64 refreshAttemptId)
{
    if (foregroundSearchPending_) {
        if (refreshAttemptId != 0) {
            emit chargeRefreshUnavailable(
                refreshAttemptId, selectionGeneration, stationId);
        }
        return;
    }
    const bool stationStillKnown = std::any_of(
        stations_.cbegin(), stations_.cend(), [stationId](const ev::user::Station &station) {
            return station.stationId == stationId;
        });
    if (selectionGeneration != selectionGeneration_
        || !origin_.has_value() || !stationStillKnown
        || origin.latitude != origin_->latitude || origin.longitude != origin_->longitude) {
        if (refreshAttemptId != 0) {
            emit chargeRefreshUnavailable(refreshAttemptId, selectionGeneration, stationId);
        }
        return;
    }
    requestNearbyStations(origin, selectionGeneration, stationId, refreshAttemptId);
}

void NearbyPage::applyChargeStationDetail(ev::user::StationDetailResult result,
                                          quint64 selectionGeneration,
                                          quint64 refreshAttemptId)
{
    if (!chargeRefreshAttemptId_.has_value()
        || *chargeRefreshAttemptId_ != refreshAttemptId) {
        return;
    }
    if (*chargeRefreshSelectionGeneration_ != selectionGeneration
        || *chargeRefreshStationId_ != result.station.stationId
        || selectionGeneration_ != selectionGeneration
        || chargeRefreshOriginGeneration_ != originGeneration_
        || chargeRefreshSearchGeneration_ != searchGeneration_
        || !origin_.has_value()) {
        finishChargeRefreshUnavailable();
        return;
    }
    chargeBufferedDetail_ = std::move(result);
    chargeDetailApplied_ = true;
    finishChargeRefreshIfReady();
}

void NearbyPage::failChargeStationDetail(quint64 refreshAttemptId,
                                         quint64 selectionGeneration,
                                         qint64 stationId,
                                         ev::user::ApiError error)
{
    if (!chargeRefreshAttemptId_.has_value()
        || *chargeRefreshAttemptId_ != refreshAttemptId
        || *chargeRefreshSelectionGeneration_ != selectionGeneration
        || *chargeRefreshStationId_ != stationId) {
        return;
    }
    finishChargeRefreshFailed(error);
}

void NearbyPage::displayStations(ev::user::StationListResult result)
{
    const bool newOrigin = origin_.has_value()
        && (origin_->latitude != result.origin.latitude
            || origin_->longitude != result.origin.longitude);
    abandonChargeRefresh();
    foregroundSearchPending_ = false;
    cancelPendingStationList();
    if (!pendingForecastRequestId_.isEmpty()) {
        supersededSafeReadIds_.insert(pendingForecastRequestId_);
        pendingForecastRequestId_.clear();
    }
    if (!pendingDetailRequestId_.isEmpty()) {
        supersededSafeReadIds_.insert(pendingDetailRequestId_);
        pendingDetailRequestId_.clear();
    }
    if (newOrigin) {
        clearForecastCache();
    }
    applyStations(std::move(result), true);
    clearLayout(detailLayout_);
    displayedDetailOrigin_.reset();
    detailStatus_->setText(QStringLiteral("请选择充电站查看充电桩"));
}

void NearbyPage::applyStations(ev::user::StationListResult result,
                               bool invalidateCurrentSelection)
{
    if (invalidateCurrentSelection) {
        invalidateSelection();
        nearbyViews_->setCurrentWidget(stationListView_);
    }
    origin_ = result.origin;
    stations_ = std::move(result.stations);
    statusLabel_->setText(stations_.isEmpty()
        ? QStringLiteral("附近暂无充电站")
        : QStringLiteral("已加载 %1 个附近充电站").arg(stations_.size()));
    rebuildStationCards();
}

void NearbyPage::displayForecast(ev::user::ForecastLatestResult result)
{
    forecast_ = std::move(result);
    forecastStationFacts_.clear();
    for (const auto &station : std::as_const(stations_)) {
        forecastStationFacts_.insert(
            station.stationId,
            {station.chargerCount, station.forecastEnabled});
    }
    if (!forecast_.forecastRun.has_value()) {
        forecastSource_->setText(QStringLiteral("暂无预测来源"));
    } else {
        const auto &run = *forecast_.forecastRun;
        forecastSource_->setText(
            QStringLiteral("activatedAt: %1 · generatedAt: %2 · dataCutoff: %3%4")
                .arg(run.activatedAt, run.generatedAt, run.dataCutoff,
                     run.stale ? QStringLiteral(" · 预测已过期") : QString()));
    }
    forecastSource_->setVisible(forecast_.forecastRun.has_value());
    rebuildStationCards();
}

void NearbyPage::displayStationDetail(ev::user::StationDetailResult result)
{
    abandonChargeRefresh();
    if (!pendingDetailRequestId_.isEmpty()) {
        supersededSafeReadIds_.insert(pendingDetailRequestId_);
        pendingDetailRequestId_.clear();
    }
    applyStationDetail(std::move(result), true);
}

void NearbyPage::resetForSessionExpiry()
{
    pendingGeocodeId_.clear();
    foregroundSearchPending_ = false;
    cancelChargeRefresh();
    cancelPendingStationList();
    if (!pendingForecastRequestId_.isEmpty()) {
        userApi_->cancelSafeRead(pendingForecastRequestId_);
    }
    if (!pendingDetailRequestId_.isEmpty()) {
        userApi_->cancelSafeRead(pendingDetailRequestId_);
    }
    pendingForecastRequestId_.clear();
    pendingDetailRequestId_.clear();
    supersededSafeReadIds_.clear();
    reconnectRefreshPending_ = false;
    reconnectStationsPending_ = false;
    pendingStationsSelectionGeneration_.reset();
    pendingForecastSelectionGeneration_.reset();
    pendingForecastRefreshAttemptId_.reset();
    pendingDetailOrigin_.reset();
    displayedDetailOrigin_.reset();
    origin_.reset();
    stations_.clear();
    clearForecastCache();
    ++originGeneration_;
    ++searchGeneration_;
    ++selectionGeneration_;
    rebuildStationCards();
    clearLayout(detailLayout_);
    setSearchPending(false);
    setDetailControlsEnabled(false);
    nearbyViews_->setCurrentWidget(stationListView_);
    statusLabel_->setText(QStringLiteral("请重新登录后查询附近充电站"));
    detailStatus_->setText(QStringLiteral("请重新登录后查看充电桩"));
}

void NearbyPage::applyStationDetail(ev::user::StationDetailResult result,
                                    bool invalidateCurrentSelection)
{
    if (invalidateCurrentSelection) {
        invalidateSelection();
        nearbyViews_->setCurrentWidget(stationDetailView_);
    }
    if (!displayedDetailOrigin_.has_value()) {
        displayedDetailOrigin_ = origin_;
    }
    const std::optional<ev::user::GeoPoint> selectionOrigin = displayedDetailOrigin_;
    clearLayout(detailLayout_);
    setLabelRole(detailStatus_, "secondary");
    detailStatus_->setText(result.chargers.isEmpty()
        ? QStringLiteral("该站暂无充电桩")
        : QStringLiteral("已加载 %1 个充电桩").arg(result.chargers.size()));
    detailStatus_->setVisible(result.chargers.isEmpty());
    auto *summary = new QFrame(this);
    summary->setProperty("role", QStringLiteral("card"));
    auto *summaryLayout = new QVBoxLayout(summary);
    summaryLayout->setContentsMargins(16, 16, 16, 16);
    summaryLayout->setSpacing(8);
    auto *title = wrappedLabel(result.station.name, summary, "pageTitle");
    title->setObjectName(QStringLiteral("detailTitle"));
    summaryLayout->addWidget(title);
    auto *address = wrappedLabel(result.station.address, summary);
    address->setObjectName(QStringLiteral("detailAddress"));
    summaryLayout->addWidget(address);
    auto *price = priceLabel(result.station.priceFenPerKwh, summary, QStringLiteral("detailPrice"));
    auto *priceAndFacts = new QHBoxLayout;
    priceAndFacts->addWidget(price, 1);
    auto *facts = new QVBoxLayout;
    facts->setSpacing(2);
    auto *counts = wrappedLabel(QStringLiteral("共 %1 桩 / 空闲 %2")
        .arg(result.station.chargerCount).arg(result.station.idleCount), summary);
    counts->setObjectName(QStringLiteral("detailCounts"));
    counts->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    facts->addWidget(counts);
    std::optional<double> distance = result.station.distanceKm;
    if (!distance.has_value() && selectionOrigin.has_value() && origin_.has_value()
        && selectionOrigin->latitude == origin_->latitude
        && selectionOrigin->longitude == origin_->longitude) {
        const auto station = std::find_if(stations_.cbegin(), stations_.cend(), [&result](const auto &s) {
            return s.stationId == result.station.stationId;
        });
        if (station != stations_.cend()) distance = station->distanceKm;
    }
    auto *distanceLabel = wrappedLabel(distanceText(distance), summary);
    distanceLabel->setObjectName(QStringLiteral("detailDistance"));
    distanceLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    facts->addWidget(distanceLabel);
    priceAndFacts->addLayout(facts, 1);
    summaryLayout->addLayout(priceAndFacts);
    auto *navigate = new QPushButton(QStringLiteral("导航到这里"), summary);
    navigate->setObjectName(QStringLiteral("navigateButton"));
    navigate->setProperty("role", QStringLiteral("outline"));
    navigate->setIcon(QIcon(QStringLiteral(":/ui/location.svg")));
    navigate->setIconSize({22, 22});
    connect(navigate, &QPushButton::clicked, this, [this, selectionOrigin, station = result.station] {
        if (selectionOrigin.has_value()) emit navigationRequested(*selectionOrigin, station);
    });
    summaryLayout->addWidget(navigate);
    detailLayout_->addWidget(summary);
    detailLayout_->addWidget(wrappedLabel(QStringLiteral("选择充电桩"), this, "sectionTitle"));
    auto *chargerGroup = new QFrame(this);
    chargerGroup->setProperty("role", QStringLiteral("card"));
    auto *chargerRows = new QVBoxLayout(chargerGroup);
    chargerRows->setContentsMargins(0, 0, 0, 0);
    chargerRows->setSpacing(0);
    detailLayout_->addWidget(chargerGroup);
    for (const auto &charger : result.chargers) {
        auto *card = new QFrame(chargerGroup);
        card->setProperty("role", QStringLiteral("chargerRow"));
        card->setProperty("last", charger.chargerId == result.chargers.last().chargerId);
        auto *row = new QHBoxLayout(card);
        row->setContentsMargins(16, 16, 16, 16);
        row->setSpacing(8);
        auto *icon = new QLabel(card);
        icon->setPixmap(QIcon(QStringLiteral(":/ui/charger.svg")).pixmap({28, 28}));
        icon->setFixedSize(28, 32);
        row->addWidget(icon, 0, Qt::AlignVCenter);
        auto *text = new QVBoxLayout;
        text->setSpacing(3);
        auto *id = wrappedLabel(QString::number(charger.chargerId), card, "sectionTitle");
        id->setObjectName(QStringLiteral("chargerId_%1").arg(charger.chargerId));
        id->setAccessibleName(QStringLiteral("充电桩 ID：%1").arg(charger.chargerId));
        auto *idAndCode = new QHBoxLayout;
        idAndCode->setSpacing(8);
        id->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        idAndCode->addWidget(id);
        text->addLayout(idAndCode);
        auto *code = wrappedLabel(charger.code, card);
        code->setAccessibleName(QStringLiteral("充电桩编号 %1").arg(charger.code));
        code->setObjectName(QStringLiteral("chargerCode_%1").arg(charger.chargerId));
        idAndCode->addWidget(code, 1);
        auto *metadata = wrappedLabel(QStringLiteral("%1 · 额定功率 %2 kW")
            .arg(charger.type == QStringLiteral("fast") ? QStringLiteral("快充") : QStringLiteral("慢充"))
            .arg(charger.powerKw, 0, 'g', 8), card);
        metadata->setObjectName(QStringLiteral("chargerMetadata_%1").arg(charger.chargerId));
        text->addWidget(metadata);
        auto *status = wrappedLabel(chargerStatusText(charger.status), card,
            charger.status == QStringLiteral("idle") ? "available" : "secondary");
        status->setObjectName(QStringLiteral("chargerStatus_%1").arg(charger.chargerId));
        if (charger.status == QStringLiteral("fault")) status->setProperty("role", QStringLiteral("danger"));
        status->setAlignment(Qt::AlignCenter);
        row->addLayout(text, 1);
        auto *actions = new QVBoxLayout;
        actions->setSpacing(2);
        actions->addWidget(status);
        const bool available = charger.status == QStringLiteral("idle");
        auto *button = new QPushButton(available ? QStringLiteral("选择") : QStringLiteral("不可选"), card);
        button->setObjectName(QStringLiteral("chargerButton_%1").arg(charger.chargerId));
        button->setProperty("role", QStringLiteral("outline"));
        button->setProperty("chargerAvailable", available);
        button->setFixedWidth(76);
        button->setAccessibleName(QStringLiteral("%1充电桩 %2，%3")
            .arg(available ? QStringLiteral("选择") : QStringLiteral("不可选择"))
            .arg(charger.chargerId).arg(chargerStatusText(charger.status)));
        connect(button, &QPushButton::clicked, this, [this, selectionOrigin, station = result.station, charger] {
            if (selectionOrigin.has_value() && charger.status == QStringLiteral("idle")) {
                ++selectionGeneration_;
                emit chargerSelected({*selectionOrigin, station, charger, selectionGeneration_});
            }
        });
        actions->addWidget(button);
        row->addLayout(actions);
        chargerRows->addWidget(card);
    }
    detailLayout_->addWidget(wrappedLabel(QStringLiteral("选择空闲桩后进入预约确认。"), this));
    setDetailControlsEnabled(detailControlsEnabled_);
}

void NearbyPage::searchCurrentAddress()
{
    if (!connected_) {
        setSearchPending(false);
        statusLabel_->setText(stations_.isEmpty()
            ? QStringLiteral("离线，暂无可用站点缓存")
            : QStringLiteral("离线缓存 · %1 个附近充电站").arg(stations_.size()));
        return;
    }
    const QString address = addressBox_->currentText().trimmed();
    if (mapClient_ == nullptr) {
        showError(QStringLiteral("地图服务不可用"));
        return;
    }
    ++searchGeneration_;
    foregroundSearchPending_ = true;
    pendingForegroundOrigin_.reset();
    setSearchPending(true);
    setDetailControlsEnabled(false);
    statusLabel_->setText(stations_.isEmpty()
        ? QStringLiteral("正在定位地址…")
        : QStringLiteral("正在定位新地址（保留当前缓存）…"));
    abandonChargeRefresh();
    cancelPendingStationList();
    if (!pendingForecastRequestId_.isEmpty()) {
        userApi_->cancelSafeRead(pendingForecastRequestId_);
        pendingForecastRequestId_.clear();
    }
    pendingForecastSelectionGeneration_.reset();
    pendingForecastRefreshAttemptId_.reset();
    if (!pendingDetailRequestId_.isEmpty()) {
        userApi_->cancelSafeRead(pendingDetailRequestId_);
        pendingDetailRequestId_.clear();
    }
    pendingDetailOrigin_.reset();
    pendingGeocodeId_ = mapClient_->geocode(address);
}

void NearbyPage::requestNearbyStations(
    const ev::user::GeoPoint &origin,
    std::optional<quint64> requiredSelectionGeneration,
    std::optional<qint64> expectedStationId,
    quint64 refreshAttemptId, bool foregroundSearch)
{
    if (userApi_ == nullptr) {
        return;
    }
    if (requiredSelectionGeneration.has_value()) {
        cancelChargeRefresh();
    } else {
        abandonChargeRefresh();
    }
    cancelPendingStationList();
    if (foregroundSearch) {
        pendingForegroundOrigin_ = origin;
    }
    setSearchPending(true);
    statusLabel_->setText(foregroundSearch && !stations_.isEmpty()
        ? QStringLiteral("正在加载新的附近充电站（保留当前缓存）…")
        : QStringLiteral("正在加载附近充电站…"));
    ++searchGeneration_;
    pendingStationsSelectionGeneration_ = requiredSelectionGeneration;
    if (requiredSelectionGeneration.has_value()) {
        chargeRefreshAttemptId_ = refreshAttemptId;
        chargeRefreshSelectionGeneration_ = requiredSelectionGeneration;
        chargeRefreshStationId_ = expectedStationId;
        chargeRefreshOriginGeneration_ = originGeneration_;
        chargeRefreshSearchGeneration_ = searchGeneration_;
        chargeListApplied_ = false;
        chargeDetailApplied_ = false;
        chargeBufferedDetail_.reset();
    }
    if (!pendingForecastRequestId_.isEmpty()) {
        supersededSafeReadIds_.insert(pendingForecastRequestId_);
    }
    pendingForecastRequestId_.clear();
    pendingForecastSelectionGeneration_.reset();
    pendingForecastRefreshAttemptId_.reset();
    if (!pendingDetailRequestId_.isEmpty()) {
        if (!foregroundSearch) {
            invalidateSelection();
        }
        supersededSafeReadIds_.insert(pendingDetailRequestId_);
        pendingDetailRequestId_.clear();
        pendingDetailOrigin_.reset();
        setDetailPending(false);
    }
    pendingStationsOriginGeneration_ = originGeneration_;
    pendingStationsSearchGeneration_ = searchGeneration_;
    pendingStationsRequestId_ = userApi_->loadNearbyStations(origin);
    if (foregroundSearch) {
        setDetailControlsEnabled(false);
    }
    if (pendingStationsRequestId_.isEmpty()) {
        pendingStationsSelectionGeneration_.reset();
        pendingForegroundOrigin_.reset();
        if (foregroundSearch) {
            foregroundSearchPending_ = false;
        }
        setSearchPending(false);
        setDetailControlsEnabled(connected_);
        showError(stations_.isEmpty() ? QStringLiteral("请求失败，请重试")
                                      : QStringLiteral("请求失败，继续显示缓存"));
        if (requiredSelectionGeneration.has_value()) {
            finishChargeRefreshFailed({QString(), QStringLiteral("NOT_CONNECTED"),
                                       QStringLiteral("请求失败，请重试")});
        }
    }
}

void NearbyPage::requestStationDetail(const ev::user::Station &station)
{
    if (userApi_ == nullptr || !origin_.has_value()) {
        return;
    }
    abandonChargeRefresh();
    invalidateSelection();
    pendingDetailOriginGeneration_ = originGeneration_;
    pendingDetailSearchGeneration_ = searchGeneration_;
    pendingDetailSelectionGeneration_ = selectionGeneration_;
    pendingDetailOrigin_ = origin_;
    nearbyViews_->setCurrentWidget(stationDetailView_);
    setDetailPending(true);
    pendingDetailRequestId_ = userApi_->loadStationDetail(station.stationId);
    if (pendingDetailRequestId_.isEmpty()) {
        setDetailPending(false);
        detailStatus_->setText(QStringLiteral("请求失败，请重试"));
    }
}

void NearbyPage::requestReconnectStations()
{
    if (userApi_ == nullptr || !connected_ || !origin_.has_value()
        || chargeRefreshAttemptId_.has_value() || foregroundSearchPending_) {
        return;
    }
    reconnectRefreshPending_ = false;
    cancelPendingStationList();
    ++searchGeneration_;
    pendingStationsOriginGeneration_ = originGeneration_;
    pendingStationsSearchGeneration_ = searchGeneration_;
    reconnectStationsPending_ = true;
    statusLabel_->setText(stations_.isEmpty()
        ? QStringLiteral("正在重新加载附近充电站…")
        : QStringLiteral("正在刷新附近充电站（保留当前缓存）…"));
    pendingStationsRequestId_ = userApi_->loadNearbyStations(*origin_);
    if (pendingStationsRequestId_.isEmpty()) {
        reconnectStationsPending_ = false;
        statusLabel_->setText(stations_.isEmpty()
            ? QStringLiteral("重新连接后刷新失败")
            : QStringLiteral("刷新失败，继续显示缓存"));
    }
}

void NearbyPage::invalidateSelection()
{
    ++selectionGeneration_;
    emit selectionInvalidated(selectionGeneration_);
}

void NearbyPage::rebuildStationCards()
{
    clearLayout(stationLayout_);
    QVector<ev::user::Station> displayStations = stations_;
    const bool freshRun = forecast_.forecastRun.has_value()
        && !forecast_.forecastRun->stale;
    std::sort(displayStations.begin(), displayStations.end(),
              [this, freshRun](const auto &left, const auto &right) {
        const auto *leftRecord = freshRun ? compatibleHorizonOneRecord(left) : nullptr;
        const auto *rightRecord = freshRun ? compatibleHorizonOneRecord(right) : nullptr;
        if ((leftRecord != nullptr) != (rightRecord != nullptr)) {
            return leftRecord != nullptr;
        }
        if (leftRecord != nullptr && rightRecord != nullptr) {
            const int leftCongestion = congestionRank(leftRecord->congestionLevel);
            const int rightCongestion = congestionRank(rightRecord->congestionLevel);
            if (leftCongestion != rightCongestion) {
                return leftCongestion < rightCongestion;
            }
            if (leftRecord->predictedIdleCount != rightRecord->predictedIdleCount) {
                return leftRecord->predictedIdleCount > rightRecord->predictedIdleCount;
            }
        }
        if (stationDistance(left) != stationDistance(right)) {
            return stationDistance(left) < stationDistance(right);
        }
        return left.stationId < right.stationId;
    });
    for (const auto &station : displayStations) {
        auto *card = new QFrame(this);
        card->setProperty("role", QStringLiteral("card"));
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(2);
        auto *name = wrappedLabel(station.name, card, "sectionTitle");
        name->setObjectName(QStringLiteral("stationName_%1").arg(station.stationId));
        layout->addWidget(name);
        auto *priceRow = new QHBoxLayout;
        priceRow->setSpacing(4);
        auto *price = priceLabel(station.priceFenPerKwh, card,
            QStringLiteral("stationPrice_%1").arg(station.stationId));
        priceRow->addWidget(price, 1);
        auto *navigate = new QPushButton(distanceText(station.distanceKm), card);
        navigate->setObjectName(QStringLiteral("stationNavigateButton_%1").arg(station.stationId));
        navigate->setProperty("role", QStringLiteral("textAction"));
        navigate->setIcon(QIcon(QStringLiteral(":/ui/location.svg")));
        navigate->setIconSize({18, 18});
        navigate->setAccessibleName(QStringLiteral("导航到%1，%2").arg(station.name, distanceText(station.distanceKm)));
        connect(navigate, &QPushButton::clicked, this, [this, origin = origin_, station] {
            if (origin.has_value()) emit navigationRequested(*origin, station);
        });
        priceRow->addWidget(navigate);
        layout->addLayout(priceRow);
        auto *counts = wrappedLabel(QStringLiteral("共 %1 桩 / 空闲 %2")
            .arg(station.chargerCount).arg(station.idleCount), card);
        counts->setObjectName(QStringLiteral("stationCounts_%1").arg(station.stationId));
        layout->addWidget(counts);
        auto *prediction = wrappedLabel(forecastTextForStation(station), card);
        prediction->setObjectName(QStringLiteral("forecastLabel_%1").arg(station.stationId));
        prediction->setVisible(compatibleHorizonOneRecord(station) != nullptr);
        layout->addWidget(prediction);
        auto *open = new QPushButton(QStringLiteral("查看充电桩"), card);
        open->setObjectName(QStringLiteral("stationButton_%1").arg(station.stationId));
        open->setProperty("role", QStringLiteral("cardAction"));
        open->setIcon(QIcon(QStringLiteral(":/ui/charger.svg")));
        open->setIconSize({24, 24});
        connect(open, &QPushButton::clicked, this, [this, station] {
            requestStationDetail(station);
        });
        layout->addWidget(open);
        stationLayout_->addWidget(card);
    }
    stationLayout_->addStretch();
    setDetailControlsEnabled(detailControlsEnabled_);
}

void NearbyPage::clearForecastCache()
{
    forecast_ = {};
    forecastStationFacts_.clear();
    forecastSource_->setText(QStringLiteral("暂无预测来源"));
    forecastSource_->hide();
}

const ev::user::ForecastRecord *NearbyPage::compatibleHorizonOneRecord(
    const ev::user::Station &station) const
{
    const auto facts = forecastStationFacts_.constFind(station.stationId);
    if (facts == forecastStationFacts_.cend()
        || facts->chargerCount != station.chargerCount
        || facts->forecastEnabled != station.forecastEnabled) {
        return nullptr;
    }
    return horizonOneRecord(station, forecast_);
}

QString NearbyPage::forecastTextForStation(const ev::user::Station &station) const
{
    const auto *record = compatibleHorizonOneRecord(station);
    if (record == nullptr || !forecast_.forecastRun.has_value()) {
        return QStringLiteral("暂无预测");
    }
    return forecastRecordText(*record, forecast_.forecastRun->stale);
}

void NearbyPage::showError(const QString &message)
{
    statusLabel_->setText(message.isEmpty() ? QStringLiteral("请求失败，请重试") : message);
}

void NearbyPage::setSearchPending(bool pending)
{
    searchButton_->setEnabled(connected_ && !pending);
    addressBox_->setEnabled(!pending);
    searchButton_->setText(pending ? QStringLiteral("加载中…") : QStringLiteral("查找附近充电站"));
}

void NearbyPage::setDetailPending(bool pending)
{
    setDetailControlsEnabled(!pending);
    if (pending) {
        setLabelRole(detailStatus_, "secondary");
        detailStatus_->setText(QStringLiteral("正在加载充电桩…"));
        detailStatus_->show();
    }
}

void NearbyPage::setDetailControlsEnabled(bool enabled)
{
    detailControlsEnabled_ = enabled;
    const auto buttons = findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        const QString name = button->objectName();
        if (name.startsWith(QStringLiteral("chargerButton_"))) {
            button->setEnabled(enabled && button->property("chargerAvailable").toBool());
        } else if (name.startsWith(QStringLiteral("stationButton_"))
            || name.startsWith(QStringLiteral("stationNavigateButton_"))
            || name == QStringLiteral("navigateButton")) {
            button->setEnabled(enabled);
        }
    }
}

void NearbyPage::handleApiFailure(const ev::user::ApiError &error)
{
    if (supersededSafeReadIds_.remove(error.requestId) > 0) {
        return;
    }
    if (!pendingStationsRequestId_.isEmpty()
        && error.requestId == pendingStationsRequestId_) {
        const bool chargeRefresh = pendingStationsSelectionGeneration_.has_value();
        const bool reconnectRefresh = reconnectStationsPending_;
        const bool foregroundSearch = pendingForegroundOrigin_.has_value();
        if (pendingStationsOriginGeneration_ != originGeneration_
            || pendingStationsSearchGeneration_ != searchGeneration_) {
            pendingStationsRequestId_.clear();
            pendingStationsSelectionGeneration_.reset();
            pendingForegroundOrigin_.reset();
            reconnectStationsPending_ = false;
            setSearchPending(false);
            if (chargeRefresh) {
                finishChargeRefreshUnavailable();
            }
            return;
        }
        if (pendingStationsSelectionGeneration_.has_value()
            && *pendingStationsSelectionGeneration_ != selectionGeneration_) {
            pendingStationsRequestId_.clear();
            pendingStationsSelectionGeneration_.reset();
            pendingForegroundOrigin_.reset();
            reconnectStationsPending_ = false;
            setSearchPending(false);
            finishChargeRefreshUnavailable();
            return;
        }
        pendingStationsRequestId_.clear();
        pendingStationsSelectionGeneration_.reset();
        pendingForegroundOrigin_.reset();
        if (foregroundSearch) {
            foregroundSearchPending_ = false;
        }
        reconnectStationsPending_ = false;
        setSearchPending(false);
        setDetailControlsEnabled(connected_);
        if ((reconnectRefresh || foregroundSearch) && !stations_.isEmpty()) {
            statusLabel_->setText(QStringLiteral("刷新失败，继续显示缓存：%1")
                                      .arg(errorText(error)));
        } else {
            showError(errorText(error));
        }
        if (chargeRefresh) {
            finishChargeRefreshFailed(error);
        }
        return;
    }
    if (!pendingForecastRequestId_.isEmpty()
        && error.requestId == pendingForecastRequestId_
        && pendingForecastOriginGeneration_ == originGeneration_
        && pendingForecastSearchGeneration_ == searchGeneration_) {
        pendingForecastRequestId_.clear();
        pendingForecastSelectionGeneration_.reset();
        pendingForecastRefreshAttemptId_.reset();
        if (!forecast_.records.isEmpty()) {
            statusLabel_->setText(QStringLiteral("预测刷新失败，继续显示缓存：%1")
                                      .arg(errorText(error)));
        } else {
            showError(errorText(error));
        }
        return;
    }
    if (!pendingDetailRequestId_.isEmpty()
        && error.requestId == pendingDetailRequestId_
        && pendingDetailOriginGeneration_ == originGeneration_
        && pendingDetailSearchGeneration_ == searchGeneration_
        && pendingDetailSelectionGeneration_ == selectionGeneration_) {
        pendingDetailRequestId_.clear();
        setDetailPending(false);
        detailStatus_->setText(errorText(error));
        detailStatus_->show();
    }
}

void NearbyPage::finishChargeRefreshIfReady()
{
    if (!chargeRefreshAttemptId_.has_value()
        || !chargeListApplied_ || !chargeDetailApplied_) {
        return;
    }
    applyStationDetail(std::move(*chargeBufferedDetail_), false);
    const quint64 attemptId = *chargeRefreshAttemptId_;
    const quint64 selectionGeneration = *chargeRefreshSelectionGeneration_;
    const qint64 stationId = *chargeRefreshStationId_;
    resetChargeRefresh();
    if (attemptId != 0) {
        emit chargeRefreshCommitted(attemptId, selectionGeneration, stationId);
    }
}

void NearbyPage::finishChargeRefreshFailed(const ev::user::ApiError &error)
{
    if (!chargeRefreshAttemptId_.has_value()) {
        return;
    }
    const quint64 attemptId = *chargeRefreshAttemptId_;
    const quint64 selectionGeneration = *chargeRefreshSelectionGeneration_;
    const qint64 stationId = *chargeRefreshStationId_;
    resetChargeRefresh();
    if (pendingStationsSelectionGeneration_.has_value()) {
        cancelPendingStationList();
    }
    if (pendingForecastRefreshAttemptId_ == attemptId
        && !pendingForecastRequestId_.isEmpty()) {
        userApi_->cancelSafeRead(pendingForecastRequestId_);
        pendingForecastRequestId_.clear();
        pendingForecastSelectionGeneration_.reset();
        pendingForecastRefreshAttemptId_.reset();
    }
    if (attemptId != 0) {
        emit chargeRefreshFailed(attemptId, selectionGeneration, stationId, error);
    }
}

void NearbyPage::finishChargeRefreshUnavailable()
{
    if (!chargeRefreshAttemptId_.has_value()) {
        return;
    }
    const quint64 attemptId = *chargeRefreshAttemptId_;
    const quint64 selectionGeneration = *chargeRefreshSelectionGeneration_;
    const qint64 stationId = *chargeRefreshStationId_;
    resetChargeRefresh();
    if (attemptId != 0) {
        emit chargeRefreshUnavailable(attemptId, selectionGeneration, stationId);
    }
}

void NearbyPage::resetChargeRefresh()
{
    chargeRefreshAttemptId_.reset();
    chargeRefreshSelectionGeneration_.reset();
    chargeRefreshStationId_.reset();
    chargeRefreshOriginGeneration_ = 0;
    chargeRefreshSearchGeneration_ = 0;
    chargeListApplied_ = false;
    chargeDetailApplied_ = false;
    chargeBufferedDetail_.reset();
}

void NearbyPage::clearDisplayedDetailForMissingStation()
{
    invalidateSelection();
    clearLayout(detailLayout_);
    displayedDetailOrigin_.reset();
    detailStatus_->setText(QStringLiteral("原充电站已不在附近列表中"));
    detailStatus_->show();
}

QString NearbyPage::errorText(const ev::user::ApiError &error)
{
    if (error.code == QStringLiteral("NOT_CONNECTED")) {
        return QStringLiteral("服务器连接不可用");
    }
    if (error.code == QStringLiteral("TRANSPORT_ERROR")) {
        return QStringLiteral("服务器连接中断，请重试");
    }
    if (error.code == QStringLiteral("TIMEOUT")) {
        return QStringLiteral("服务器响应超时，请重试");
    }
    if (error.code == QStringLiteral("PROTOCOL_ERROR")) {
        return QStringLiteral("服务器通信异常，请重试");
    }
    if (error.code == QStringLiteral("INVALID_RESPONSE")) {
        return QStringLiteral("服务器响应无效，请重试");
    }
    if (error.code == QStringLiteral("MAP_TIMEOUT")) {
        return QStringLiteral("地图请求超时，请重试");
    }
    if (error.code == QStringLiteral("NETWORK_ERROR")) {
        return QStringLiteral("地图网络请求失败，请重试");
    }
    if (error.code == QStringLiteral("MAP_API_ERROR")) {
        return QStringLiteral("腾讯地图服务返回错误，请重试");
    }
    if (error.code == QStringLiteral("INVALID_ADDRESS")) {
        return QStringLiteral("请输入有效地址");
    }
    if (error.code == QStringLiteral("AUTH_REQUIRED")) {
        return QStringLiteral("请先登录");
    }
    if (error.code == QStringLiteral("ENTITY_NOT_FOUND")) {
        return QStringLiteral("未找到对应充电站");
    }
    return QStringLiteral("请求失败，请重试");
}
