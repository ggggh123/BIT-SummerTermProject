#include "ui/MainWindow.h"

#include "net/TcpJsonClient.h"
#include "net/TencentMapClient.h"
#include "services/UserApi.h"
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
    chargePage_ = new QLabel(QStringLiteral("当前充电订单"), pages_);
    chargePage_->setObjectName(QStringLiteral("chargePage"));
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
    connect(userApi_, &UserApi::requestFailed, this, [this](const ev::user::ApiError &error) {
        if (pages_->currentWidget() == loginPage_) {
            loginPage_->setError(error.message);
        }
    });
    connect(userApi_, &UserApi::loginSucceeded, this, [this](const ev::user::User &) {
        userApi_->loadCurrentOrder();
    });
    connect(userApi_, &UserApi::currentOrderLoaded, this, [this](const ev::user::CurrentOrderResult &result) {
        hasActiveOrder_ = result.order.has_value();
        nearbyNavigationButton_->setEnabled(!hasActiveOrder_);
        currentOrderNavigationButton_->setVisible(hasActiveOrder_);
        authenticatedNavigation_->setVisible(true);
        pages_->setCurrentWidget(hasActiveOrder_ ? chargePage_ : nearbyPage_);
    });
    connect(nearbyNavigationButton_, &QPushButton::clicked, this, [this] {
        if (!hasActiveOrder_) {
            pages_->setCurrentWidget(nearbyPage_);
        }
    });
    connect(currentOrderNavigationButton_, &QPushButton::clicked, this, [this] {
        if (hasActiveOrder_) {
            pages_->setCurrentWidget(chargePage_);
        }
    });
    connect(profileNavigationButton_, &QPushButton::clicked, this, [this] {
        pages_->setCurrentWidget(profilePage_);
        profilePage_->refresh();
    });
    connect(nearbyPage_, &NearbyPage::navigationRequested, this,
            [this](ev::user::GeoPoint origin, ev::user::Station station) {
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
