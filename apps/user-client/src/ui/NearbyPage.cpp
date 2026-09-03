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
#include <utility>

namespace {

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
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

} // namespace

NearbyPage::NearbyPage(UserApi *userApi, TencentMapClient *mapClient, QWidget *parent)
    : QWidget(parent)
    , userApi_(userApi)
    , mapClient_(mapClient)
    , addressBox_(new QComboBox(this))
    , searchButton_(new QPushButton(QStringLiteral("查找附近充电站"), this))
    , connectionBanner_(new QLabel(QStringLiteral("服务器连接中…"), this))
    , statusLabel_(new QLabel(QStringLiteral("请选择预设地点或输入地址"), this))
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
    connectionBanner_->setObjectName(QStringLiteral("nearbyConnectionBanner"));
    statusLabel_->setObjectName(QStringLiteral("nearbyStatus"));
    statusLabel_->setWordWrap(true);
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
    layout->addWidget(statusLabel_);
    layout->addWidget(stationScroll, 1);
    layout->addWidget(detailStatus_);
    layout->addWidget(detailContainer);

    connect(searchButton_, &QPushButton::clicked, this, &NearbyPage::searchCurrentAddress);
    if (mapClient_ != nullptr) {
        connect(mapClient_, &TencentMapClient::geocodeSucceeded, this,
                [this](const QString &requestId, ev::user::GeoPoint origin) {
            if (requestId != pendingGeocodeId_) {
                return;
            }
            pendingGeocodeId_.clear();
            abandonChargeRefresh();
            cancelPendingStationList();
            ++originGeneration_;
            invalidateSelection();
            origin_ = origin;
            pendingStationsRequestId_.clear();
            pendingForecastRequestId_.clear();
            pendingDetailRequestId_.clear();
            pendingDetailOrigin_.reset();
            displayedDetailOrigin_.reset();
            forecast_ = {};
            stations_.clear();
            rebuildStationCards();
            clearLayout(detailLayout_);
            setDetailControlsEnabled(false);
            detailStatus_->setText(QStringLiteral("起点已变更，请重新选择充电站"));
            statusLabel_->setText(QStringLiteral("定位成功：%1, %2，正在加载附近站点…")
                                      .arg(origin.latitude, 0, 'f', 6)
                                      .arg(origin.longitude, 0, 'f', 6));
            requestNearbyStations(origin);
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
            const std::optional<quint64> forecastAttempt = chargeRefreshAttemptId_;
            if (pendingStationsOriginGeneration_ != originGeneration_
                || pendingStationsSearchGeneration_ != searchGeneration_) {
                pendingStationsRequestId_.clear();
                pendingStationsSelectionGeneration_.reset();
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
                setSearchPending(false);
                finishChargeRefreshUnavailable();
                return;
            }
            const std::optional<quint64> chargeGeneration =
                pendingStationsSelectionGeneration_;
            pendingStationsRequestId_.clear();
            pendingStationsSelectionGeneration_.reset();
            if (!chargeRefresh) {
                pendingDetailRequestId_.clear();
                pendingDetailOrigin_.reset();
            }
            const bool containsExpectedStation = !chargeRefreshStationId_.has_value()
                || std::any_of(result.stations.cbegin(), result.stations.cend(),
                               [this](const ev::user::Station &station) {
                    return station.stationId == *chargeRefreshStationId_;
                });
            applyStations(std::move(result), !chargeRefresh);
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
            if (!chargeRefresh) {
                clearLayout(detailLayout_);
                detailStatus_->setText(QStringLiteral("请选择充电站查看充电桩"));
            }
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
    if (!station.forecastEnabled || !forecast.forecastRun.has_value()) {
        return QStringLiteral("暂无预测");
    }
    for (const auto &record : forecast.records) {
        if (record.stationId == station.stationId && record.horizonH == 1) {
            QString text = QStringLiteral("1小时预测：繁忙 %1 / 空闲 %2，拥堵%3")
                               .arg(record.predictedBusyCount)
                               .arg(record.predictedIdleCount)
                               .arg(congestionText(record.congestionLevel));
            if (forecast.forecastRun->stale) {
                text += QStringLiteral("（预测已过期）");
            }
            return text;
        }
    }
    return QStringLiteral("暂无预测");
}

void NearbyPage::setConnectionAvailable(bool available)
{
    connectionBanner_->setText(available
        ? QStringLiteral("服务器已连接")
        : QStringLiteral("服务器连接不可用"));
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
    setSearchPending(false);
}

void NearbyPage::refreshAfterCharge(ev::user::GeoPoint origin, qint64 stationId,
                                    quint64 selectionGeneration, quint64 refreshAttemptId)
{
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
    abandonChargeRefresh();
    cancelPendingStationList();
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
    rebuildStationCards();
}

void NearbyPage::displayStationDetail(ev::user::StationDetailResult result)
{
    abandonChargeRefresh();
    applyStationDetail(std::move(result), true);
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
            QStringLiteral("%1 · %2 · %3 kW · %4")
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
    quint64 refreshAttemptId)
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
    setSearchPending(true);
    statusLabel_->setText(QStringLiteral("正在加载附近充电站…"));
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
    pendingForecastRequestId_.clear();
    pendingForecastSelectionGeneration_.reset();
    pendingForecastRefreshAttemptId_.reset();
    forecast_ = {};
    rebuildStationCards();
    if (!pendingDetailRequestId_.isEmpty()) {
        invalidateSelection();
        pendingDetailRequestId_.clear();
        pendingDetailOrigin_.reset();
        setDetailPending(false);
    }
    pendingStationsOriginGeneration_ = originGeneration_;
    pendingStationsSearchGeneration_ = searchGeneration_;
    pendingStationsRequestId_ = userApi_->loadNearbyStations(origin);
    if (pendingStationsRequestId_.isEmpty()) {
        pendingStationsSelectionGeneration_.reset();
        setSearchPending(false);
        showError(QStringLiteral("请求失败，请重试"));
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

void NearbyPage::invalidateSelection()
{
    ++selectionGeneration_;
    emit selectionInvalidated(selectionGeneration_);
}

void NearbyPage::rebuildStationCards()
{
    clearLayout(stationLayout_);
    for (const auto &station : stations_) {
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
        auto *prediction = new QLabel(forecastText(station, forecast_), card);
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
    if (!pendingStationsRequestId_.isEmpty()
        && error.requestId == pendingStationsRequestId_) {
        const bool chargeRefresh = pendingStationsSelectionGeneration_.has_value();
        if (pendingStationsOriginGeneration_ != originGeneration_
            || pendingStationsSearchGeneration_ != searchGeneration_) {
            pendingStationsRequestId_.clear();
            pendingStationsSelectionGeneration_.reset();
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
            setSearchPending(false);
            finishChargeRefreshUnavailable();
            return;
        }
        pendingStationsRequestId_.clear();
        pendingStationsSelectionGeneration_.reset();
        setSearchPending(false);
        showError(errorText(error));
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
        showError(errorText(error));
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
