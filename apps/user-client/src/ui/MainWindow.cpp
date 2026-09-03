#include "ui/MainWindow.h"

#include "net/TcpJsonClient.h"
#include "net/TencentMapClient.h"
#include "services/UserApi.h"
#include "ui/ChargePage.h"
#include "ui/HistoryPage.h"
#include "ui/LoginPage.h"
#include "ui/NavigationPage.h"
#include "ui/NearbyPage.h"
#include "ui/ProfilePage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(UserAppConfig config, QWidget *parent)
    : QMainWindow(parent)
    , client_(new TcpJsonClient(this))
    , userApi_(new UserApi(client_, this))
    , mapClient_(new TencentMapClient(config.tencentMapKey, nullptr,
                                      TencentMapClient::productionEndpoint(), 5'000, this))
    , mapKey_(config.tencentMapKey) {
    setWindowTitle(QStringLiteral("电动汽车充电"));

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);

    auto *configurationMessage = new QLabel(centralWidget);
    configurationMessage->setObjectName(QStringLiteral("configurationMessage"));
    configurationMessage->setWordWrap(true);
    if (config.isValid()) {
        configurationMessage->setText(QStringLiteral("配置已就绪"));
    } else {
        configurationMessage->setText(QStringLiteral("配置错误：\n%1").arg(config.validationMessage()));
    }
    layout->addWidget(configurationMessage);

    authenticatedNavigation_ = new QWidget(centralWidget);
    authenticatedNavigation_->setObjectName(QStringLiteral("authenticatedNavigation"));
    auto *navigationLayout = new QHBoxLayout(authenticatedNavigation_);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    nearbyNavigationButton_ = new QPushButton(QStringLiteral("附近充电站"), authenticatedNavigation_);
    nearbyNavigationButton_->setObjectName(QStringLiteral("nearbyNavigationButton"));
    currentOrderNavigationButton_ = new QPushButton(QStringLiteral("当前订单"), authenticatedNavigation_);
    currentOrderNavigationButton_->setObjectName(QStringLiteral("currentOrderNavigationButton"));
    currentOrderNavigationButton_->setVisible(false);
    historyNavigationButton_ = new QPushButton(QStringLiteral("历史订单"), authenticatedNavigation_);
    historyNavigationButton_->setObjectName(QStringLiteral("historyNavigationButton"));
    profileNavigationButton_ = new QPushButton(QStringLiteral("我的账户"), authenticatedNavigation_);
    profileNavigationButton_->setObjectName(QStringLiteral("profileNavigationButton"));
    navigationLayout->addWidget(nearbyNavigationButton_);
    navigationLayout->addWidget(currentOrderNavigationButton_);
    navigationLayout->addWidget(historyNavigationButton_);
    navigationLayout->addWidget(profileNavigationButton_);
    navigationLayout->addStretch();
    authenticatedNavigation_->setVisible(false);
    layout->addWidget(authenticatedNavigation_);

    pages_ = new QStackedWidget(centralWidget);
    pages_->setObjectName(QStringLiteral("mainPages"));
    loginPage_ = new LoginPage(pages_);
    nearbyPage_ = new NearbyPage(userApi_, mapClient_, pages_);
    historyPage_ = new HistoryPage(userApi_, pages_);
    profilePage_ = new ProfilePage(userApi_, pages_);
    chargePage_ = new ChargePage(userApi_, pages_);
    pages_->addWidget(loginPage_);
    pages_->addWidget(nearbyPage_);
    pages_->addWidget(historyPage_);
    pages_->addWidget(profilePage_);
    pages_->addWidget(chargePage_);
    pages_->setCurrentWidget(loginPage_);
    layout->addWidget(pages_);

    connect(loginPage_, &LoginPage::loginRequested, userApi_, &UserApi::loginByPhone);
    connect(userApi_, &UserApi::loginPendingChanged, loginPage_, &LoginPage::setPending);
    connect(userApi_, &UserApi::connectionChanged, loginPage_, &LoginPage::setConnectionAvailable);
    connect(userApi_, &UserApi::connectionChanged, nearbyPage_, &NearbyPage::setConnectionAvailable);
    connect(userApi_, &UserApi::connectionChanged, chargePage_, &ChargePage::setConnectionAvailable);
    connect(userApi_, &UserApi::requestFailed, this, [this](const ev::user::ApiError &error) {
        if (pages_->currentWidget() == loginPage_) {
            loginPage_->setError(error.message);
        }
    });
    connect(userApi_, &UserApi::loginSucceeded, this, [this](const ev::user::User &) {
        rememberedSelection_.reset();
        deferredSelectionInvalidation_.reset();
        mutationAuthorityStamps_.clear();
        reconnectCurrentContext_.reset();
        reconnectCurrentOwnerOrderId_ = 0;
        authoritativeActiveOrder_.reset();
        hasActiveOrder_ = false;
        ++authorityRevision_;
        updateAuthenticatedNavigation();
        guardContext_ = userApi_->loadCurrentOrder(
            0, 0, ev::user::ChargeOperation::Guard);
    });
    connect(userApi_, &UserApi::currentOrderLoaded, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::CurrentOrderResult &result) {
        if (!guardContext_.has_value() || context != *guardContext_) {
            return;
        }
        guardContext_.reset();
        authenticatedNavigation_->setVisible(true);
        if (result.order.has_value()) {
            std::optional<ev::user::StationSelection> matching;
            if (rememberedSelection_.has_value()
                && rememberedSelection_->station.stationId == result.order->stationId
                && rememberedSelection_->charger.chargerId == result.order->chargerId) {
                matching = rememberedSelection_;
            } else {
                rememberedSelection_.reset();
            }
            chargePage_->enterGuardOrder(*result.order, matching);
        }
        if (result.order.has_value()) {
            pages_->setCurrentWidget(chargePage_);
        } else {
            pages_->setCurrentWidget(nearbyPage_);
        }
    });
    connect(userApi_, &UserApi::chargeRequestFailed, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::ApiError &, bool) {
        if (!guardContext_.has_value() || context != *guardContext_) {
            return;
        }
        guardContext_.reset();
        clearActiveOrder(0, true);
        authenticatedNavigation_->setVisible(false);
        pages_->setCurrentWidget(loginPage_);
        loginPage_->setError(QStringLiteral("当前订单加载失败，请重新登录"));
    });
    connect(nearbyNavigationButton_, &QPushButton::clicked, this, [this] {
        if (!hasActiveOrder_ && !chargeFlowBlocked_) {
            if (pages_->currentWidget() == historyPage_) {
                historyPage_->deactivate();
            }
            if (pages_->currentWidget() == chargePage_) {
                chargePage_->leavePage();
            }
            pages_->setCurrentWidget(nearbyPage_);
        }
    });
    connect(currentOrderNavigationButton_, &QPushButton::clicked, this, [this] {
        if (hasActiveOrder_ && !chargeFlowBlocked_ && authoritativeActiveOrder_.has_value()) {
            cancelReconnectCurrent();
            if (pages_->currentWidget() == historyPage_) {
                historyPage_->deactivate();
            }
            std::optional<ev::user::StationSelection> matching;
            if (rememberedSelection_.has_value()
                && rememberedSelection_->station.stationId == authoritativeActiveOrder_->stationId
                && rememberedSelection_->charger.chargerId == authoritativeActiveOrder_->chargerId) {
                matching = rememberedSelection_;
            }
            chargePage_->enterOrder(*authoritativeActiveOrder_, matching);
            chargePage_->resume();
            pages_->setCurrentWidget(chargePage_);
        }
    });
    connect(historyNavigationButton_, &QPushButton::clicked, this, [this] {
        if (chargeFlowBlocked_) {
            return;
        }
        if (pages_->currentWidget() == chargePage_) {
            chargePage_->leavePage();
        }
        pages_->setCurrentWidget(historyPage_);
        historyPage_->activate();
    });
    connect(profileNavigationButton_, &QPushButton::clicked, this, [this] {
        if (chargeFlowBlocked_) {
            return;
        }
        if (pages_->currentWidget() == chargePage_) {
            chargePage_->leavePage();
        }
        if (pages_->currentWidget() == historyPage_) {
            historyPage_->deactivate();
        }
        pages_->setCurrentWidget(profilePage_);
        profilePage_->refresh();
    });
    connect(nearbyPage_, &NearbyPage::chargerSelected, this,
            [this](const ev::user::StationSelection &selection) {
        if (hasActiveOrder_ || chargeFlowBlocked_ || reconnectCurrentContext_.has_value()) {
            return;
        }
        rememberedSelection_ = selection;
        if (pages_->currentWidget() == historyPage_) {
            historyPage_->deactivate();
        }
        chargePage_->enterSelection(selection);
        pages_->setCurrentWidget(chargePage_);
    });
    connect(nearbyPage_, &NearbyPage::selectionInvalidated, this,
            [this](quint64 selectionGeneration) {
        if (chargeFlowBlocked_) {
            if (!deferredSelectionInvalidation_.has_value()
                || selectionGeneration > *deferredSelectionInvalidation_) {
                deferredSelectionInvalidation_ = selectionGeneration;
            }
            return;
        }
        applySelectionInvalidation(selectionGeneration);
    });
    connect(chargePage_, &ChargePage::nearbyRefreshRequested,
            nearbyPage_, &NearbyPage::refreshAfterCharge);
    connect(chargePage_, &ChargePage::nearbyDetailRefreshReady,
            nearbyPage_, &NearbyPage::applyChargeStationDetail);
    connect(chargePage_, &ChargePage::nearbyDetailRefreshFailed,
            nearbyPage_, &NearbyPage::failChargeStationDetail);
    connect(nearbyPage_, &NearbyPage::chargeRefreshCommitted,
            chargePage_, &ChargePage::nearbyRefreshCommitted);
    connect(nearbyPage_, &NearbyPage::chargeRefreshFailed,
            chargePage_, &ChargePage::nearbyRefreshFailed);
    connect(nearbyPage_, &NearbyPage::chargeRefreshUnavailable,
            chargePage_, &ChargePage::nearbyRefreshUnavailable);
    connect(chargePage_, &ChargePage::rememberedSelectionInvalidated, this, [this] {
        rememberedSelection_.reset();
    });
    chargePage_->setNearbyRefreshAvailable(true);
    connect(chargePage_, &ChargePage::chargeSafeReadsInvalidated,
            nearbyPage_, &NearbyPage::cancelChargeRefresh);
    connect(chargePage_, &ChargePage::currentAuthorityObserved, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::CurrentOrderResult &result,
                   bool pageOwned, qint64 resolvedOwnerOrderId) {
        const bool reconnectOwned = reconnectCurrentContext_.has_value()
            && context == *reconnectCurrentContext_;
        const qint64 reconnectOwnerOrderId = reconnectCurrentOwnerOrderId_;
        if (reconnectOwned) {
            reconnectCurrentContext_.reset();
            reconnectCurrentOwnerOrderId_ = 0;
        }
        if (result.order.has_value()) {
            adoptActiveOrder(*result.order);
            return;
        }
        const bool guardOwned = guardContext_.has_value() && context == *guardContext_;
        if (guardOwned) {
            clearActiveOrder(0, true);
        } else if (reconnectOwned) {
            clearActiveOrder(reconnectOwnerOrderId);
        } else if (pageOwned) {
            clearActiveOrder(resolvedOwnerOrderId);
        }
    });
    connect(userApi_, &UserApi::chargeRequestFailed, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::ApiError &, bool) {
        if (reconnectCurrentContext_.has_value()
            && context == *reconnectCurrentContext_) {
            reconnectCurrentContext_.reset();
            reconnectCurrentOwnerOrderId_ = 0;
        }
    });
    connect(chargePage_, &ChargePage::mutationDispatched, this,
            [this](const ev::user::RequestContext &context, qint64 subjectOrderId) {
        mutationAuthorityStamps_.insert(
            context.requestId, {authorityRevision_, subjectOrderId});
    });
    connect(chargePage_, &ChargePage::mutationAuthorityObserved, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::Order &order) {
        const auto stamp = mutationAuthorityStamps_.constFind(context.requestId);
        if (stamp == mutationAuthorityStamps_.cend()) {
            return;
        }
        const bool newerDifferentAuthority = stamp->revisionAtDispatch != authorityRevision_
            && authoritativeActiveOrder_.has_value()
            && authoritativeActiveOrder_->orderId != order.orderId;
        if (newerDifferentAuthority) {
            return;
        }
        const bool active = order.status == QStringLiteral("reserved")
            || order.status == QStringLiteral("charging");
        if (active) {
            adoptActiveOrder(order);
        } else {
            clearActiveOrder(order.orderId);
        }
    });
    connect(chargePage_, &ChargePage::mutationFinished, this,
            [this](const ev::user::RequestContext &context) {
        mutationAuthorityStamps_.remove(context.requestId);
    });
    connect(chargePage_, &ChargePage::chargeFlowBlockedChanged, this, [this](bool blocked) {
        chargeFlowBlocked_ = blocked;
        if (!blocked && deferredSelectionInvalidation_.has_value()) {
            const quint64 selectionGeneration = *deferredSelectionInvalidation_;
            deferredSelectionInvalidation_.reset();
            applySelectionInvalidation(selectionGeneration);
        }
        updateAuthenticatedNavigation();
    });
    connect(chargePage_, &ChargePage::backRequested, this, [this](qint64) {
        const bool authoritativeActive = authoritativeActiveOrder_.has_value()
            && (authoritativeActiveOrder_->status == QStringLiteral("reserved")
                || authoritativeActiveOrder_->status == QStringLiteral("charging"));
        if (authoritativeActive) {
            std::optional<ev::user::StationSelection> matching;
            if (rememberedSelection_.has_value()
                && rememberedSelection_->station.stationId
                    == authoritativeActiveOrder_->stationId
                && rememberedSelection_->charger.chargerId
                    == authoritativeActiveOrder_->chargerId) {
                matching = rememberedSelection_;
            }
            chargePage_->enterOrder(*authoritativeActiveOrder_, matching);
            chargePage_->resume();
            pages_->setCurrentWidget(chargePage_);
            return;
        }
        chargePage_->leavePage();
        hasActiveOrder_ = false;
        authoritativeActiveOrder_.reset();
        rememberedSelection_.reset();
        nearbyNavigationButton_->setEnabled(true);
        currentOrderNavigationButton_->setVisible(false);
        updateAuthenticatedNavigation();
        pages_->setCurrentWidget(nearbyPage_);
    });
    connect(nearbyPage_, &NearbyPage::navigationRequested, this,
            [this](ev::user::GeoPoint origin, ev::user::Station station) {
        if (chargeFlowBlocked_) {
            return;
        }
        if (navigationPage_ == nullptr) {
            navigationPage_ = new NavigationPage(mapKey_, pages_);
            pages_->addWidget(navigationPage_);
            connect(navigationPage_, &NavigationPage::backRequested, this, [this] {
                pages_->setCurrentWidget(nearbyPage_);
            });
        }
        navigationPage_->showRoute(origin, station);
        pages_->setCurrentWidget(navigationPage_);
    });

    connect(userApi_, &UserApi::connectionChanged, this, [this](bool connected) {
        if (!connected || !userApi_->sessionUser().has_value()
            || pages_->currentWidget() == chargePage_
            || guardContext_.has_value()
            || reconnectCurrentContext_.has_value()) {
            return;
        }
        reconnectCurrentOwnerOrderId_ = authoritativeActiveOrder_.has_value()
            ? authoritativeActiveOrder_->orderId : 0;
        reconnectCurrentContext_ = userApi_->loadCurrentOrder(
            0, 0, ev::user::ChargeOperation::Reconcile);
        if (reconnectCurrentContext_->requestId.isEmpty()) {
            reconnectCurrentContext_.reset();
            reconnectCurrentOwnerOrderId_ = 0;
        }
        nearbyPage_->refreshAfterReconnect();
    });

    if (!config.serverHost.isEmpty() && config.serverPort != 0) {
        client_->configure(config.serverHost, config.serverPort);
        client_->connectToServer();
    }

    setCentralWidget(centralWidget);
}

void MainWindow::updateAuthenticatedNavigation()
{
    authenticatedNavigation_->setEnabled(!chargeFlowBlocked_);
    nearbyNavigationButton_->setEnabled(!chargeFlowBlocked_ && !hasActiveOrder_);
    currentOrderNavigationButton_->setVisible(hasActiveOrder_);
    currentOrderNavigationButton_->setEnabled(!chargeFlowBlocked_ && hasActiveOrder_);
    historyNavigationButton_->setEnabled(!chargeFlowBlocked_);
    profileNavigationButton_->setEnabled(!chargeFlowBlocked_);
}

void MainWindow::applySelectionInvalidation(quint64 selectionGeneration)
{
    rememberedSelection_.reset();
    chargePage_->invalidateSelection(selectionGeneration);
}

void MainWindow::adoptActiveOrder(const ev::user::Order &order)
{
    if (rememberedSelection_.has_value()
        && (rememberedSelection_->station.stationId != order.stationId
            || rememberedSelection_->charger.chargerId != order.chargerId)) {
        rememberedSelection_.reset();
    }
    authoritativeActiveOrder_ = order;
    hasActiveOrder_ = true;
    ++authorityRevision_;
    updateAuthenticatedNavigation();
}

void MainWindow::clearActiveOrder(qint64 resolvedOwnerOrderId, bool force)
{
    if (!force && authoritativeActiveOrder_.has_value()
        && (resolvedOwnerOrderId <= 0
            || authoritativeActiveOrder_->orderId != resolvedOwnerOrderId)) {
        return;
    }
    const bool changed = authoritativeActiveOrder_.has_value() || hasActiveOrder_;
    authoritativeActiveOrder_.reset();
    hasActiveOrder_ = false;
    if (changed || force) {
        ++authorityRevision_;
    }
    updateAuthenticatedNavigation();
}

void MainWindow::cancelReconnectCurrent()
{
    if (reconnectCurrentContext_.has_value()
        && !reconnectCurrentContext_->requestId.isEmpty()) {
        userApi_->cancelSafeRead(reconnectCurrentContext_->requestId);
    }
    reconnectCurrentContext_.reset();
    reconnectCurrentOwnerOrderId_ = 0;
}
