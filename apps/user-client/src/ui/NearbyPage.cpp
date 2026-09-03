#include "ui/NearbyPage.h"

#include "net/TencentMapClient.h"
#include "services/UserApi.h"

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

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

    auto *stationContainer = new QWidget(this);
    stationLayout_ = new QVBoxLayout(stationContainer);
    stationLayout_->setContentsMargins(0, 0, 0, 0);
    auto *stationScroll = new QScrollArea(this);
    stationScroll->setWidgetResizable(true);
    stationScroll->setWidget(stationContainer);
    auto *detailContainer = new QWidget(this);
    detailLayout_ = new QVBoxLayout(detailContainer);
    detailLayout_->setContentsMargins(0, 0, 0, 0);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("起点地址"), this));
    layout->addWidget(addressBox_);
    layout->addWidget(searchButton_);
    layout->addWidget(connectionBanner_);
    layout->addWidget(retryButton_);
    layout->addWidget(statusLabel_);
    layout->addWidget(forecastSource_);
    layout->addWidget(stationScroll, 1);
    layout->addWidget(detailStatus_);
    layout->addWidget(detailContainer);

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
                setSearchPending(false);
                showError(errorText(error));
            }
        });
    }
    if (userApi_ != nullptr) {
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
        connectionBanner_->setText(stations_.isEmpty()
            ? QStringLiteral("服务器连接不可用")
            : QStringLiteral("服务器连接不可用 · 离线缓存"));
        connectionBanner_->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
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
        statusLabel_->setText(stations_.isEmpty()
            ? QStringLiteral("离线，暂无可用站点缓存")
            : QStringLiteral("离线缓存 · %1 个附近充电站").arg(stations_.size()));
        retryButton_->setEnabled(true);
        return;
    }
    connectionBanner_->setText(QStringLiteral("服务器已连接"));
    connectionBanner_->setStyleSheet(QString());
    retryButton_->setEnabled(true);
    if (pendingStationsRequestId_.isEmpty()) {
        setDetailControlsEnabled(true);
    }
}

void NearbyPage::refreshAfterReconnect()
{
    if (!connected_) {
        reconnectRefreshPending_ = origin_.has_value();
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
    setSearchPending(false);
}

void NearbyPage::refreshAfterCharge(ev::user::GeoPoint origin, qint64 stationId,
                                    quint64 selectionGeneration, quint64 refreshAttemptId)
{
    if (pendingForegroundOrigin_.has_value()) {
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
    statusLabel_->setText(QStringLiteral("请重新登录后查询附近充电站"));
    detailStatus_->setText(QStringLiteral("请重新登录后查看充电桩"));
}

void NearbyPage::applyStationDetail(ev::user::StationDetailResult result,
                                    bool invalidateCurrentSelection)
{
    if (invalidateCurrentSelection) {
        invalidateSelection();
    }
    if (!displayedDetailOrigin_.has_value()) {
        displayedDetailOrigin_ = origin_;
    }
    const std::optional<ev::user::GeoPoint> selectionOrigin = displayedDetailOrigin_;
    clearLayout(detailLayout_);
    detailStatus_->setText(result.chargers.isEmpty()
        ? QStringLiteral("该站暂无充电桩")
        : QStringLiteral("已加载 %1 个充电桩").arg(result.chargers.size()));
    auto *title = new QLabel(QStringLiteral("%1 · 充电桩").arg(result.station.name), this);
    title->setObjectName(QStringLiteral("detailTitle"));
    detailLayout_->addWidget(title);
    for (const auto &charger : result.chargers) {
        auto *button = new QPushButton(
            QStringLiteral("充电桩 ID：%1 · %2 · %3 · %4 kW · %5")
                .arg(charger.chargerId)
                .arg(charger.code,
                     charger.type == QStringLiteral("fast") ? QStringLiteral("快充") : QStringLiteral("慢充"))
                .arg(charger.powerKw, 0, 'f', 1)
                .arg(chargerStatusText(charger.status)), this);
        button->setObjectName(QStringLiteral("chargerButton_%1").arg(charger.chargerId));
        connect(button, &QPushButton::clicked, this, [this, selectionOrigin, station = result.station, charger] {
            if (selectionOrigin.has_value()) {
                ++selectionGeneration_;
                emit chargerSelected({*selectionOrigin, station, charger, selectionGeneration_});
            }
        });
        detailLayout_->addWidget(button);
    }
    auto *navigate = new QPushButton(QStringLiteral("导航到该站"), this);
    navigate->setObjectName(QStringLiteral("navigateButton"));
    connect(navigate, &QPushButton::clicked, this, [this, selectionOrigin, station = result.station] {
        if (selectionOrigin.has_value()) {
            emit navigationRequested(*selectionOrigin, station);
        }
    });
    detailLayout_->addWidget(navigate);
}

void NearbyPage::searchCurrentAddress()
{
    const QString address = addressBox_->currentText().trimmed();
    if (mapClient_ == nullptr) {
        showError(QStringLiteral("地图服务不可用"));
        return;
    }
    searchButton_->setEnabled(false);
    statusLabel_->setText(QStringLiteral("正在定位地址…"));
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
        || chargeRefreshAttemptId_.has_value()) {
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
        card->setFrameShape(QFrame::StyledPanel);
        auto *layout = new QVBoxLayout(card);
        auto *name = new QLabel(station.name, card);
        name->setObjectName(QStringLiteral("stationName_%1").arg(station.stationId));
        layout->addWidget(name);
        layout->addWidget(new QLabel(
            QStringLiteral("%1 元/度 · 共 %2 桩 / 空闲 %3 · %4 km")
                .arg(station.priceFenPerKwh / 100.0, 0, 'f', 2)
                .arg(station.chargerCount)
                .arg(station.idleCount)
                .arg(station.distanceKm.value_or(0.0), 0, 'f', 2), card));
        auto *prediction = new QLabel(forecastTextForStation(station), card);
        prediction->setObjectName(QStringLiteral("forecastLabel_%1").arg(station.stationId));
        layout->addWidget(prediction);
        auto *open = new QPushButton(QStringLiteral("查看充电桩"), card);
        open->setObjectName(QStringLiteral("stationButton_%1").arg(station.stationId));
        connect(open, &QPushButton::clicked, this, [this, station] {
            requestStationDetail(station);
        });
        layout->addWidget(open);
        stationLayout_->addWidget(card);
    }
    stationLayout_->addStretch();
}

void NearbyPage::clearForecastCache()
{
    forecast_ = {};
    forecastStationFacts_.clear();
    forecastSource_->setText(QStringLiteral("暂无预测来源"));
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
    searchButton_->setEnabled(!pending);
    addressBox_->setEnabled(!pending);
    searchButton_->setText(pending ? QStringLiteral("加载中…") : QStringLiteral("查找附近充电站"));
}

void NearbyPage::setDetailPending(bool pending)
{
    setDetailControlsEnabled(!pending);
    if (pending) {
        detailStatus_->setText(QStringLiteral("正在加载充电桩…"));
    }
}

void NearbyPage::setDetailControlsEnabled(bool enabled)
{
    const auto buttons = findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        const QString name = button->objectName();
        if (name.startsWith(QStringLiteral("stationButton_"))
            || name.startsWith(QStringLiteral("chargerButton_"))
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
