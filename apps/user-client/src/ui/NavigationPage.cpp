#include "ui/NavigationPage.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QUuid>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <cmath>
#include <utility>

namespace {

QString compactJson(const QJsonValue &value)
{
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

QString operationIdOrNew(const QString &operationId)
{
    return operationId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : operationId;
}

bool validPoint(const ev::user::GeoPoint &point)
{
    return std::isfinite(point.latitude) && point.latitude >= -90.0 && point.latitude <= 90.0
        && std::isfinite(point.longitude) && point.longitude >= -180.0 && point.longitude <= 180.0;
}

QJsonObject pointObject(const ev::user::GeoPoint &point)
{
    return {{QStringLiteral("lat"), point.latitude}, {QStringLiteral("lng"), point.longitude}};
}

} // namespace

void RouteOperationTracker::begin(QString operationId, LastRoute candidate)
{
    currentOperationId_ = std::move(operationId);
    pendingRoute_ = std::move(candidate);
    retryRoute_ = pendingRoute_;
}

bool RouteOperationTracker::complete(const QString &operationId, const QString &state,
                                     const QDateTime &generatedAt)
{
    if (operationId != currentOperationId_ || !pendingRoute_.has_value()
        || (state != QStringLiteral("success") && state != QStringLiteral("error"))) {
        return false;
    }
    if (state == QStringLiteral("success")) {
        pendingRoute_->generatedAt = generatedAt;
        lastSuccessfulRoute_ = pendingRoute_;
    }
    currentOperationId_.clear();
    pendingRoute_.reset();
    return true;
}

std::optional<LastRoute> RouteOperationTracker::lastSuccessfulRoute() const
{
    return lastSuccessfulRoute_;
}

std::optional<LastRoute> RouteOperationTracker::retryRoute() const
{
    return retryRoute_;
}

void RouteOperationTracker::invalidatePending()
{
    currentOperationId_.clear();
    pendingRoute_.reset();
}

void RouteOperationTracker::resetForSession()
{
    currentOperationId_.clear();
    pendingRoute_.reset();
    retryRoute_.reset();
    lastSuccessfulRoute_.reset();
}

NavigationPage::NavigationPage(QString mapKey, QWidget *parent)
    : NavigationPage(std::move(mapKey), {}, parent)
{
}

NavigationPage::NavigationPage(QString mapKey, QString documentReadyBootstrapScript,
                               QWidget *parent)
    : QWidget(parent)
    , mapKey_(std::move(mapKey))
    , documentReadyBootstrapScript_(std::move(documentReadyBootstrapScript))
    , view_(new QWebEngineView(this))
    , statusLabel_(new QLabel(QStringLiteral("正在加载导航页面…"), this))
    , cacheLabel_(new QLabel(QStringLiteral("暂无成功路线"), this))
    , destinationLabel_(new QLabel(QStringLiteral("选择站点后开始导航"), this))
    , retryButton_(new QPushButton(QStringLiteral("重试"), this))
    , modeBox_(new QComboBox(this))
    , callbackGate_(std::make_shared<CallbackGate>())
{
    setObjectName(QStringLiteral("navigationPage"));
    statusLabel_->setObjectName(QStringLiteral("navigationStatus"));
    statusLabel_->setWordWrap(true);
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setProperty("role", QStringLiteral("secondary"));
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    cacheLabel_->setObjectName(QStringLiteral("lastRouteLabel"));
    cacheLabel_->setWordWrap(true);
    cacheLabel_->setTextFormat(Qt::PlainText);
    cacheLabel_->setProperty("role", QStringLiteral("secondary"));
    cacheLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    cacheLabel_->hide();
    destinationLabel_->setObjectName(QStringLiteral("navigationDestination"));
    destinationLabel_->setProperty("role", QStringLiteral("sectionTitle"));
    destinationLabel_->setWordWrap(true);
    destinationLabel_->setTextFormat(Qt::PlainText);
    destinationLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    retryButton_->setObjectName(QStringLiteral("navigationRetryButton"));
    retryButton_->hide();
    retryButton_->setProperty("role", QStringLiteral("outline"));
    view_->setObjectName(QStringLiteral("navigationWebView"));
    view_->setMinimumHeight(240);
    modeBox_->setObjectName(QStringLiteral("routeModeBox"));
    modeBox_->addItem(QStringLiteral("驾车"), QStringLiteral("driving"));
    modeBox_->addItem(QStringLiteral("步行"), QStringLiteral("walking"));
    modeBox_->setAccessibleName(QStringLiteral("出行方式"));
    modeBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *backButton = new QPushButton(this);
    backButton->setObjectName(QStringLiteral("navigationBackButton"));
    backButton->setAccessibleName(QStringLiteral("返回附近站点"));
    backButton->setToolTip(QStringLiteral("返回附近站点"));
    backButton->setIcon(QIcon(QStringLiteral(":/ui/back.svg")));
    backButton->setIconSize(QSize(22, 22));
    backButton->setFixedWidth(44);
    backButton->setProperty("role", QStringLiteral("back"));

    auto *header = new QHBoxLayout;
    header->addWidget(backButton);
    auto *title = new QLabel(QStringLiteral("站点导航"), this);
    title->setProperty("role", QStringLiteral("sectionTitle"));
    title->setAlignment(Qt::AlignCenter);
    header->addWidget(title, 1);
    header->addSpacing(44);
    auto *controls = new QHBoxLayout;
    auto *modeLabel = new QLabel(QStringLiteral("出行方式"), this);
    modeLabel->setProperty("role", QStringLiteral("secondary"));
    controls->addWidget(modeLabel);
    controls->addWidget(modeBox_, 1);
    controls->addWidget(retryButton_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 16);
    layout->setSpacing(12);
    layout->addLayout(header);
    auto *summary = new QFrame(this);
    summary->setProperty("role", QStringLiteral("card"));
    auto *summaryLayout = new QVBoxLayout(summary);
    summaryLayout->setContentsMargins(16, 14, 16, 14);
    summaryLayout->setSpacing(8);
    summaryLayout->addWidget(destinationLabel_);
    summaryLayout->addWidget(statusLabel_);
    summaryLayout->addWidget(cacheLabel_);
    layout->addWidget(summary);
    layout->addLayout(controls);
    layout->addWidget(view_, 1);

    view_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    connect(backButton, &QPushButton::clicked, this, [this] {
        deactivate();
        emit backRequested();
    });
    connect(view_, &QWebEngineView::loadFinished, this, [this](bool success) {
        pageLoaded_ = success;
        configurationStarted_ = false;
        configured_ = false;
        if (!success) {
            showFailure(QStringLiteral("导航页面加载失败，请重试"));
            return;
        }
        if (documentReadyBootstrapScript_.isEmpty()) {
            configureForCurrentLoad();
            return;
        }
        const QPointer<NavigationPage> guardedPage(this);
        const auto gate = callbackGate_;
        view_->page()->runJavaScript(documentReadyBootstrapScript_,
                                     [guardedPage, gate](const QVariant &) {
            if (!gate->active || guardedPage.isNull()) {
                return;
            }
            guardedPage->configureForCurrentLoad();
        });
    });
    connect(retryButton_, &QPushButton::clicked, this, [this] {
        if (!configured_) {
            const auto retry = routeTracker_.retryRoute();
            if (retry.has_value()) {
                routeOperationId_ = QStringLiteral("route-%1").arg(++nextOperationId_);
                routeTracker_.begin(routeOperationId_, *retry);
            }
            view_->reload();
            return;
        }
        const auto retry = routeTracker_.retryRoute();
        if (retry.has_value()) {
            showRoute(retry->origin,
                      ev::user::Station{0, retry->stationName, {}, retry->destination.latitude,
                                        retry->destination.longitude}, retry->mode);
        }
    });
    connect(modeBox_, &QComboBox::currentIndexChanged, this, [this] {
        const auto route = routeTracker_.retryRoute();
        if (route.has_value()) {
            showRoute(route->origin,
                      ev::user::Station{0, route->stationName, {}, route->destination.latitude,
                                        route->destination.longitude},
                      modeBox_->currentData().toString());
        }
    });
    view_->setUrl(pageUrl());
}

NavigationPage::~NavigationPage()
{
    callbackGate_->active = false;
    routeTracker_.invalidatePending();
    routeOperationId_.clear();
    configureOperationId_.clear();
    configured_ = false;
    pageLoaded_ = false;
}

QUrl NavigationPage::pageUrl()
{
    return QUrl(QStringLiteral("qrc:/map/navigation.html"));
}

QString NavigationPage::buildConfigureMapScript(const QString &key, const QString &operationId)
{
    const QJsonObject operation{
        {QStringLiteral("operationId"), operationIdOrNew(operationId)},
        {QStringLiteral("config"), QJsonObject{{QStringLiteral("key"), key}}},
    };
    return QStringLiteral(
        "(()=>{const operation=%1;window.__qtOperations=window.__qtOperations||{};"
        "Promise.resolve().then(()=>window.configureMap(operation.config))"
        ".then(()=>{window.__qtOperations[operation.operationId]={state:'success'};})"
        ".catch(()=>{window.__qtOperations[operation.operationId]={state:'error'};});"
        "return operation.operationId;})()")
        .arg(compactJson(operation));
}

QString NavigationPage::buildRenderRouteScript(
    const ev::user::GeoPoint &from, const ev::user::GeoPoint &to, const QString &mode,
    const QString &stationName, const QString &operationId, QString *error)
{
    if (error != nullptr) {
        error->clear();
    }
    if (!validPoint(from) || !validPoint(to)) {
        if (error != nullptr) {
            *error = QStringLiteral("起点或终点坐标无效");
        }
        return {};
    }
    if (mode != QStringLiteral("driving") && mode != QStringLiteral("walking")) {
        if (error != nullptr) {
            *error = QStringLiteral("仅支持驾车或步行导航");
        }
        return {};
    }
    const QJsonObject operation{
        {QStringLiteral("operationId"), operationIdOrNew(operationId)},
        {QStringLiteral("route"), QJsonObject{
             {QStringLiteral("from"), pointObject(from)},
             {QStringLiteral("to"), pointObject(to)},
             {QStringLiteral("mode"), mode},
             {QStringLiteral("stationName"), stationName},
         }},
    };
    return QStringLiteral(
        "(()=>{const operation=%1;window.__qtOperations=window.__qtOperations||{};"
        "try{Promise.resolve(window.renderRoute(operation.route,operation.operationId))"
        ".then(()=>{window.__qtOperations[operation.operationId]={state:'success'};})"
        ".catch(()=>{window.__qtOperations[operation.operationId]={state:'error'};});}"
        "catch(_){window.__qtOperations[operation.operationId]={state:'error'};}"
        "return operation.operationId;})()")
        .arg(compactJson(operation));
}

QString NavigationPage::buildOperationStatusScript(const QString &operationId)
{
    return QStringLiteral(
        "(()=>{const operationId=%1;return window.__qtOperations&&"
        "window.__qtOperations[operationId]?window.__qtOperations[operationId]:null;})()")
        .arg(compactJson(operationId));
}

QString NavigationPage::buildInvalidateRouteScript(const QString &operationId)
{
    return QStringLiteral(
        "(()=>{const operationId=%1;return typeof window.invalidateRouteAttempt==='function'"
        "?window.invalidateRouteAttempt(operationId):false;})()")
        .arg(compactJson(operationId));
}

QString NavigationPage::buildResetRouteSessionScript()
{
    return QStringLiteral(
        "(()=>typeof window.resetRouteSession==='function'"
        "?window.resetRouteSession():false)()");
}

std::optional<LastRoute> NavigationPage::lastSuccessfulRoute() const
{
    return routeTracker_.lastSuccessfulRoute();
}

void NavigationPage::showRoute(ev::user::GeoPoint origin, ev::user::Station station, QString mode)
{
    invalidateRouteAttempt();
    routeTracker_.invalidatePending();
    routeOperationId_.clear();
    const ev::user::GeoPoint destination{station.latitude, station.longitude};
    QString error;
    const QString operationId = QStringLiteral("route-%1").arg(++nextOperationId_);
    const QString script = buildRenderRouteScript(
        origin, destination, mode, station.name, operationId, &error);
    if (script.isEmpty()) {
        showFailure(error);
        return;
    }
    routeOperationId_ = operationId;
    destinationLabel_->setText(station.name);
    routeTracker_.begin(operationId, {origin, destination, station.name, mode, {}});
    const int modeIndex = modeBox_->findData(mode);
    if (modeIndex >= 0 && modeBox_->currentIndex() != modeIndex) {
        const QSignalBlocker blocker(modeBox_);
        modeBox_->setCurrentIndex(modeIndex);
    }
    if (!pageLoaded_ || !configured_) {
        setStatus(QStringLiteral("地图配置完成后将规划路线"));
        return;
    }
    view_->page()->runJavaScript(script);
    setStatus(QStringLiteral("正在规划路线…"));
    retryButton_->hide();
    pollOperation(operationId, OperationKind::Route);
}

void NavigationPage::deactivate()
{
    invalidateRouteAttempt();
    routeTracker_.invalidatePending();
    routeOperationId_.clear();
    retryButton_->hide();
    setStatus(QStringLiteral("导航已暂停"));
}

void NavigationPage::resetForSession()
{
    invalidateRouteAttempt();
    routeTracker_.resetForSession();
    routeOperationId_.clear();
    retryButton_->hide();
    cacheLabel_->setText(QStringLiteral("暂无成功路线"));
    cacheLabel_->hide();
    destinationLabel_->setText(QStringLiteral("选择站点后开始导航"));
    setStatus(QStringLiteral("导航会话已重置"));
    if (view_ != nullptr && view_->page() != nullptr) {
        view_->page()->runJavaScript(buildResetRouteSessionScript());
    }
}

void NavigationPage::configureForCurrentLoad()
{
    if (!pageLoaded_ || configurationStarted_) {
        return;
    }
    configurationStarted_ = true;
    configureOperationId_ = QStringLiteral("configure-%1").arg(++nextOperationId_);
    view_->page()->runJavaScript(buildConfigureMapScript(mapKey_, configureOperationId_));
    setStatus(QStringLiteral("正在配置腾讯地图…"));
    pollOperation(configureOperationId_, OperationKind::Configure);
}

void NavigationPage::executePendingRoute()
{
    const auto route = routeTracker_.retryRoute();
    if (!route.has_value() || routeOperationId_.isEmpty()) {
        return;
    }
    QString error;
    const QString script = buildRenderRouteScript(
        route->origin, route->destination, route->mode, route->stationName, routeOperationId_, &error);
    if (script.isEmpty()) {
        showFailure(error);
        return;
    }
    routeTracker_.begin(routeOperationId_, *route);
    view_->page()->runJavaScript(script);
    setStatus(QStringLiteral("正在规划路线…"));
    pollOperation(routeOperationId_, OperationKind::Route);
}

void NavigationPage::pollOperation(const QString &operationId, OperationKind kind, int attemptsRemaining)
{
    if (attemptsRemaining <= 0) {
        finishOperation(operationId, kind, QStringLiteral("error"));
        return;
    }
    QTimer::singleShot(50, this, [this, operationId, kind, attemptsRemaining] {
        const QPointer<NavigationPage> guardedPage(this);
        const auto gate = callbackGate_;
        view_->page()->runJavaScript(buildOperationStatusScript(operationId),
            [guardedPage, gate, operationId, kind, attemptsRemaining](const QVariant &result) {
                if (!gate->active || guardedPage.isNull()) {
                    return;
                }
                const QVariantMap record = result.toMap();
                const QString state = record.value(QStringLiteral("state")).toString();
                if (state == QStringLiteral("success") || state == QStringLiteral("error")) {
                    guardedPage->finishOperation(operationId, kind, state);
                    return;
                }
                guardedPage->pollOperation(operationId, kind, attemptsRemaining - 1);
            });
    });
}

void NavigationPage::finishOperation(const QString &operationId, OperationKind kind, const QString &state)
{
    if (kind == OperationKind::Configure) {
        if (operationId != configureOperationId_) {
            return;
        }
        if (state == QStringLiteral("success")) {
            configured_ = true;
            setStatus(QStringLiteral("地图已加载"));
            retryButton_->hide();
            executePendingRoute();
        } else {
            showFailure(QStringLiteral("地图加载失败，请检查网络后重试"));
        }
        return;
    }
    if (!routeTracker_.complete(operationId, state, QDateTime::currentDateTime())) {
        return;
    }
    if (state == QStringLiteral("success")) {
        setStatus(QStringLiteral("路线规划成功"));
        retryButton_->hide();
        updateLastSuccessLabel();
    } else {
        if (view_ != nullptr && view_->page() != nullptr) {
            view_->page()->runJavaScript(buildInvalidateRouteScript(operationId));
        }
        showFailure(QStringLiteral("路线规划失败，请检查网络后重试"));
    }
}

void NavigationPage::invalidateRouteAttempt()
{
    if (routeOperationId_.isEmpty() || view_ == nullptr || view_->page() == nullptr) {
        return;
    }
    view_->page()->runJavaScript(buildInvalidateRouteScript(routeOperationId_));
}

void NavigationPage::showFailure(const QString &reason)
{
    setStatus(reason, true);
    retryButton_->show();
    updateLastSuccessLabel();
}

void NavigationPage::setStatus(const QString &text, bool failed)
{
    statusLabel_->setText(text);
    statusLabel_->setProperty("role", failed ? QStringLiteral("danger") : QStringLiteral("secondary"));
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
}

void NavigationPage::updateLastSuccessLabel()
{
    const auto last = routeTracker_.lastSuccessfulRoute();
    if (!last.has_value()) {
        cacheLabel_->setText(QStringLiteral("暂无成功路线"));
        cacheLabel_->hide();
        return;
    }
    cacheLabel_->show();
    cacheLabel_->setText(QStringLiteral("上次成功路线：%1（%2，生成于 %3）")
        .arg(last->stationName,
             last->mode == QStringLiteral("walking") ? QStringLiteral("步行") : QStringLiteral("驾车"),
             last->generatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
}
