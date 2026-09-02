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
            ++originGeneration_;
            ++selectionGeneration_;
            origin_ = origin;
            pendingStationsRequestId_.clear();
            pendingForecastRequestId_.clear();
            pendingDetailRequestId_.clear();
            pendingDetailOrigin_.reset();
            displayedDetailOrigin_.reset();
            forecast_ = {};
            rebuildStationCards();
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
            if (requestId != pendingStationsRequestId_
                || pendingStationsOriginGeneration_ != originGeneration_
                || pendingStationsSearchGeneration_ != searchGeneration_) {
                return;
            }
            pendingStationsRequestId_.clear();
            ++selectionGeneration_;
            pendingDetailRequestId_.clear();
            pendingDetailOrigin_.reset();
            displayStations(std::move(result));
            setSearchPending(false);
            clearLayout(detailLayout_);
            detailStatus_->setText(QStringLiteral("请选择充电站查看充电桩"));
            const int forecastStations = std::count_if(
                stations_.cbegin(), stations_.cend(), [](const auto &station) {
                    return station.forecastEnabled;
            });
            if (forecastStations == 6) {
                pendingForecastOriginGeneration_ = originGeneration_;
                pendingForecastSearchGeneration_ = searchGeneration_;
                pendingForecastRequestId_ = userApi_->loadLatestForecast(requestId);
                if (pendingForecastRequestId_.isEmpty()) {
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

void NearbyPage::displayStations(ev::user::StationListResult result)
{
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
                emit chargerSelected({*selectionOrigin, station, charger});
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

void NearbyPage::requestNearbyStations(const ev::user::GeoPoint &origin)
{
    if (userApi_ == nullptr) {
        return;
    }
    setSearchPending(true);
    statusLabel_->setText(QStringLiteral("正在加载附近充电站…"));
    ++searchGeneration_;
    pendingForecastRequestId_.clear();
    forecast_ = {};
    rebuildStationCards();
    if (!pendingDetailRequestId_.isEmpty()) {
        ++selectionGeneration_;
        pendingDetailRequestId_.clear();
        pendingDetailOrigin_.reset();
        setDetailPending(false);
    }
    pendingStationsOriginGeneration_ = originGeneration_;
    pendingStationsSearchGeneration_ = searchGeneration_;
    pendingStationsRequestId_ = userApi_->loadNearbyStations(origin);
    if (pendingStationsRequestId_.isEmpty()) {
        setSearchPending(false);
        showError(QStringLiteral("请求失败，请重试"));
    }
}

void NearbyPage::requestStationDetail(const ev::user::Station &station)
{
    if (userApi_ == nullptr || !origin_.has_value()) {
        return;
    }
    ++selectionGeneration_;
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
        && error.requestId == pendingStationsRequestId_
        && pendingStationsOriginGeneration_ == originGeneration_
        && pendingStationsSearchGeneration_ == searchGeneration_) {
        pendingStationsRequestId_.clear();
        setSearchPending(false);
        showError(errorText(error));
        return;
    }
    if (!pendingForecastRequestId_.isEmpty()
        && error.requestId == pendingForecastRequestId_
        && pendingForecastOriginGeneration_ == originGeneration_
        && pendingForecastSearchGeneration_ == searchGeneration_) {
        pendingForecastRequestId_.clear();
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
