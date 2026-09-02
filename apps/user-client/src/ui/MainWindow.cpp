#include "ui/MainWindow.h"

#include "net/TcpJsonClient.h"
#include "services/UserApi.h"
#include "ui/LoginPage.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(UserAppConfig config, QWidget *parent)
    : QMainWindow(parent)
    , client_(new TcpJsonClient(this))
    , userApi_(new UserApi(client_, this)) {
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

    pages_ = new QStackedWidget(centralWidget);
    pages_->setObjectName(QStringLiteral("mainPages"));
    loginPage_ = new LoginPage(pages_);
    nearbyPage_ = new QLabel(QStringLiteral("附近充电站"), pages_);
    nearbyPage_->setObjectName(QStringLiteral("nearbyPage"));
    chargePage_ = new QLabel(QStringLiteral("当前充电订单"), pages_);
    chargePage_->setObjectName(QStringLiteral("chargePage"));
    pages_->addWidget(loginPage_);
    pages_->addWidget(nearbyPage_);
    pages_->addWidget(chargePage_);
    pages_->setCurrentWidget(loginPage_);
    layout->addWidget(pages_);

    connect(loginPage_, &LoginPage::loginRequested, userApi_, &UserApi::loginByPhone);
    connect(userApi_, &UserApi::loginPendingChanged, loginPage_, &LoginPage::setPending);
    connect(userApi_, &UserApi::connectionChanged, loginPage_, &LoginPage::setConnectionAvailable);
    connect(userApi_, &UserApi::requestFailed, this, [this](const ev::user::ApiError &error) {
        loginPage_->setError(error.message);
    });
    connect(userApi_, &UserApi::loginSucceeded, this, [this](const ev::user::User &) {
        userApi_->loadCurrentOrder();
    });
    connect(userApi_, &UserApi::currentOrderLoaded, this, [this](const ev::user::CurrentOrderResult &result) {
        pages_->setCurrentWidget(result.order.has_value() ? chargePage_ : nearbyPage_);
    });

    if (!config.serverHost.isEmpty() && config.serverPort != 0) {
        client_->configure(config.serverHost, config.serverPort);
        client_->connectToServer();
    }

    setCentralWidget(centralWidget);
}
