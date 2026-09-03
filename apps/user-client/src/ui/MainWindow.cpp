#include "ui/MainWindow.h"

#include "net/TcpJsonClient.h"
#include "net/TencentMapClient.h"
#include "services/UserApi.h"
#include "ui/ChargePage.h"
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
    profileNavigationButton_ = new QPushButton(QStringLiteral("我的账户"), authenticatedNavigation_);
    profileNavigationButton_->setObjectName(QStringLiteral("profileNavigationButton"));
    navigationLayout->addWidget(nearbyNavigationButton_);
    navigationLayout->addWidget(currentOrderNavigationButton_);
    navigationLayout->addWidget(profileNavigationButton_);
    navigationLayout->addStretch();
    authenticatedNavigation_->setVisible(false);
    layout->addWidget(authenticatedNavigation_);

    pages_ = new QStackedWidget(centralWidget);
    pages_->setObjectName(QStringLiteral("mainPages"));
    loginPage_ = new LoginPage(pages_);
    nearbyPage_ = new NearbyPage(userApi_, mapClient_, pages_);
    profilePage_ = new ProfilePage(userApi_, pages_);
    chargePage_ = new ChargePage(userApi_, pages_);
    pages_->addWidget(loginPage_);
    pages_->addWidget(nearbyPage_);
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
        guardContext_ = userApi_->loadCurrentOrder(
            0, 0, ev::user::ChargeOperation::Guard);
    });
    connect(userApi_, &UserApi::currentOrderLoaded, this,
            [this](const ev::user::RequestContext &context,
                   const ev::user::CurrentOrderResult &result) {
        if (result.order.has_value() && rememberedSelection_.has_value()
            && (rememberedSelection_->station.stationId != result.order->stationId
                || rememberedSelection_->charger.chargerId != result.order->chargerId)) {
            rememberedSelection_.reset();
        }
        authoritativeActiveOrder_ = result.order;
        hasActiveOrder_ = result.order.has_value();
        updateAuthenticatedNavigation();
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
        if (hasActiveOrder_) {
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
        hasActiveOrder_ = false;
        authoritativeActiveOrder_.reset();
        authenticatedNavigation_->setVisible(false);
        pages_->setCurrentWidget(loginPage_);
        loginPage_->setError(QStringLiteral("当前订单加载失败，请重新登录"));
    });
    connect(nearbyNavigationButton_, &QPushButton::clicked, this, [this] {
        if (!hasActiveOrder_ && !chargeFlowBlocked_) {
            if (pages_->currentWidget() == chargePage_) {
                chargePage_->leavePage();
            }
            pages_->setCurrentWidget(nearbyPage_);
        }
    });
    connect(currentOrderNavigationButton_, &QPushButton::clicked, this, [this] {
        if (hasActiveOrder_ && !chargeFlowBlocked_ && authoritativeActiveOrder_.has_value()) {
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
    connect(profileNavigationButton_, &QPushButton::clicked, this, [this] {
        if (chargeFlowBlocked_) {
            return;
        }
        if (pages_->currentWidget() == chargePage_) {
            chargePage_->leavePage();
        }
        pages_->setCurrentWidget(profilePage_);
        profilePage_->refresh();
    });
    connect(nearbyPage_, &NearbyPage::chargerSelected, this,
            [this](const ev::user::StationSelection &selection) {
        if (hasActiveOrder_ || chargeFlowBlocked_) {
            return;
        }
        rememberedSelection_ = selection;
        chargePage_->enterSelection(selection);
        pages_->setCurrentWidget(chargePage_);
    });
    connect(nearbyPage_, &NearbyPage::selectionInvalidated, this,
            [this](quint64 selectionGeneration) {
        if (chargeFlowBlocked_) {
            return;
        }
        rememberedSelection_.reset();
        chargePage_->invalidateSelection(selectionGeneration);
    });
    connect(chargePage_, &ChargePage::nearbyRefreshRequested,
            nearbyPage_, &NearbyPage::refreshAfterCharge);
    connect(chargePage_, &ChargePage::nearbyDetailRefreshReady,
            nearbyPage_, &NearbyPage::applyChargeStationDetail);
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
    connect(chargePage_, &ChargePage::chargeFlowBlockedChanged, this, [this](bool blocked) {
        chargeFlowBlocked_ = blocked;
        updateAuthenticatedNavigation();
    });
    connect(chargePage_, &ChargePage::backRequested, this, [this] {
        hasActiveOrder_ = false;
        authoritativeActiveOrder_.reset();
        rememberedSelection_.reset();
        nearbyNavigationButton_->setEnabled(true);
        currentOrderNavigationButton_->setVisible(false);
        updateAuthenticatedNavigation();
        pages_->setCurrentWidget(nearbyPage_);
    });
    connect(chargePage_, &ChargePage::activeOrderResolved, this, [this](bool active) {
        hasActiveOrder_ = active;
        if (!active) {
            authoritativeActiveOrder_.reset();
        }
        updateAuthenticatedNavigation();
    });
    connect(userApi_, &UserApi::chargeOrderChanged, this,
            [this](const ev::user::RequestContext &, const ev::user::Order &order) {
        hasActiveOrder_ = order.status == QStringLiteral("reserved")
            || order.status == QStringLiteral("charging");
        if (hasActiveOrder_) {
            authoritativeActiveOrder_ = order;
        } else {
            authoritativeActiveOrder_.reset();
        }
        updateAuthenticatedNavigation();
    });
    connect(userApi_, &UserApi::chargeSettled, this,
            [this](const ev::user::RequestContext &, const ev::user::Order &, qint64) {
        hasActiveOrder_ = false;
        authoritativeActiveOrder_.reset();
        updateAuthenticatedNavigation();
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
    profileNavigationButton_->setEnabled(!chargeFlowBlocked_);
}
